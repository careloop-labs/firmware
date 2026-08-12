// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <gatt/services/core_service.h>
#include <gatt/gatt_common.h>

#include <string.h>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(core_service, LOG_LEVEL_INF);

enum {
    CORE_ATTR_CONTROL_VALUE = 2,
    CORE_ATTR_TIME_SYNC_VALUE = 4,
    CORE_ATTR_STATUS_VALUE = 6,
    CORE_ATTR_DATA_PIPE_VALUE = 9,
    CORE_ATTR_TRANSFER_CTRL_VALUE = 12,
};

struct core_write_ctx {
    uint8_t *buf;
    uint16_t max_len;
    uint16_t *len;
};

static ssize_t core_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    struct core_write_ctx *ctx = attr->user_data;

    LOG_INF("Core write len %u", len);
    LOG_HEXDUMP_INF(buf, len, "Core write data");

    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len > ctx->max_len) {
        return GATT_ERR_INVALID_LENGTH;
    }

    memcpy(ctx->buf, buf, len);
    *ctx->len = len;

    return len;
}

static ssize_t core_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset)
{
    const struct core_write_ctx *ctx = attr->user_data;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, ctx->buf, *ctx->len);
}

static struct bt_uuid_128 core_service_uuid = BT_UUID_INIT_128(CARELOOP_SERVICE_UUID);
static struct bt_uuid_128 control_char_uuid = BT_UUID_INIT_128(CL_CHAR_CONTROL_UUID);
static struct bt_uuid_128 time_sync_char_uuid = BT_UUID_INIT_128(CL_CHAR_TIME_SYNC_UUID);
static struct bt_uuid_128 status_char_uuid = BT_UUID_INIT_128(CL_CHAR_TELEMETRY_UUID);
static struct bt_uuid_128 data_pipe_char_uuid = BT_UUID_INIT_128(CL_CHAR_DATA_PIPE_UUID);
static struct bt_uuid_128 transfer_ctrl_char_uuid = BT_UUID_INIT_128(CL_CHAR_TRANSFER_CTRL_UUID);

#define CORE_CTRL_MAX_LEN          64U
#define CORE_TIME_SYNC_MAX_LEN     32U
#define CORE_STATUS_MAX_LEN        32U
#define CORE_DATA_PIPE_MAX_LEN     64U
#define CORE_TRANSFER_CTRL_MAX_LEN 32U

static uint8_t control_buf[CORE_CTRL_MAX_LEN];
static uint16_t control_len;
static uint8_t time_sync_buf[CORE_TIME_SYNC_MAX_LEN];
static uint16_t time_sync_len;
static uint8_t status_buf[CORE_STATUS_MAX_LEN] = { 0x00 };
static uint16_t status_len = 1U;
static uint8_t data_pipe_buf[CORE_DATA_PIPE_MAX_LEN];
static uint16_t data_pipe_len;
static uint8_t transfer_ctrl_buf[CORE_TRANSFER_CTRL_MAX_LEN];
static uint16_t transfer_ctrl_len;

static struct core_write_ctx control_ctx = {
    .buf = control_buf,
    .max_len = CORE_CTRL_MAX_LEN,
    .len = &control_len,
};

static struct core_write_ctx time_sync_ctx = {
    .buf = time_sync_buf,
    .max_len = CORE_TIME_SYNC_MAX_LEN,
    .len = &time_sync_len,
};

static struct core_write_ctx status_ctx = {
    .buf = status_buf,
    .max_len = CORE_STATUS_MAX_LEN,
    .len = &status_len,
};

static struct core_write_ctx data_pipe_ctx = {
    .buf = data_pipe_buf,
    .max_len = CORE_DATA_PIPE_MAX_LEN,
    .len = &data_pipe_len,
};

static struct core_write_ctx transfer_ctrl_ctx = {
    .buf = transfer_ctrl_buf,
    .max_len = CORE_TRANSFER_CTRL_MAX_LEN,
    .len = &transfer_ctrl_len,
};

BT_GATT_SERVICE_DEFINE(core_service,
    BT_GATT_PRIMARY_SERVICE(&core_service_uuid),
    BT_GATT_CHARACTERISTIC(&control_char_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, core_write, &control_ctx),
    BT_GATT_CHARACTERISTIC(&time_sync_char_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_WRITE,
                           NULL, core_write, &time_sync_ctx),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&status_char_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           core_read, NULL, &status_ctx),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&data_pipe_char_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, &data_pipe_ctx),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&transfer_ctrl_char_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_WRITE,
                           NULL, core_write, &transfer_ctrl_ctx),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

int core_service_init(void)
{
    LOG_INF("Core service initialized");
    return 0;
}

void core_service_notify_control(void)
{
    static const uint8_t payload = 0x01;

    (void)bt_gatt_notify(NULL, &core_service.attrs[CORE_ATTR_CONTROL_VALUE], &payload, sizeof(payload));
}

void core_service_notify_time_sync(void)
{
    static const uint8_t payload = 0x01;

    (void)bt_gatt_notify(NULL, &core_service.attrs[CORE_ATTR_TIME_SYNC_VALUE], &payload, sizeof(payload));
}

void core_service_notify_status(void)
{
    (void)bt_gatt_notify(NULL, &core_service.attrs[CORE_ATTR_STATUS_VALUE], status_buf, status_len);
}

void core_service_notify_data_pipe(void)
{
    static const uint8_t payload = 0x00;

    data_pipe_buf[0] = payload;
    data_pipe_len = 1U;

    (void)bt_gatt_notify(NULL, &core_service.attrs[CORE_ATTR_DATA_PIPE_VALUE], data_pipe_buf, data_pipe_len);
}

void core_service_notify_transfer_control(void)
{
    static const uint8_t payload = 0x00;

    transfer_ctrl_buf[0] = payload;
    transfer_ctrl_len = 1U;

    (void)bt_gatt_notify(NULL, &core_service.attrs[CORE_ATTR_TRANSFER_CTRL_VALUE],
                         transfer_ctrl_buf, transfer_ctrl_len);
}
