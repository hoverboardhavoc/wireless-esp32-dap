#ifndef __OTA_SLOT_H__
#define __OTA_SLOT_H__

// Marks the running slot valid (call once Wi-Fi is up) and starts the raw TCP image receiver on
// OTA_SLOT_PORT.
void ota_slot_start(void);

#define OTA_SLOT_PORT 5336

#endif
