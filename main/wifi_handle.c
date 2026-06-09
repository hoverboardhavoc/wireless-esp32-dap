#include "sdkconfig.h"

#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "main/wifi_configuration.h"
#include "main/uart_bridge.h"

#include "components/DAP/include/gpio_op.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"     // IDF v5.x: replaces removed esp_event_loop.h
#include "esp_netif.h"     // IDF v5.x: replaces removed tcpip_adapter
#include "esp_log.h"
#include "lwip/ip4_addr.h" // IP4_ADDR()

#ifdef CONFIG_IDF_TARGET_ESP8266
    #define PIN_LED_WIFI_STATUS 15
#elif defined CONFIG_IDF_TARGET_ESP32
    #define PIN_LED_WIFI_STATUS 27
#elif defined CONFIG_IDF_TARGET_ESP32C3
    #define PIN_LED_WIFI_STATUS 10
#elif defined CONFIG_IDF_TARGET_ESP32S3
    #define PIN_LED_WIFI_STATUS 4
#else
    #error unknown hardware
#endif

static EventGroupHandle_t wifi_event_group;
static int ssid_index = 0;

const int IPV4_GOTIP_BIT = BIT0;

static void ssid_change();

// IDF v5.x event model: separate WIFI_EVENT / IP_EVENT handlers registered with
// the default event loop, instead of the old monolithic system_event_t callback.
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        GPIO_SET_LEVEL_LOW(PIN_LED_WIFI_STATUS);
        wifi_event_sta_disconnected_t *info = (wifi_event_sta_disconnected_t *)event_data;
        os_printf("Disconnect reason : %d\r\n", (int)info->reason);
        ssid_change();
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
#if (USE_UART_BRIDGE == 1)
        uart_bridge_close();
#endif
        break;
    }
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        GPIO_SET_LEVEL_HIGH(PIN_LED_WIFI_STATUS);
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        os_printf("SYSTEM EVENT STA GOT IP : " IPSTR "\r\n", IP2STR(&event->ip_info.ip));
    }
}

static void ssid_change() {
    if (ssid_index > WIFI_LIST_SIZE - 1) {
        ssid_index = 0;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };

    strcpy((char *)wifi_config.sta.ssid, wifi_list[ssid_index].ssid);
    strcpy((char *)wifi_config.sta.password, wifi_list[ssid_index].password);
    ssid_index++;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
}

static void wait_for_ip() {
    uint32_t bits = IPV4_GOTIP_BIT;

    os_printf("Waiting for AP connection...\r\n");
    xEventGroupWaitBits(wifi_event_group, bits, false, true, portMAX_DELAY);
    os_printf("Connected to AP\r\n");
}

void wifi_init(void) {
    GPIO_FUNCTION_SET(PIN_LED_WIFI_STATUS);
    GPIO_SET_DIRECTION_NORMAL_OUT(PIN_LED_WIFI_STATUS);

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

#if (USE_STATIC_IP == 1)
    esp_netif_dhcpc_stop(sta_netif);

    esp_netif_ip_info_t ip_info;

    // Indirection so the preprocessor expands DAP_IP_* (a,b,c,d) before counting
    // IP4_ADDR's args; calling IP4_ADDR(&x, DAP_IP_ADDRESS) directly looks like 2 args.
#define MY_IP4_ADDR(...) IP4_ADDR(__VA_ARGS__)
    MY_IP4_ADDR(&ip_info.ip, DAP_IP_ADDRESS);
    MY_IP4_ADDR(&ip_info.gw, DAP_IP_GATEWAY);
    MY_IP4_ADDR(&ip_info.netmask, DAP_IP_NETMASK);
#undef MY_IP4_ADDR

    esp_netif_set_ip_info(sta_netif, &ip_info);
#else
    (void)sta_netif;
#endif // (USE_STATIC_IP == 1)

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ssid_change();
    ESP_ERROR_CHECK(esp_wifi_start());

    wait_for_ip();
}
