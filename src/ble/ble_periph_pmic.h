#ifndef BLE_PERIPH_PMIC_H_
#define BLE_PERIPH_PMIC_H_

#include <zephyr/bluetooth/bluetooth.h>

// Declaration of custom GATT service and characteristics UUIDs
#define PMIC_HUB_SERVICE_UUID BT_UUID_128_ENCODE(0x2100F600, 0x8445, 0x5fca, 0xb332, 0xc13064b9dea2)
#define PMIC_RD_ALL_CHARACTERISTIC_UUID BT_UUID_128_ENCODE(0x21002EAD, 0xA770, 0x57A7, 0xb333, 0xc13064b9dea2)
#define BATT_RD_CHARACTERISTIC_UUID BT_UUID_128_ENCODE(0xBA77E129, 0x2EAD, 0x5eea, 0x8e62, 0x6aadbe1e624f)
#define SHPHLD_WR_MV_CHARACTERISTIC_UUID BT_UUID_128_ENCODE(0x57EED111, 0x217E, 0x4faf, 0x956b, 0xafb01c17d0be)

extern struct k_msgq pmic_msgq;

int bt_init(void);

#endif
