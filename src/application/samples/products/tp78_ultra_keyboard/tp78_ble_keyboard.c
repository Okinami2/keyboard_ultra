#include <stddef.h>
#include <string.h>

#include "common_def.h"
#include "bts_def.h"
#include "bts_device_manager.h"
#include "bts_gatt_server.h"
#include "bts_gatt_stru.h"
#include "bts_le_gap.h"
#include "errcode.h"
#include "nv.h"
#include "securec.h"
#include "soc_osal.h"
#include "tcxo.h"
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
#define TP78_BLE_SLOT_COUNT 4
#define TP78_BLE_BOND_QUERY_COUNT 8
#define TP78_BLE_PAIRING_TIMEOUT_MS 120000
#define TP78_BLE_NV_KEY 0x7801
#define TP78_BLE_NV_MAGIC 0x54503738
#define TP78_BLE_NV_VERSION 2
#define TP78_BLE_INVALID_SLOT 0xFF
#define TP78_BLE_SLOT_NAME_MAX 32

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
static bool g_active;
static bool g_pairing;
static bool g_advertising;
static bool g_adv_restart;
static bool g_slot_switch_pending;
static bool g_nv_needs_reset;
static bool g_base_local_address_valid;
static uint64_t g_pairing_deadline;
static uint8_t g_identity_slot = TP78_BLE_INVALID_SLOT;
static bd_addr_t g_connected_address;
static bd_addr_t g_base_local_address;
static bd_addr_t g_pairing_bonds[TP78_BLE_BOND_QUERY_COUNT];
static uint16_t g_pairing_bond_count;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t selected_slot;
    uint8_t valid_mask;
    uint8_t reserved;
    bd_addr_t slots[TP78_BLE_SLOT_COUNT];
} tp78_ble_nv_state_t;

static tp78_ble_nv_state_t g_nv_state;

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

static bool tp78_ble_addr_equal(const bd_addr_t *left, const bd_addr_t *right)
{
    return left->type == right->type && memcmp(left->addr, right->addr, BD_ADDR_LEN) == 0;
}

static bool tp78_ble_slot_valid(uint8_t slot)
{
    return slot < TP78_BLE_SLOT_COUNT && (g_nv_state.valid_mask & (1U << slot)) != 0;
}

static uint16_t tp78_ble_get_bonds(bd_addr_t bonds[TP78_BLE_BOND_QUERY_COUNT])
{
    uint16_t count = TP78_BLE_BOND_QUERY_COUNT;
    if (gap_ble_get_bonded_devices(bonds, &count) != ERRCODE_BT_SUCCESS) {
        return 0;
    }
    return count > TP78_BLE_BOND_QUERY_COUNT ? TP78_BLE_BOND_QUERY_COUNT : count;
}

static bool tp78_ble_bond_list_contains(const bd_addr_t *address, const bd_addr_t *bonds, uint16_t count)
{
    for (uint16_t index = 0; index < count; index++) {
        if (tp78_ble_addr_equal(address, &bonds[index])) {
            return true;
        }
    }
    return false;
}

static void tp78_ble_save_state(void)
{
    errcode_t status = uapi_nv_write(TP78_BLE_NV_KEY, (const uint8_t *)&g_nv_state, sizeof(g_nv_state));
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] BLE slot NV write failed: 0x%x\r\n", status);
    }
}

static void tp78_ble_load_state(void)
{
    uint16_t length = 0;
    if (uapi_nv_read(TP78_BLE_NV_KEY, sizeof(g_nv_state), &length, (uint8_t *)&g_nv_state) != ERRCODE_SUCC ||
        length != sizeof(g_nv_state) || g_nv_state.magic != TP78_BLE_NV_MAGIC ||
        g_nv_state.version != TP78_BLE_NV_VERSION || g_nv_state.selected_slot >= TP78_BLE_SLOT_COUNT) {
        (void)memset_s(&g_nv_state, sizeof(g_nv_state), 0, sizeof(g_nv_state));
        g_nv_state.magic = TP78_BLE_NV_MAGIC;
        g_nv_state.version = TP78_BLE_NV_VERSION;
        g_nv_needs_reset = true;
    }
}

static void tp78_ble_clear_all_bonds(void)
{
    bd_addr_t bonded[TP78_BLE_BOND_QUERY_COUNT] = { 0 };
    uint16_t count = tp78_ble_get_bonds(bonded);
    for (uint16_t index = 0; index < count; index++) {
        (void)gap_ble_remove_white_list(&bonded[index]);
        (void)gap_ble_remove_pair(&bonded[index]);
    }
}

static void tp78_ble_validate_slots(void)
{
    bd_addr_t bonded[TP78_BLE_BOND_QUERY_COUNT] = { 0 };
    uint16_t count = tp78_ble_get_bonds(bonded);
    bool changed = false;
    for (uint8_t slot = 0; slot < TP78_BLE_SLOT_COUNT; slot++) {
        if (tp78_ble_slot_valid(slot) &&
            !tp78_ble_bond_list_contains(&g_nv_state.slots[slot], bonded, count)) {
            g_nv_state.valid_mask &= (uint8_t)~(1U << slot);
            changed = true;
        }
    }
    if (changed) {
        tp78_ble_save_state();
    }
}

static void tp78_ble_configure_white_list(void)
{
    for (uint8_t slot = 0; slot < TP78_BLE_SLOT_COUNT; slot++) {
        if (tp78_ble_slot_valid(slot)) {
            (void)gap_ble_remove_white_list(&g_nv_state.slots[slot]);
        }
    }
    if (!g_pairing && tp78_ble_slot_valid(g_nv_state.selected_slot)) {
        errcode_t status = gap_ble_add_white_list(&g_nv_state.slots[g_nv_state.selected_slot]);
        if (status != ERRCODE_BT_SUCCESS) {
            osal_printk("[tp78] BLE slot whitelist failed:0x%x\r\n", (unsigned int)status);
        }
    }
}

static bool tp78_ble_apply_slot_identity(void)
{
    uint8_t slot = g_nv_state.selected_slot;
    if (g_identity_slot == slot) {
        return true;
    }
    if (!g_base_local_address_valid) {
        if (gap_ble_get_local_addr(&g_base_local_address) != ERRCODE_BT_SUCCESS) {
            osal_printk("[tp78] BLE get local address failed\r\n");
            return false;
        }
        g_base_local_address_valid = true;
    }

    bd_addr_t slot_address = g_base_local_address;
    slot_address.addr[BD_ADDR_LEN - 1] = (uint8_t)('1' + slot);
    errcode_t address_status = gap_ble_set_local_addr(&slot_address);
    if (address_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE slot address failed:0x%x\r\n", (unsigned int)address_status);
        return false;
    }

    uint8_t slot_name[TP78_BLE_SLOT_NAME_MAX] = { 0 };
    size_t name_length = strlen(CONFIG_TP78_ULTRA_BLE_NAME);
    if (name_length > sizeof(slot_name) - 3) {
        name_length = sizeof(slot_name) - 3;
    }
    (void)memcpy_s(slot_name, sizeof(slot_name), CONFIG_TP78_ULTRA_BLE_NAME, name_length);
    slot_name[name_length++] = ' ';
    slot_name[name_length++] = (uint8_t)('1' + slot);
    errcode_t name_status = gap_ble_set_local_name(slot_name, (uint8_t)name_length);
    if (name_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE slot name failed:0x%x\r\n", (unsigned int)name_status);
        return false;
    }
    g_identity_slot = slot;
    osal_printk("[tp78] BLE identity switched to slot %u\r\n", (unsigned int)slot + 1U);
    return true;
}

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
        tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_REPORT_MAP,
        GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_AUTHENTICATION_NEED,
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

static void tp78_ble_stop_advertising(void)
{
    g_adv_restart = false;
    if (g_advertising) {
        (void)gap_ble_stop_adv(TP78_BLE_ADV_HANDLE);
    }
}

static void tp78_ble_start_advertising(void)
{
    if (!g_active || g_connected || !g_service_initialized) {
        return;
    }
    if (g_advertising) {
        g_adv_restart = true;
        (void)gap_ble_stop_adv(TP78_BLE_ADV_HANDLE);
        return;
    }
    if (!g_pairing && !tp78_ble_slot_valid(g_nv_state.selected_slot)) {
        osal_printk("[tp78] BLE slot %u is empty; hold Fn+F11 to pair\r\n",
            (unsigned int)g_nv_state.selected_slot + 1U);
        return;
    }
    if (!tp78_ble_apply_slot_identity()) {
        return;
    }
    tp78_ble_configure_white_list();

    uint8_t name[TP78_BLE_SLOT_NAME_MAX] = { 0 };
    size_t name_length_value = strlen(CONFIG_TP78_ULTRA_BLE_NAME);
    if (name_length_value > sizeof(name) - 3) {
        name_length_value = sizeof(name) - 3;
    }
    (void)memcpy_s(name, sizeof(name), CONFIG_TP78_ULTRA_BLE_NAME, name_length_value);
    name[name_length_value++] = ' ';
    name[name_length_value++] = (uint8_t)('1' + g_nv_state.selected_slot);
    uint8_t advertising[TP78_BLE_ADV_DATA_MAX] = {
        2, TP78_BLE_ADV_FLAGS_TYPE, TP78_BLE_ADV_FLAGS,
        3, TP78_BLE_ADV_SERVICE_DATA_16, (uint8_t)TP78_BLE_UUID_HID_SERVICE,
        (uint8_t)(TP78_BLE_UUID_HID_SERVICE >> 8),
    };
    uint8_t name_length = (uint8_t)name_length_value;
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
    parameters.channel_map = TP78_BLE_ADV_CHANNELS_ALL;
    parameters.peer_addr.type = BT_ADDRESS_TYPE_PUBLIC_DEVICE_ADDRESS;
    parameters.adv_type = GAP_BLE_ADV_CONN_SCAN_UNDIR;
    parameters.adv_filter_policy = g_pairing ? GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY :
        GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST;

    g_adv_restart = false;
    (void)gap_ble_set_adv_data(TP78_BLE_ADV_HANDLE, &data);
    (void)gap_ble_set_adv_param(TP78_BLE_ADV_HANDLE, &parameters);
    (void)gap_ble_start_adv(TP78_BLE_ADV_HANDLE);
}

static void tp78_ble_adv_started(uint8_t adv_id, adv_status_t status)
{
    unused(adv_id);
    g_advertising = status == ADV_STATUS_ADVERTISING;
    if (g_advertising && (!g_active || g_connected)) {
        (void)gap_ble_stop_adv(TP78_BLE_ADV_HANDLE);
    }
}

static void tp78_ble_adv_stopped(uint8_t adv_id, adv_status_t status)
{
    unused(adv_id);
    unused(status);
    g_advertising = false;
    if (g_adv_restart) {
        g_adv_restart = false;
        tp78_ble_start_advertising();
    }
}

static void tp78_ble_connection_changed(uint16_t conn_id, bd_addr_t *address, gap_ble_conn_state_t state,
    gap_ble_pair_state_t pair_state, gap_ble_disc_reason_t reason)
{
    g_connection_id = conn_id;
    g_connected = state == GAP_BLE_STATE_CONNECTED;
    if (g_connected) {
        osal_printk("[tp78] BLE connected, pair_state:%u, pairing:%u\r\n",
            (unsigned int)pair_state, g_pairing ? 1U : 0U);
        g_advertising = false;
        g_connected_address = *address;
    } else if (g_active) {
        g_slot_switch_pending = false;
        osal_printk("[tp78] BLE disconnected, reason:%u, pairing:%u\r\n",
            (unsigned int)reason, g_pairing ? 1U : 0U);
        tp78_ble_start_advertising();
    }
}

static void tp78_ble_pair_complete(uint16_t conn_id, const bd_addr_t *address, errcode_t status)
{
    unused(conn_id);
    if (!g_pairing || address == NULL) {
        return;
    }
    if (status == ERRCODE_BT_KEY_MISSING) {
        errcode_t remove_status = gap_ble_remove_pair(address);
        osal_printk("[tp78] BLE stale key removed, status:0x%x\r\n", (unsigned int)remove_status);
        return;
    }
    if (status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE pairing failed, status:0x%x\r\n", (unsigned int)status);
        return;
    }
    bd_addr_t bonded[TP78_BLE_BOND_QUERY_COUNT] = { 0 };
    uint16_t bonded_count = tp78_ble_get_bonds(bonded);
    const bd_addr_t *identity = NULL;
    for (uint16_t index = 0; index < bonded_count; index++) {
        if (!tp78_ble_bond_list_contains(&bonded[index], g_pairing_bonds, g_pairing_bond_count)) {
            identity = &bonded[index];
            break;
        }
    }
    if (identity == NULL && tp78_ble_bond_list_contains(address, bonded, bonded_count)) {
        identity = address;
    }
    if (identity == NULL) {
        osal_printk("[tp78] BLE paired identity address unavailable\r\n");
        return;
    }
    uint8_t slot = g_nv_state.selected_slot;
    if (tp78_ble_slot_valid(slot) && !tp78_ble_addr_equal(identity, &g_nv_state.slots[slot])) {
        (void)gap_ble_remove_pair(&g_nv_state.slots[slot]);
    }
    for (uint8_t other = 0; other < TP78_BLE_SLOT_COUNT; other++) {
        if (other != slot && tp78_ble_slot_valid(other) &&
            tp78_ble_addr_equal(identity, &g_nv_state.slots[other])) {
            g_nv_state.valid_mask &= (uint8_t)~(1U << other);
        }
    }
    g_nv_state.slots[slot] = *identity;
    g_nv_state.valid_mask |= (uint8_t)(1U << slot);
    g_pairing = false;
    g_pairing_deadline = 0;
    g_pairing_bond_count = 0;
    tp78_ble_save_state();
    tp78_ble_configure_white_list();
    osal_printk("[tp78] BLE paired in slot %u\r\n", (unsigned int)slot + 1U);
}

static void tp78_ble_enabled(uint8_t status)
{
    unused(status);
    if (g_service_initialized) {
        return;
    }
    g_service_initialized = true;

    gap_ble_sec_params_t security = {
        .bondable = 1,
        .io_capability = GAP_BLE_IO_CAPABILITY_NOINPUTNOOUTPUT,
        .sc_enable = 1,
        .sc_mode = GAP_BLE_GAP_SECURITY_MODE1_LEVEL2,
    };
    bt_uuid_t app_uuid = { 0 };
    app_uuid.uuid_len = sizeof(g_app_uuid);
    (void)memcpy_s(app_uuid.uuid, sizeof(app_uuid.uuid), g_app_uuid, sizeof(g_app_uuid));
    (void)gap_ble_set_local_appearance(GAP_BLE_APPEARANCE_TYPE_KEYBOARD);
    errcode_t security_status = gap_ble_set_sec_param(&security);
    if (security_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE security config failed:0x%x\r\n", (unsigned int)security_status);
    }
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
    if (g_nv_needs_reset) {
        tp78_ble_clear_all_bonds();
        tp78_ble_save_state();
        g_nv_needs_reset = false;
        osal_printk("[tp78] BLE legacy bonds cleared; pair each slot again\r\n");
    } else {
        tp78_ble_validate_slots();
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
    gap_callbacks.pair_result_cb = tp78_ble_pair_complete;
    gap_callbacks.start_adv_cb = tp78_ble_adv_started;
    gap_callbacks.stop_adv_cb = tp78_ble_adv_stopped;
    device_callbacks.power_on_cb = tp78_ble_powered_on;
    device_callbacks.ble_enable_cb = tp78_ble_enabled;
    tp78_ble_load_state();

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
    return g_connected && !g_slot_switch_pending && g_keyboard_handle != TP78_BLE_INVALID_HANDLE;
}

void tp78_ble_keyboard_set_active(bool active)
{
    g_active = active;
    g_pairing = false;
    g_pairing_deadline = 0;
    g_slot_switch_pending = false;
    tp78_ble_stop_advertising();
    if (!active) {
        if (g_connected) {
            (void)gap_ble_disconnect_remote_device(&g_connected_address);
        }
        return;
    }
    if (g_connected) {
        errcode_t disconnect_status = gap_ble_disconnect_remote_device(&g_connected_address);
        if (disconnect_status != ERRCODE_BT_SUCCESS) {
            osal_printk("[tp78] BLE mode disconnect failed:0x%x\r\n", (unsigned int)disconnect_status);
        }
    } else {
        tp78_ble_start_advertising();
    }
}

void tp78_ble_keyboard_process(void)
{
    if (g_pairing && g_pairing_deadline != 0 && uapi_tcxo_get_ms() >= g_pairing_deadline) {
        g_pairing = false;
        g_pairing_deadline = 0;
        tp78_ble_stop_advertising();
        osal_printk("[tp78] BLE pairing timed out\r\n");
        if (g_connected) {
            (void)gap_ble_disconnect_remote_device(&g_connected_address);
        } else {
            tp78_ble_start_advertising();
        }
    }
}

void tp78_ble_keyboard_select_slot(uint8_t slot)
{
    if (slot >= TP78_BLE_SLOT_COUNT || slot == g_nv_state.selected_slot) {
        return;
    }
    g_nv_state.selected_slot = slot;
    tp78_ble_save_state();
    g_pairing = false;
    g_pairing_deadline = 0;
    tp78_ble_stop_advertising();
    g_identity_slot = TP78_BLE_INVALID_SLOT;
    osal_printk("[tp78] BLE slot %u selected\r\n", (unsigned int)slot + 1U);
    if (!g_active) {
        return;
    }
    if (g_connected) {
        g_slot_switch_pending = true;
        errcode_t disconnect_status = gap_ble_disconnect_remote_device(&g_connected_address);
        if (disconnect_status != ERRCODE_BT_SUCCESS) {
            g_slot_switch_pending = false;
            osal_printk("[tp78] BLE slot disconnect failed:0x%x\r\n", (unsigned int)disconnect_status);
        }
    } else {
        tp78_ble_start_advertising();
    }
}

uint8_t tp78_ble_keyboard_get_slot(void)
{
    return g_nv_state.selected_slot;
}

void tp78_ble_keyboard_start_pairing(void)
{
    if (!g_active || !g_service_initialized) {
        return;
    }
    uint8_t slot = g_nv_state.selected_slot;
    (void)memset_s(g_pairing_bonds, sizeof(g_pairing_bonds), 0, sizeof(g_pairing_bonds));
    g_pairing_bond_count = tp78_ble_get_bonds(g_pairing_bonds);
    g_pairing = true;
    g_pairing_deadline = uapi_tcxo_get_ms() + TP78_BLE_PAIRING_TIMEOUT_MS;
    tp78_ble_stop_advertising();
    osal_printk("[tp78] BLE slot %u pairing for 120 seconds\r\n", (unsigned int)slot + 1U);
    if (g_connected) {
        (void)gap_ble_disconnect_remote_device(&g_connected_address);
    } else {
        tp78_ble_start_advertising();
    }
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
