#include <stddef.h>
#include <string.h>

#include "common_def.h"
#include "securec.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_device_manager.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"
#include "tcxo.h"
#include "tp78_sle_keyboard.h"

#define TP78_SLE_ADV_HANDLE 1
#define TP78_SLE_SERVICE_UUID 0xABCD
#define TP78_SLE_REPORT_UUID 0x1122
#define TP78_SLE_UUID_SHORT_LENGTH 2
#define TP78_SLE_FRAME_MAGIC 0xA5
#define TP78_SLE_FRAME_VERSION 0x02
#define TP78_SLE_FRAME_HEADER_LENGTH 7
#define TP78_SLE_FRAME_CRC_LENGTH 2
#define TP78_SLE_REPORT_KEYBOARD 1
#define TP78_SLE_REPORT_MOUSE 2
#define TP78_SLE_REPORT_CONSUMER 3
#define TP78_SLE_REPORT_KEYBOARD_LED 0x10
#define TP78_SLE_MAX_FRAME_LENGTH 32
#define TP78_SLE_CRC_INITIAL 0xFFFF
#define TP78_SLE_CRC_POLYNOMIAL 0x1021
#define TP78_SLE_ADV_INTERVAL 0xC8
#define TP78_SLE_ADV_CHANNELS_ALL 0x07
#define TP78_SLE_CONN_INTERVAL 0x64
#define TP78_SLE_SUPERVISION_TIMEOUT 0x1F4
#define TP78_SLE_PAIRING_TIMEOUT_MS 120000
#define TP78_SLE_BOND_QUERY_COUNT 4

static uint8_t g_server_id;
static uint16_t g_service_handle;
static uint16_t g_property_handle;
static uint16_t g_connection_id;
static uint8_t g_sequence[4];
static bool g_connected;
static bool g_service_initialized;
static bool g_active;
static bool g_pairing;
static bool g_advertising;
static bool g_adv_restart;
static uint64_t g_pairing_deadline;
static sle_addr_t g_connected_address;
static uint8_t g_keyboard_leds;
static uint8_t g_app_uuid[] = { 0x78, 0x03 };
static uint8_t g_property_value[TP78_SLE_MAX_FRAME_LENGTH];
static uint8_t g_uuid_base[SLE_UUID_LEN] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static bool tp78_sle_addr_equal(const sle_addr_t *left, const sle_addr_t *right)
{
    return left->type == right->type && memcmp(left->addr, right->addr, SLE_ADDR_LEN) == 0;
}

static uint16_t tp78_sle_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = TP78_SLE_CRC_INITIAL;
    for (uint16_t index = 0; index < length; index++) {
        crc ^= (uint16_t)data[index] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ?
                (uint16_t)((crc << 1) ^ TP78_SLE_CRC_POLYNOMIAL) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void tp78_sle_uuid(uint16_t value, sle_uuid_t *uuid)
{
    (void)memcpy_s(uuid->uuid, sizeof(uuid->uuid), g_uuid_base, sizeof(g_uuid_base));
    uuid->len = TP78_SLE_UUID_SHORT_LENGTH;
    uuid->uuid[14] = (uint8_t)value;
    uuid->uuid[15] = (uint8_t)(value >> 8);
}

static int32_t tp78_sle_encode(uint8_t type, const uint8_t *payload, uint16_t payload_length,
    uint8_t *frame, uint16_t capacity)
{
    uint16_t frame_length = TP78_SLE_FRAME_HEADER_LENGTH + payload_length + TP78_SLE_FRAME_CRC_LENGTH;
    if (frame == NULL || payload == NULL || frame_length > capacity || type > TP78_SLE_REPORT_CONSUMER) {
        return -1;
    }

    frame[0] = TP78_SLE_FRAME_MAGIC;
    frame[1] = TP78_SLE_FRAME_VERSION;
    frame[2] = type;
    frame[3] = 0;
    frame[4] = g_sequence[type]++;
    frame[5] = (uint8_t)payload_length;
    frame[6] = (uint8_t)(payload_length >> 8);
    if (memcpy_s(frame + TP78_SLE_FRAME_HEADER_LENGTH, capacity - TP78_SLE_FRAME_HEADER_LENGTH,
        payload, payload_length) != EOK) {
        return -1;
    }
    uint16_t crc = tp78_sle_crc16(frame + 1, frame_length - 3);
    frame[frame_length - 2] = (uint8_t)crc;
    frame[frame_length - 1] = (uint8_t)(crc >> 8);
    return frame_length;
}

static bool tp78_sle_get_bonded_receiver(sle_addr_t *address)
{
    sle_addr_t bonded[TP78_SLE_BOND_QUERY_COUNT] = { 0 };
    uint16_t count = TP78_SLE_BOND_QUERY_COUNT;
    if (sle_get_bonded_devices(bonded, &count) != ERRCODE_SLE_SUCCESS || count == 0) {
        return false;
    }
    *address = bonded[0];
    return true;
}

static void tp78_sle_keep_single_bond(void)
{
    sle_addr_t bonded[TP78_SLE_BOND_QUERY_COUNT] = { 0 };
    uint16_t count = TP78_SLE_BOND_QUERY_COUNT;
    if (sle_get_bonded_devices(bonded, &count) != ERRCODE_SLE_SUCCESS) {
        return;
    }
    if (count > TP78_SLE_BOND_QUERY_COUNT) {
        count = TP78_SLE_BOND_QUERY_COUNT;
    }
    for (uint16_t index = 1; index < count; index++) {
        (void)sle_remove_paired_remote_device(&bonded[index]);
    }
}

static void tp78_sle_remove_all_bonds(void)
{
    sle_addr_t bonded[TP78_SLE_BOND_QUERY_COUNT] = { 0 };
    uint16_t count = TP78_SLE_BOND_QUERY_COUNT;
    if (sle_get_bonded_devices(bonded, &count) != ERRCODE_SLE_SUCCESS) {
        return;
    }
    if (count > TP78_SLE_BOND_QUERY_COUNT) {
        count = TP78_SLE_BOND_QUERY_COUNT;
    }
    for (uint16_t index = 0; index < count; index++) {
        (void)sle_remove_paired_remote_device(&bonded[index]);
    }
}

static void tp78_sle_stop_advertising(void)
{
    g_adv_restart = false;
    if (g_advertising) {
        (void)sle_stop_announce(TP78_SLE_ADV_HANDLE);
    }
}

static void tp78_sle_start_advertising(void)
{
    if (!g_active || g_connected || !g_service_initialized) {
        return;
    }

    sle_addr_t receiver = { 0 };
    if (!g_pairing && !tp78_sle_get_bonded_receiver(&receiver)) {
        osal_printk("[tp78] SLE receiver is empty; hold Fn+F12 to pair\r\n");
        return;
    }
    if (g_advertising) {
        g_adv_restart = true;
        (void)sle_stop_announce(TP78_SLE_ADV_HANDLE);
        return;
    }

    const char name[] = CONFIG_TP78_ULTRA_SLE_NAME;
    uint8_t response[2 + sizeof(name) - 1];
    uint8_t advertising[] = {
        0x01, 0x01, 0x01,
        0x05, 0x04, 0x0B, 0x06, 0x09, 0x06,
        0x03, 0x12, 0x09, 0x06, 0x07, 0x03, 0x02, 0x05, 0x00,
    };
    response[0] = sizeof(response);
    response[1] = 0x09;
    (void)memcpy_s(response + 2, sizeof(response) - 2, name, sizeof(name) - 1);

    sle_announce_param_t parameters = { 0 };
    parameters.announce_mode = g_pairing ? SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE :
        SLE_ANNOUNCE_MODE_CONNECTABLE_DIRECTED;
    parameters.announce_handle = TP78_SLE_ADV_HANDLE;
    parameters.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    parameters.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    parameters.announce_channel_map = TP78_SLE_ADV_CHANNELS_ALL;
    parameters.announce_interval_min = TP78_SLE_ADV_INTERVAL;
    parameters.announce_interval_max = TP78_SLE_ADV_INTERVAL;
    parameters.conn_interval_min = TP78_SLE_CONN_INTERVAL;
    parameters.conn_interval_max = TP78_SLE_CONN_INTERVAL;
    parameters.conn_max_latency = 0;
    parameters.conn_supervision_timeout = TP78_SLE_SUPERVISION_TIMEOUT;
    if (!g_pairing) {
        parameters.peer_addr = receiver;
    }

    sle_announce_data_t data = {
        .announce_data = advertising,
        .announce_data_len = sizeof(advertising),
        .seek_rsp_data = response,
        .seek_rsp_data_len = sizeof(response),
    };
    g_adv_restart = false;
    (void)sle_set_announce_param(TP78_SLE_ADV_HANDLE, &parameters);
    (void)sle_set_announce_data(TP78_SLE_ADV_HANDLE, &data);
    (void)sle_start_announce(TP78_SLE_ADV_HANDLE);
}

static void tp78_sle_adv_started(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    g_advertising = status == ERRCODE_SLE_SUCCESS;
    if (g_advertising && (!g_active || g_connected)) {
        (void)sle_stop_announce(TP78_SLE_ADV_HANDLE);
    }
}

static void tp78_sle_adv_stopped(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
    g_advertising = false;
    if (g_adv_restart) {
        g_adv_restart = false;
        tp78_sle_start_advertising();
    }
}

static void tp78_sle_adv_terminated(uint32_t announce_id)
{
    unused(announce_id);
    g_advertising = false;
}

static int32_t tp78_sle_add_service(void)
{
    sle_uuid_t app_uuid = { 0 };
    app_uuid.len = sizeof(g_app_uuid);
    if (memcpy_s(app_uuid.uuid, sizeof(app_uuid.uuid), g_app_uuid, sizeof(g_app_uuid)) != EOK ||
        ssaps_register_server(&app_uuid, &g_server_id) != ERRCODE_SLE_SUCCESS) {
        return -1;
    }

    sle_uuid_t service_uuid = { 0 };
    tp78_sle_uuid(TP78_SLE_SERVICE_UUID, &service_uuid);
    if (ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle) != ERRCODE_SLE_SUCCESS) {
        return -1;
    }

    ssaps_property_info_t property = { 0 };
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
        SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP | SSAP_OPERATE_INDICATION_BIT_WRITE |
        SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    tp78_sle_uuid(TP78_SLE_REPORT_UUID, &property.uuid);
    property.value = g_property_value;
    property.value_len = sizeof(g_property_value);
    if (ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle) !=
        ERRCODE_SLE_SUCCESS) {
        return -1;
    }

    uint8_t notification_value[] = { 0x01, 0x00 };
    ssaps_desc_info_t descriptor = { 0 };
    descriptor.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    descriptor.value = notification_value;
    descriptor.value_len = sizeof(notification_value);
    if (ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_property_handle, &descriptor) !=
        ERRCODE_SLE_SUCCESS ||
        ssaps_start_service(g_server_id, g_service_handle) != ERRCODE_SLE_SUCCESS) {
        return -1;
    }
    return 0;
}

static void tp78_sle_receive_write(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *request,
    errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    if (status != ERRCODE_SLE_SUCCESS || request == NULL || request->value == NULL ||
        request->length < TP78_SLE_FRAME_HEADER_LENGTH + TP78_SLE_FRAME_CRC_LENGTH ||
        request->value[0] != TP78_SLE_FRAME_MAGIC || request->value[1] != TP78_SLE_FRAME_VERSION ||
        request->value[2] != TP78_SLE_REPORT_KEYBOARD_LED || request->value[5] != 1) {
        return;
    }
    uint16_t crc = tp78_sle_crc16(request->value + 1, request->length - 3);
    uint16_t expected = (uint16_t)request->value[request->length - 2] |
        ((uint16_t)request->value[request->length - 1] << 8);
    if (crc == expected) {
        g_keyboard_leds = request->value[TP78_SLE_FRAME_HEADER_LENGTH];
    }
}

static void tp78_sle_connection_changed(uint16_t conn_id, const sle_addr_t *address,
    sle_acb_state_t state, sle_pair_state_t pair_state, sle_disc_reason_t reason)
{
    unused(reason);
    g_connection_id = conn_id;
    g_connected = state == SLE_ACB_STATE_CONNECTED;
    if (g_connected) {
        g_advertising = false;
        g_connected_address = *address;
        sle_addr_t receiver = { 0 };
        if (!g_pairing && (!tp78_sle_get_bonded_receiver(&receiver) ||
            !tp78_sle_addr_equal(&receiver, address))) {
            (void)sle_disconnect_remote_device(address);
        } else if (g_pairing && pair_state == SLE_PAIR_NONE) {
            (void)sle_pair_remote_device(address);
        }
        if (pair_state == SLE_PAIR_PAIRED && g_pairing) {
            g_pairing = false;
            g_pairing_deadline = 0;
        }
    } else if (g_active) {
        tp78_sle_start_advertising();
    }
}

static void tp78_sle_pair_complete(uint16_t conn_id, const sle_addr_t *address, errcode_t status)
{
    unused(conn_id);
    unused(address);
    if (!g_pairing || status != ERRCODE_SLE_SUCCESS) {
        return;
    }
    g_pairing = false;
    g_pairing_deadline = 0;
    osal_printk("[tp78] SLE receiver paired\r\n");
}

static void tp78_sle_enabled(uint8_t status)
{
    unused(status);
    if (!g_service_initialized && tp78_sle_add_service() == 0) {
        g_service_initialized = true;
        tp78_sle_keep_single_bond();
        tp78_sle_start_advertising();
    }
}

static void tp78_sle_powered_on(uint8_t status)
{
    if (status == 0) {
        (void)enable_sle();
    }
}

int32_t tp78_sle_keyboard_init(void)
{
    sle_dev_manager_callbacks_t device_callbacks = { 0 };
    sle_connection_callbacks_t connection_callbacks = { 0 };
    sle_announce_seek_callbacks_t announce_callbacks = { 0 };
    ssaps_callbacks_t server_callbacks = { 0 };

    device_callbacks.sle_power_on_cb = tp78_sle_powered_on;
    device_callbacks.sle_enable_cb = tp78_sle_enabled;
    connection_callbacks.connect_state_changed_cb = tp78_sle_connection_changed;
    connection_callbacks.pair_complete_cb = tp78_sle_pair_complete;
    announce_callbacks.announce_enable_cb = tp78_sle_adv_started;
    announce_callbacks.announce_disable_cb = tp78_sle_adv_stopped;
    announce_callbacks.announce_terminal_cb = tp78_sle_adv_terminated;
    server_callbacks.write_request_cb = tp78_sle_receive_write;

    if (sle_dev_manager_register_callbacks(&device_callbacks) != ERRCODE_SLE_SUCCESS ||
        sle_connection_register_callbacks(&connection_callbacks) != ERRCODE_SLE_SUCCESS ||
        sle_announce_seek_register_callbacks(&announce_callbacks) != ERRCODE_SLE_SUCCESS ||
        ssaps_register_callbacks(&server_callbacks) != ERRCODE_SLE_SUCCESS) {
        return -1;
    }
#if (CORE_NUMS < 2)
    if (enable_sle() != ERRCODE_SLE_SUCCESS) {
        return -1;
    }
#endif
    return 0;
}

bool tp78_sle_keyboard_is_ready(void)
{
    return g_connected && g_property_handle != 0;
}

void tp78_sle_keyboard_set_active(bool active)
{
    g_active = active;
    g_pairing = false;
    g_pairing_deadline = 0;
    tp78_sle_stop_advertising();
    if (!active) {
        if (g_connected) {
            (void)sle_disconnect_remote_device(&g_connected_address);
        }
        return;
    }
    if (g_connected) {
        (void)sle_disconnect_remote_device(&g_connected_address);
    } else {
        tp78_sle_start_advertising();
    }
}

void tp78_sle_keyboard_process(void)
{
    if (g_pairing && g_pairing_deadline != 0 && uapi_tcxo_get_ms() >= g_pairing_deadline) {
        g_pairing = false;
        g_pairing_deadline = 0;
        tp78_sle_stop_advertising();
        osal_printk("[tp78] SLE pairing timed out\r\n");
        if (g_connected) {
            (void)sle_disconnect_remote_device(&g_connected_address);
        } else {
            tp78_sle_start_advertising();
        }
    }
}

void tp78_sle_keyboard_start_pairing(void)
{
    if (!g_active || !g_service_initialized) {
        return;
    }
    tp78_sle_remove_all_bonds();
    g_pairing = true;
    g_pairing_deadline = uapi_tcxo_get_ms() + TP78_SLE_PAIRING_TIMEOUT_MS;
    tp78_sle_stop_advertising();
    osal_printk("[tp78] SLE receiver pairing for 120 seconds\r\n");
    if (g_connected) {
        (void)sle_disconnect_remote_device(&g_connected_address);
    } else {
        tp78_sle_start_advertising();
    }
}

static int32_t tp78_sle_send(uint8_t type, const uint8_t *payload, uint16_t payload_length)
{
    uint8_t frame[TP78_SLE_MAX_FRAME_LENGTH];
    if (!tp78_sle_keyboard_is_ready()) {
        return -1;
    }
    int32_t frame_length = tp78_sle_encode(type, payload, payload_length, frame, sizeof(frame));
    if (frame_length < 0) {
        return -1;
    }
    ssaps_ntf_ind_t notification = {
        .handle = g_property_handle,
        .type = SSAP_PROPERTY_TYPE_VALUE,
        .value = frame,
        .value_len = (uint16_t)frame_length,
    };
    return ssaps_notify_indicate(g_server_id, g_connection_id, &notification) == ERRCODE_SLE_SUCCESS ? 0 : -1;
}

int32_t tp78_sle_keyboard_send(uint8_t modifiers, const uint8_t keys[6])
{
    uint8_t report[8] = {
        modifiers, 0, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]
    };
    return tp78_sle_send(TP78_SLE_REPORT_KEYBOARD, report, sizeof(report));
}

int32_t tp78_sle_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    uint8_t report[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel };
    return tp78_sle_send(TP78_SLE_REPORT_MOUSE, report, sizeof(report));
}

int32_t tp78_sle_consumer_send(uint16_t usage)
{
    uint8_t report[2] = { (uint8_t)usage, (uint8_t)(usage >> 8) };
    return tp78_sle_send(TP78_SLE_REPORT_CONSUMER, report, sizeof(report));
}
