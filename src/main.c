/*
 * npm2100_nrf54l15_BFG
 * main.c
 * Bluetooth fuel gauge demo application main file
 * auth: ddhd
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <dk_buttons_and_leds.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include "threads.h"
#include "tsync.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

K_SEM_DEFINE(sem_gpio_ready, 0, 1);

int main(void)
{
    int err;

    k_sem_take(&sem_pmic_ready, K_FOREVER);
    k_sem_give(&sem_gpio_ready);
    k_sem_take(&sem_ble_ready, K_FOREVER);

    for (;;)
    {
        k_sleep(K_MSEC(2000));
    }
    return err;
}