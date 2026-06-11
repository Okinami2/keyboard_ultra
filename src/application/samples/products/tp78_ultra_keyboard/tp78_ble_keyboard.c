#include <stddef.h>
#include <string.h>

#include "common_def.h"
#include "bts_def.h"
#include "bts_device_manager.h"
#include "bts_gatt_server.h"
#include "bts_gatt_stru.h"
#include "bts_le_gap.h"
#include "errcode.h"
#include "securec.h"
#include "soc_osal.h"
#include "tp78_ble_keyboard.h"

#define TP78_BLE_SERVER_ID 1
#define TP78_BLE_UUID_LENGTH 2
#define TP78_BLE_INVALID_HANDLE 0
#define TP78_BLE_REPORT_KEYBOARD 1
#define TP78_BLE_REPORT_MOUSE 2
#define TP78_BLE_REPORT_CONSUMER 3
#define TP78_BLE_ADV_DATA_MAX 64
#define TP78_BLE_ADV_INTERVAL_MIN 0x30
#define TP78_BLE_ADV_INTERVAL_MAX 0x60
#define TP78_BLE_ADV_HANDLE 1
#define TP78_BLE_ADV_DURATION_FOREVER 0
#define TP78_BLE_ADV_CHANNELS_ALL 0x07
#define TP78_BLE_ADV_FLAGS_TYPE 0x01
#define TP78_BLE_ADV_FLAGS 0x05
#define TP78_BLE_ADV_SERVICE_DATA_16 0x16
#define TP78_BLE_ADV_COMPLETE_NAME 0x09

#define TP78_BLE_UUID_HID_SERVICE 0x1812
#define TP78_BLE_UUID_HID_INFORMATION 0x2A4A
#define TP78_BLE_UUID_REPORT_MAP 0x2A4B
#define TP78_BLE_UUID_HID_CONTROL_POINT 0x2A4C
#define TP78_BLE_UUID_REPORT 0x2A4D
#define TP78_BLE_UUID_PROTOCOL_MODE 0x2A4E
#define TP78_BLE_UUID_CCC 0x2902
#define TP78_BLE_UUID_REPORT_REFERENCE 0x2908

static uint8_t g_server_id;
static uint16_t g_connection_id;
static uint16_t g_keyboard_handle;
static uint16_t g_mouse_handle;
static uint16_t g_consumer_handle;
static bool g_connected;
static bool g_service_initialized;

static uint8_t g_hid_information[] = { 0x11, 0x01, 0x00, 0x03 };
static uint8_t g_protocol_mode[] = { 0x01 };
static uint8_t g_control_point[] = { 0x00 };
static uint8_t g_keyboard_value[8];
static uint8_t g_mouse_value[4];
static uint8_t g_consumer_value[2];
static uint8_t g_keyboard_output[1];
static uint8_t g_ccc_value[2];
static uint8_t g_keyboard_reference[] = { TP78_BLE_REPORT_KEYBOARD, 0x01 };
static uint8_t g_mouse_reference[] = { TP78_BLE_REPORT_MOUSE, 0x01 };
static uint8_t g_consumer_reference[] = { TP78_BLE_REPORT_CONSUMER, 0x01 };
static uint8_t g_output_reference[] = { TP78_BLE_REPORT_KEYBOARD, 0x02 };
static uint8_t g_app_uuid[] = { 0x78, 0x03 };

static uint8_t g_report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, TP78_BLE_REPORT_KEYBOARD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
    0x95, 0x06, 0x75, 0x08, 0x81, 0x00, 0xC0,

    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, TP78_BLE_REPORT_MOUSE,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,

    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, TP78_BLE_REPORT_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

static void tp78_ble_uuid(uint16_t value, bt_uuid_t *uuid)
{
    uuid->uuid_len = TP78_BLE_UUID_LENGTH;
    uuid->uuid[0] = (uint8_t)(value >> 8);
    uuid->uuid[1] = (uint8_t)value;
}

static errcode_t tp78_ble_add_descriptor(uint16_t service_handle, uint16_t uuid_value, uint8_t *value,
    uint16_t length, uint16_t permissions)
{
    gatts_add_desc_info_t descriptor = { 0 };
    uint16_t handle;
    tp78_ble_uuid(uuid_value, &descriptor.desc_uuid);
    descriptor.permissions = permissions;
    descriptor.value = value;
    descriptor.value_len = length;
    return gatts_add_descriptor_sync(g_server_id, service_handle, &descriptor, &handle);
}

static errcode_t tp78_ble_add_characteristic(uint16_t service_handle, uint16_t uuid_value, uint16_t permissions,
    uint8_t properties, uint8_t *value, uint16_t length, uint16_t *value_handle)
{
    gatts_add_chara_info_t characteristic = { 0 };
    gatts_add_character_result_t result = { 0 };
    tp78_ble_uuid(uuid_value, &characteristic.chara_uuid);
    characteristic.permissions = permissions;
    characteristic.properties = properties;
    characteristic.value = value;
    characteristic.value_len = length;
    errcode_t status = gatts_add_characteristic_sync(g_server_id, service_handle, &characteristic, &result);
    if (status == ERRCODE_BT_SUCCESS && value_handle != NULL) {
        *value_handle = result.value_handle;
    }
    return status;
}

static errcode_t tp78_ble_add_input_report(uint16_t service_handle, uint8_t *value, uint16_t length,
    uint8_t *reference, uint16_t *value_handle)
{
    if (tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_REPORT, GATT_ATTRIBUTE_PERMISSION_READ,
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_NOTIFY, value, length, value_handle) !=
        ERRCODE_BT_SUCCESS ||
        tp78_ble_add_descriptor(service_handle, TP78_BLE_UUID_CCC, g_ccc_value, sizeof(g_ccc_value),
        GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_descriptor(service_handle, TP78_BLE_UUID_REPORT_REFERENCE, reference, 2,
        GATT_ATTRIBUTE_PERMISSION_READ) != ERRCODE_BT_SUCCESS) {
        return ERRCODE_BT_FAIL;
    }
    return ERRCODE_BT_SUCCESS;
}

static errcode_t tp78_ble_add_service_contents(uint16_t service_handle)
{
    if (tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_HID_INFORMATION,
        GATT_ATTRIBUTE_PERMISSION_READ, GATT_CHARACTER_PROPERTY_BIT_READ,
        g_hid_information, sizeof(g_hid_information), NULL) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_REPORT_MAP, GATT_ATTRIBUTE_PERMISSION_READ,
        GATT_CHARACTER_PROPERTY_BIT_READ, g_report_map, sizeof(g_report_map), NULL) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_PROTOCOL_MODE,
        GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE,
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP,
        g_protocol_mode, sizeof(g_protocol_mode), NULL) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_input_report(service_handle, g_keyboard_value, sizeof(g_keyboard_value),
        g_keyboard_reference, &g_keyboard_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_input_report(service_handle, g_mouse_value, sizeof(g_mouse_value),
        g_mouse_reference, &g_mouse_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_input_report(service_handle, g_consumer_value, sizeof(g_consumer_value),
        g_consumer_reference, &g_consumer_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_REPORT,
        GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE,
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE |
            GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP,
        g_keyboard_output, sizeof(g_keyboard_output), NULL) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_descriptor(service_handle, TP78_BLE_UUID_REPORT_REFERENCE, g_output_reference,
        sizeof(g_output_reference), GATT_ATTRIBUTE_PERMISSION_READ) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_HID_CONTROL_POINT,
        GATT_ATTRIBUTE_PERMISSION_WRITE, GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP,
        g_control_point, sizeof(g_control_point), NULL) != ERRCODE_BT_SUCCESS) {
        return ERRCODE_BT_FAIL;
    }
    return gatts_start_service(g_server_id, service_handle);
}

static void tp78_ble_start_advertising(void)
{
    const char name[] = CONFIG_TP78_ULTRA_BLE_NAME;
    uint8_t advertising[TP78_BLE_ADV_DATA_MAX] = {
        2, TP78_BLE_ADV_FLAGS_TYPE, TP78_BLE_ADV_FLAGS,
        3, TP78_BLE_ADV_SERVICE_DATA_16, (uint8_t)TP78_BLE_UUID_HID_SERVICE,
        (uint8_t)(TP78_BLE_UUID_HID_SERVICE >> 8),
    };
    uint8_t name_length = (uint8_t)strlen(name);
    uint8_t offset = 7;
    if (name_length > TP78_BLE_ADV_DATA_MAX - offset - 2) {
        name_length = TP78_BLE_ADV_DATA_MAX - offset - 2;
    }
    advertising[offset++] = name_length + 1;
    advertising[offset++] = TP78_BLE_ADV_COMPLETE_NAME;
    (void)memcpy_s(advertising + offset, sizeof(advertising) - offset, name, name_length);

    gap_ble_config_adv_data_t data = {
        .adv_data = advertising,
        .adv_length = offset + name_length,
        .scan_rsp_data = NULL,
        .scan_rsp_length = 0,
    };
    gap_ble_adv_params_t parameters = { 0 };
    parameters.min_interval = TP78_BLE_ADV_INTERVAL_MIN;
    parameters.max_interval = TP78_BLE_ADV_INTERVAL_MAX;
    parameters.duration = TP78_BLE_ADV_DURATION_FOREVER;
    parameters.peer_addr.type = BT_ADDRESS_TYPE_PUBLIC_DEVICE_ADDRESS;
    parameters.channel_map = TP78_BLE_ADV_CHANNELS_ALL;
    parameters.adv_type = GAP_BLE_ADV_CONN_SCAN_UNDIR;
    parameters.adv_filter_policy = GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    (void)gap_ble_set_adv_data(TP78_BLE_ADV_HANDLE, &data);
    (void)gap_ble_set_adv_param(TP78_BLE_ADV_HANDLE, &parameters);
    (void)gap_ble_start_adv(TP78_BLE_ADV_HANDLE);
}

static void tp78_ble_connection_changed(uint16_t conn_id, bd_addr_t *address, gap_ble_conn_state_t state,
    gap_ble_pair_state_t pair_state, gap_ble_disc_reason_t reason)
{
    unused(address);
    unused(pair_state);
    unused(reason);
    g_connection_id = conn_id;
    g_connected = state == GAP_BLE_STATE_CONNECTED;
    if (!g_connected) {
        tp78_ble_start_advertising();
    }
}

static void tp78_ble_enabled(uint8_t status)
{
    unused(status);
    if (g_service_initialized) {
        return;
    }
    g_service_initialized = true;

    const char name[] = CONFIG_TP78_ULTRA_BLE_NAME;
    bt_uuid_t app_uuid = { 0 };
    app_uuid.uuid_len = sizeof(g_app_uuid);
    (void)memcpy_s(app_uuid.uuid, sizeof(app_uuid.uuid), g_app_uuid, sizeof(g_app_uuid));
    (void)gap_ble_set_local_name((uint8_t *)name, (uint8_t)strlen(name));
    (void)gap_ble_set_local_appearance(GAP_BLE_APPEARANCE_TYPE_KEYBOARD);
    if (gatts_register_server(&app_uuid, &g_server_id) != ERRCODE_BT_SUCCESS) {
        g_service_initialized = false;
        return;
    }

    bt_uuid_t service_uuid = { 0 };
    uint16_t service_handle;
    tp78_ble_uuid(TP78_BLE_UUID_HID_SERVICE, &service_uuid);
    if (gatts_add_service_sync(g_server_id, &service_uuid, true, &service_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_service_contents(service_handle) != ERRCODE_BT_SUCCESS) {
        g_service_initialized = false;
        return;
    }
    tp78_ble_start_advertising();
}

static void tp78_ble_powered_on(uint8_t status)
{
    if (status == 0) {
        (void)enable_ble();
    }
}

int32_t tp78_ble_keyboard_init(void)
{
    gatts_callbacks_t gatt_callbacks = { 0 };
    gap_ble_callbacks_t gap_callbacks = { 0 };
    bts_dev_manager_callbacks_t device_callbacks = { 0 };

    gap_callbacks.conn_state_change_cb = tp78_ble_connection_changed;
    device_callbacks.power_on_cb = tp78_ble_powered_on;
    device_callbacks.ble_enable_cb = tp78_ble_enabled;

    if (gatts_register_callbacks(&gatt_callbacks) != ERRCODE_BT_SUCCESS ||
        gap_ble_register_callbacks(&gap_callbacks) != ERRCODE_BT_SUCCESS ||
        bts_dev_manager_register_callbacks(&device_callbacks) != ERRCODE_BT_SUCCESS) {
        return -1;
    }
#if (CORE_NUMS < 2)
    if (enable_ble() != ERRCODE_BT_SUCCESS) {
        return -1;
    }
#endif
    return 0;
}

bool tp78_ble_keyboard_is_ready(void)
{
    return g_connected && g_keyboard_handle != TP78_BLE_INVALID_HANDLE;
}

static int32_t tp78_ble_send(uint16_t handle, const uint8_t *data, uint16_t length)
{
    if (!g_connected || handle == TP78_BLE_INVALID_HANDLE || data == NULL) {
        return -1;
    }
    gatts_ntf_ind_t notification = {
        .attr_handle = handle,
        .value_len = length,
        .value = (uint8_t *)data,
    };
    return gatts_notify_indicate(g_server_id, g_connection_id, &notification) == ERRCODE_BT_SUCCESS ? 0 : -1;
}

int32_t tp78_ble_keyboard_send(uint8_t modifiers, const uint8_t keys[6])
{
    uint8_t report[8] = {
        modifiers, 0, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]
    };
    return tp78_ble_send(g_keyboard_handle, report, sizeof(report));
}

int32_t tp78_ble_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    uint8_t report[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel };
    return tp78_ble_send(g_mouse_handle, report, sizeof(report));
}

int32_t tp78_ble_consumer_send(uint16_t usage)
{
    uint8_t report[2] = { (uint8_t)usage, (uint8_t)(usage >> 8) };
    return tp78_ble_send(g_consumer_handle, report, sizeof(report));
}
