// The probe runs from the hoverboard bridge's OTA slots (partitions.csv). This file confirms the
// running image so the rollback bootloader keeps it, and receives a replacement image over a raw
// TCP connection: the bytes go into the inactive slot, and a clean EOF makes it the boot slot and
// restarts. Any error aborts the update and the probe keeps running. No authentication.
//
// CONFIG_OTA_SLOT_TRIAL (default off): the image never marks its slot valid, so any reset returns
// to the other slot, and a connection that closes with zero bytes is a reset request. ESP-IDF
// refuses esp_ota_begin while the running slot is pending, so a trial build cannot write the other
// slot (that slot holds the image the reset returns to).
#include "main/ota_slot.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "ota_slot";

static void receive_one(int sock) {
    static char buf[1024];
    int n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) {
#ifdef CONFIG_OTA_SLOT_TRIAL
        ESP_LOGI(TAG, "empty connection: reset requested, restarting to roll back");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
#else
        ESP_LOGW(TAG, "empty connection: nothing written");
        return;
#endif
    }
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (slot == NULL) {
        ESP_LOGE(TAG, "update failed: no inactive slot");
        return;
    }
    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update failed: begin %s: %s", slot->label, esp_err_to_name(err));
        return;
    }
    size_t total = 0;
    for (;; n = recv(sock, buf, sizeof(buf), 0)) {
        if (n == 0) {
            break;
        }
        if (n < 0) {
            ESP_LOGE(TAG, "update failed: recv errno %d after %u B", errno, (unsigned)total);
            esp_ota_abort(ota);
            return;
        }
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "update failed: write at %u B: %s", (unsigned)total, esp_err_to_name(err));
            esp_ota_abort(ota);
            return;
        }
        total += n;
    }
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update failed: end after %u B: %s", (unsigned)total, esp_err_to_name(err));
        return;
    }
    err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update failed: set boot %s: %s", slot->label, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "update written: %u B into %s, restarting", (unsigned)total, slot->label);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void ota_slot_task(void *arg) {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listener < 0) {
        ESP_LOGE(TAG, "receiver not started: socket errno %d", errno);
        vTaskDelete(NULL);
    }
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(OTA_SLOT_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0) {
        ESP_LOGE(TAG, "receiver not started: bind/listen errno %d", errno);
        close(listener);
        vTaskDelete(NULL);
    }
    for (;;) {
        int sock = accept(listener, NULL, NULL);
        if (sock < 0) {
            continue;
        }
        receive_one(sock);
        close(sock);
    }
}

static volatile bool joined;

static void guard_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(OTA_SLOT_JOIN_TIMEOUT_S * 1000));
    if (!joined) {
        ESP_LOGE(TAG, "no Wi-Fi address in %d s while pending, restarting to roll back",
                 OTA_SLOT_JOIN_TIMEOUT_S);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    vTaskDelete(NULL);
}

void ota_slot_guard(void) {
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "running %s pending verification", running->label);
        xTaskCreate(guard_task, "ota_guard", 2048, NULL, 5, NULL);
    }
}

void ota_slot_start(void) {
    joined = true;
#ifdef CONFIG_OTA_SLOT_TRIAL
    ESP_LOGI(TAG, "trial build: %s left pending, a reset returns to the other slot",
             esp_ota_get_running_partition()->label);
#else
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "running %s marked valid: %s", esp_ota_get_running_partition()->label,
             esp_err_to_name(err));
#endif
    xTaskCreate(ota_slot_task, "ota_slot", 3072, NULL, 5, NULL);
}
