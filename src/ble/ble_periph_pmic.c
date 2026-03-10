/*
 * npm2100_nrf54l15_BFG
 * ble_periph_pmic.c
 * ble application, receives pmic information and sends to BLE.
 * auth: ddhd
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <soc.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>

#include <zephyr/sys/printk.h>

#include "ble_periph_pmic.h"
#include "pmic.h"
#include "threads.h"
#include "tsync.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);
K_MSGQ_DEFINE(ble_cfg_pmic_msgq, sizeof(int32_t), 8, 4);
K_SEM_DEFINE(sem_ble_ready, 0, 1);

#define BLE_STATE_LED DK_LED2

#define BLE_NOTIFY_INTERVAL K_MSEC(1000)
#define BLE_THREAD_STACK_SIZE 1024
#define BLE_THREAD_PRIORITY 5

#define MAXLEN 19

#define BT_UUID_PMIC_HUB BT_UUID_DECLARE_128(PMIC_HUB_SERVICE_UUID)
#define BT_UUID_PMIC_HUB_RD_ALL BT_UUID_DECLARE_128(PMIC_RD_ALL_CHARACTERISTIC_UUID)
#define BT_UUID_PMIC_HUB_SHPHLD_WR BT_UUID_DECLARE_128(SHPHLD_WR_MV_CHARACTERISTIC_UUID)

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME // from prj.conf
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct gpio_dt_spec bt_status_led = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

// BT globals and callbacks
struct bt_conn *m_connection_handle = NULL;

static const struct bt_le_adv_param *adv_param =
    BT_LE_ADV_PARAM((BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use
                                                                          identity address */
                    800,   /* Min Advertising Interval 500ms (800*0.625ms) 16383 max*/
                    801,   /* Max Advertising Interval 500.625ms (801*0.625ms) 16384 max*/
                    NULL); /* Set to NULL for undirected advertising */

static struct k_work adv_work;
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, PMIC_HUB_SERVICE_UUID),
};

/*This function is called whenever the Client Characteristic Control Descriptor
(CCCD) has been changed by the GATT client, for each of the characteristics*/
static void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    switch (value)
    {
    case BT_GATT_CCC_NOTIFY:
        break;
    case 0:
        break;
    default:
        LOG_ERR("Error, CCCD has been set to an invalid value");
    }
}

// fn called when lsldo wr characteristic has been written to by a client
static ssize_t on_receive_shphld_wr(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                                    uint16_t len, uint16_t offset, uint8_t flags)
{
    static bool request;

    LOG_INF("Received lsldo wr data, handle %d, conn %p, len %d, data: 0x", attr->handle, conn, len);
    LOG_INF("REQUESTED SHIP MODE: %d", request);
    bt_conn_disconnect(m_connection_handle, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    k_msgq_put(&ble_cfg_pmic_msgq, &request, K_NO_WAIT);

    return len;
}

/*
primary
rd status
rd batt
wr shphld
*/
BT_GATT_SERVICE_DEFINE(
    pmic_hub, BT_GATT_PRIMARY_SERVICE(BT_UUID_PMIC_HUB),
    BT_GATT_CHARACTERISTIC(BT_UUID_PMIC_HUB_RD_ALL, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ, NULL, NULL, NULL),
    BT_GATT_CCC(on_cccd_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_PMIC_HUB_SHPHLD_WR, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL, on_receive_shphld_wr, NULL), );

static void adv_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_INF("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully started");
}

static void advertising_start(void)
{
    k_work_submit(&adv_work);
}

static void recycled_cb(void)
{
    LOG_INF("Connection object available from previous conn. Disconnect complete.");
    advertising_start();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_WRN("Connection failed (err %u)", err);
        return;
    }
    m_connection_handle = bt_conn_ref(conn);

    LOG_INF("Connected");

    struct bt_conn_info info;
    err = bt_conn_get_info(m_connection_handle, &info);
    if (err)
    {
        LOG_ERR("bt_conn_get_info() returned %d", err);
        return;
    }

    gpio_pin_set_dt(&bt_status_led, 1);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
    bt_conn_unref(m_connection_handle);
    m_connection_handle = NULL;
    gpio_pin_set_dt(&bt_status_led, 0);
}

struct bt_conn_cb connection_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled_cb,
};

static void ble_report_batt_volt(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    const struct bt_gatt_attr *attr = &pmic_hub.attrs[2];
    struct bt_gatt_notify_params params = {
        .uuid = BT_UUID_PMIC_HUB_RD_ALL, .attr = attr, .data = data, .len = len, .func = NULL};

    if (bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY))
    {
        if (bt_gatt_notify_cb(conn, &params))
        {
            LOG_ERR("Error, unable to send notification");
        }
    }
    else
    {
        LOG_WRN("Warning, notification not enabled for pmic stat characteristic");
    }
}

int bt_init(void)
{
    int err;

    // Setting up Bluetooth
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return -1;
    }
    LOG_INF("Bluetooth initialized");
    if (IS_ENABLED(CONFIG_SETTINGS))
    {
        settings_load();
    }
    m_connection_handle = NULL;
    bt_conn_cb_register(&connection_callbacks);
    k_work_init(&adv_work, adv_work_handler);
    advertising_start();

    return 0;
}

void ble_write_thread(void)
{
    int err;
    LOG_INF("ble write thread: entered");

    k_sem_take(&sem_gpio_ready, K_FOREVER);
    LOG_INF("ble write thread: woken by main");

    if (bt_init() != 0)
    {
        LOG_ERR("unable to initialize BLE!");
    }
    k_sem_give(&sem_ble_ready);

    err = gpio_pin_configure_dt(&bt_status_led, GPIO_OUTPUT_INACTIVE);
    if (err)
    {
        LOG_WRN("Configuring BT status LED failed (err %d)", err);
    }

    struct pmic_report_msg pmic_msg;
    for (;;)
    {
        // Wait indefinitely for msg's from other modules
        k_msgq_get(&pmic_msgq, &pmic_msg, K_FOREVER);
        LOG_INF("BLE thread rx from PMIC: V: %.2f T: %.2f SoC: %d ", pmic_msg.batt_voltage, pmic_msg.temp,
                pmic_msg.batt_soc);

        if (m_connection_handle) // if ble connection present
        {
            bt_bas_set_battery_level(pmic_msg.batt_soc); // report batt soc via standard service

            static uint8_t ble_batt_volt[MAXLEN]; // report batt volt string via custom service
            int len = snprintf(ble_batt_volt, MAXLEN, "BATT: %.2f V", pmic_msg.batt_voltage);
            if (!(len >= 0 && len < MAXLEN))
            {
                LOG_ERR("ble pmic report too large. (%d)", len);
            }
            else
            {
                ble_report_batt_volt(m_connection_handle, ble_batt_volt, len);
            }
        }
        else
        {
            LOG_INF("BLE Thread does not detect an active BLE connection");

            // toggle the LED while advertising
            gpio_pin_toggle_dt(&bt_status_led);
        }

        k_sleep(BLE_NOTIFY_INTERVAL);
    }
}

K_THREAD_DEFINE(ble_write_thread_id, BLE_THREAD_STACK_SIZE, ble_write_thread, NULL, NULL, NULL, BLE_THREAD_PRIORITY, 0,
                0);
