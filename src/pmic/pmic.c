/*
 * npm2100_nrf54l15_BFG
 * ble_periph_pmic.c
 * fuel gauging, for ref see npm2100_fuel_gauge
 * auth: ddhd
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <math.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/npm2100.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm2100_vbat.h>
#include <zephyr/dt-bindings/regulator/npm2100.h>
#include <zephyr/sys/util.h>

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm2100_vbat.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "pmic.h"

#include <nrf_fuel_gauge.h>

static int64_t ref_time;
static bool fuel_gauge_initialized;

static const struct battery_model_primary battery_model = {
#include <battery_models/primary_cell/LR44.inc>
};

static const struct i2c_dt_spec pmic_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(npm2100_pmic));
static const struct device *pmic_regulators = DEVICE_DT_GET(DT_NODELABEL(npm2100_regulators));
static const struct device *vbat = DEVICE_DT_GET(DT_NODELABEL(npm2100_vbat));

#define PMIC_THREAD_STACK_SIZE 1024
#define PMIC_THREAD_PRIORITY 5
#define PMIC_SLEEP_INTERVAL_MS 1000

LOG_MODULE_REGISTER(pmic, LOG_LEVEL_INF);

K_MSGQ_DEFINE(pmic_msgq, sizeof(struct pmic_report_msg), 8, 4);
K_SEM_DEFINE(sem_pmic_ready, 0, 1);

static int shphld_state(bool *state)
{
    int ret;
    uint8_t reg;

    ret = i2c_reg_read_byte_dt(&pmic_i2c, 0x16, &reg);
    if (ret)
    {
        LOG_ERR("Could not read STATUS register (%d)", ret);
        return ret;
    }
    *state = reg & 1U;

    return 0;
}

static int read_sensors(const struct device *vbat, float *voltage, float *temp)
{
    struct sensor_value value;
    int ret;

    ret = sensor_sample_fetch(vbat);
    if (ret < 0)
    {
        return ret;
    }

    sensor_channel_get(vbat, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
    *voltage = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(vbat, SENSOR_CHAN_DIE_TEMP, &value);
    *temp = (float)value.val1 + ((float)value.val2 / 1000000);

    return 0;
}

int fuel_gauge_init(const struct device *vbat, char *bat_name, size_t n)
{
    int err;
    bool state;

    if (!device_is_ready(pmic_regulators))
    {
        LOG_ERR("PMIC device is not ready (init_res: %d)", pmic_regulators->state->init_res);
        return -ENODEV;
    }
    LOG_INF("PMIC device ok");

    // check if we are in shphld. if it's high, we are "awake", so engage fuel gauge and typical operation.
    err = shphld_state(&state);
    if (err)
    {
        return err;
    }

    if (state)
    {

        k_sem_give(&sem_pmic_ready);

        struct nrf_fuel_gauge_init_parameters parameters = {
            .model_primary = &battery_model,
            .i0 = AVERAGE_CURRENT,
            .opt_params = NULL,
        };
        struct nrf_fuel_gauge_runtime_parameters rt_params = {
            .a = NAN,
            .b = NAN,
            .c = NAN,
            .d = NAN,
            .discard_positive_deltaz = true,
        };
        int ret;

        ret = read_sensors(vbat, &parameters.v0, &parameters.t0);
        if (ret < 0)
        {
            return ret;
        }

        ret = nrf_fuel_gauge_init(&parameters, NULL);
        if (ret < 0)
        {
            return ret;
        }

        ref_time = k_uptime_get();
        nrf_fuel_gauge_param_adjust(&rt_params);
        strncpy(bat_name, battery_model.name, n);
        err = 0;
    }
    else
    {
        err = -1;
    }

    return err;
}

int fuel_gauge_update(const struct device *vbat, uint8_t *soc)
{
    float voltage;
    float temp;
    float delta;
    int ret;
    struct pmic_report_msg pmic_ble_report;

    ret = read_sensors(vbat, &voltage, &temp);
    if (ret < 0)
    {
        return ret;
    }

    delta = (float)k_uptime_delta(&ref_time) / 1000.f;

    *soc = (uint8_t)nrf_fuel_gauge_process(voltage, AVERAGE_CURRENT, temp, delta, NULL);

    LOG_INF("PMIC Thread sending: V: %.3f, T: %.2f, SoC: %d", (double)voltage, (double)temp, *soc);
    pmic_ble_report.batt_voltage = voltage;
    pmic_ble_report.temp = temp;
    pmic_ble_report.batt_soc = *soc;
    k_msgq_put(&pmic_msgq, &pmic_ble_report, K_FOREVER);

    return 0;
}

int pmic_fg_thread(void)
{

    fuel_gauge_initialized = false;
    uint8_t soc;

    for (;;)
    {
        if (!fuel_gauge_initialized)
        {
            int err;
            char bat_name[16];
            err = fuel_gauge_init(vbat, bat_name, 16);
            if (err < 0)
            {
                LOG_INF("Could not initialise fuel gauge.");
                return 0;
            }
            LOG_INF("Fuel gauge initialised for %s battery.", bat_name);

            fuel_gauge_initialized = true;
        }
        fuel_gauge_update(vbat, &soc);
        k_msleep(PMIC_SLEEP_INTERVAL_MS);
    }
}

int pmic_reg_thread(void)
{
    int request;

    for (;;)
    {
        k_msgq_get(&ble_cfg_pmic_msgq, &request, K_FOREVER); // suspend till msg avail
        regulator_parent_ship_mode(pmic_regulators);
    }
}

K_THREAD_DEFINE(pmic_reg_thread_id, PMIC_THREAD_STACK_SIZE, pmic_reg_thread, NULL, NULL, NULL, PMIC_THREAD_PRIORITY, 0,
                1000);
K_THREAD_DEFINE(pmic_fg_thread_id, PMIC_THREAD_STACK_SIZE, pmic_fg_thread, NULL, NULL, NULL, PMIC_THREAD_PRIORITY, 0,
                0);