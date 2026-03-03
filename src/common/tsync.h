#ifndef TSYNC_H
#define TSYNC_H

#include <zephyr/kernel.h>

extern struct k_sem sem_ble_ready;
extern struct k_sem sem_gpio_ready;
extern struct k_sem sem_pmic_ready;

#endif // TSYNC_H