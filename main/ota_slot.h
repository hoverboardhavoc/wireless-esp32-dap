#ifndef __OTA_SLOT_H__
#define __OTA_SLOT_H__

// Call before Wi-Fi: if the running slot is pending verification and Wi-Fi has not come up within
// OTA_SLOT_JOIN_TIMEOUT_S, restarts so the bootloader returns to the previous slot.
void ota_slot_guard(void);

// Call once Wi-Fi has an address: marks the running slot valid and starts the raw TCP image
// receiver on OTA_SLOT_PORT.
void ota_slot_start(void);

#define OTA_SLOT_JOIN_TIMEOUT_S 120

#define OTA_SLOT_PORT 5336

#endif
