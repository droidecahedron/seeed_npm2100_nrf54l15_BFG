#ifndef PMIC_H_
#define PMIC_H_

#include <stdint.h>
#include <zephyr/device.h>

enum battery_type
{
    /* Cylindrical non-rechargeable Alkaline AA */
    BATTERY_TYPE_ALKALINE_AA,
    /* Cylindrical non-rechargeable Alkaline AAA */
    BATTERY_TYPE_ALKALINE_AAA,
    /* Cylindrical non-rechargeable Alkaline 2SAA (2 x AA in series) */
    BATTERY_TYPE_ALKALINE_2SAA,
    /* Cylindrical non-rechargeable Alkaline 2SAAA (2 x AAA in series) */
    BATTERY_TYPE_ALKALINE_2SAAA,
    /* Alkaline coin cell LR44 */
    BATTERY_TYPE_ALKALINE_LR44,
    /* Lithium-manganese dioxide coin cell CR2032 */
    BATTERY_TYPE_LITHIUM_CR2032,
};
#define AVERAGE_CURRENT (CONFIG_ACTIVE_CURRENT_UA * 1e-6f)

int fuel_gauge_init(const struct device *vbat, char *bat_name, size_t n);
int fuel_gauge_update(const struct device *vbat, uint8_t *soc);

struct pmic_report_msg
{
    double batt_voltage;
    double temp;
    uint8_t batt_soc;
};
extern struct k_msgq ble_cfg_pmic_msgq;

#endif