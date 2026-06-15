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
#define TP78_BLE_ADV_FLAGS 0x06
#define TP78_BLE_ADV_COMPLETE_UUID16 0x03
#define TP78_BLE_ADV_COMPLETE_NAME 0x09
#define TP78_BLE_SLOT_COUNT 4
#define TP78_BLE_PAIRING_TIMEOUT_MS 120000
#define TP78_BLE_NV_KEY 0x7801
#define TP78_BLE_CCC_NV_KEY 0x7802
#define TP78_BLE_NV_MAGIC 0x54503738
#define TP78_BLE_CCC_NV_MAGIC 0x43434331
#define TP78_BLE_NV_VERSION 3
#define TP78_BLE_CCC_NV_VERSION 1
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
static uint16_t g_keyboard_ccc_handle;
static uint16_t g_mouse_ccc_handle;
static uint16_t g_consumer_ccc_handle;
static bool g_connected;
static bool g_authenticated;
static bool g_keyboard_notify_enabled;
static bool g_mouse_notify_enabled;
static bool g_consumer_notify_enabled;
static bool g_service_initialized;
static bool g_active;
static bool g_pairing;
static bool g_advertising;
static bool g_adv_restart;
static bool g_slot_switch_pending;
static bool g_nv_needs_reset;
static bool g_base_local_address_valid;
static bool g_pending_auth_valid;
static bool g_pending_peer_valid;
static bool g_pair_key_retry_attempted;
static bool g_keyboard_report_sent;
static uint64_t g_pairing_deadline;
static uint8_t g_identity_slot = TP78_BLE_INVALID_SLOT;
static uint8_t g_key_capture_slot = TP78_BLE_INVALID_SLOT;
static uint8_t g_connection_slot = TP78_BLE_INVALID_SLOT;
static errcode_t g_last_notify_error = ERRCODE_BT_SUCCESS;
static bd_addr_t g_connected_address;
static bd_addr_t g_base_local_address;
static bd_addr_t g_pending_peer_address;
static ble_auth_info_evt_t g_pending_auth_info;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t selected_slot;
    uint8_t valid_mask;
    uint8_t reserved;
    bd_addr_t slots[TP78_BLE_SLOT_COUNT];
} tp78_ble_nv_state_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t keyboard_mask;
    uint8_t mouse_mask;
    uint8_t consumer_mask;
} tp78_ble_ccc_nv_state_t;

static tp78_ble_nv_state_t g_nv_state;
static tp78_ble_ccc_nv_state_t g_ccc_nv_state;

static uint8_t g_hid_information[] = { 0x11, 0x01, 0x00, 0x03 };
static uint8_t g_protocol_mode[] = { 0x01 };
static uint8_t g_control_point[] = { 0x00 };
static uint8_t g_keyboard_value[8];
static uint8_t g_mouse_value[4];
static uint8_t g_consumer_value[2];
static uint8_t g_keyboard_output[1];
static uint8_t g_keyboard_ccc[] = { 0x00, 0x00 };
static uint8_t g_mouse_ccc[] = { 0x00, 0x00 };
static uint8_t g_consumer_ccc[] = { 0x00, 0x00 };
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

static void tp78_ble_save_state(void)
{
    errcode_t status = uapi_nv_write(TP78_BLE_NV_KEY, (const uint8_t *)&g_nv_state, sizeof(g_nv_state));
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] BLE slot NV write failed: 0x%x\r\n", status);
    }
}

static void tp78_ble_save_ccc_state(void)
{
    errcode_t status = uapi_nv_write(TP78_BLE_CCC_NV_KEY, (const uint8_t *)&g_ccc_nv_state,
        sizeof(g_ccc_nv_state));
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] BLE CCC NV write failed: 0x%x\r\n", status);
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

    length = 0;
    if (uapi_nv_read(TP78_BLE_CCC_NV_KEY, sizeof(g_ccc_nv_state), &length,
        (uint8_t *)&g_ccc_nv_state) != ERRCODE_SUCC ||
        length != sizeof(g_ccc_nv_state) || g_ccc_nv_state.magic != TP78_BLE_CCC_NV_MAGIC ||
        g_ccc_nv_state.version != TP78_BLE_CCC_NV_VERSION) {
        (void)memset_s(&g_ccc_nv_state, sizeof(g_ccc_nv_state), 0, sizeof(g_ccc_nv_state));
        g_ccc_nv_state.magic = TP78_BLE_CCC_NV_MAGIC;
        g_ccc_nv_state.version = TP78_BLE_CCC_NV_VERSION;
        g_ccc_nv_state.keyboard_mask = g_nv_state.valid_mask;
        g_ccc_nv_state.mouse_mask = g_nv_state.valid_mask;
        g_ccc_nv_state.consumer_mask = g_nv_state.valid_mask;
        tp78_ble_save_ccc_state();
    }
}

static void tp78_ble_clear_all_bonds(void)
{
    for (uint8_t slot = 0; slot < TP78_BLE_SLOT_COUNT; slot++) {
        if (tp78_ble_slot_valid(slot)) {
            (void)gap_ble_remove_white_list(&g_nv_state.slots[slot]);
        }
    }
    (void)gap_ble_remove_all_pairs();
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

static bool tp78_ble_get_slot_address(uint8_t slot, bd_addr_t *slot_address)
{
    if (slot >= TP78_BLE_SLOT_COUNT || slot_address == NULL) {
        return false;
    }
    if (!g_base_local_address_valid) {
        if (gap_ble_get_local_addr(&g_base_local_address) != ERRCODE_BT_SUCCESS) {
            osal_printk("[tp78] BLE get local address failed\r\n");
            return false;
        }
        g_base_local_address_valid = true;
    }
    *slot_address = g_base_local_address;
    /*
     * Each BLE slot must expose a stable but different local identity.  The
     * previous code replaced the last byte with ASCII '1'..'4', which can
     * collapse entropy and also makes the derived address depend on the vendor
     * factory byte.  Keep the factory address as the base and only toggle the
     * low bits that are needed to split the four slots.
     */
    slot_address->addr[BD_ADDR_LEN - 1] = (uint8_t)((slot_address->addr[BD_ADDR_LEN - 1] & 0xFCU) | slot);
    return true;
}

static bool tp78_ble_apply_slot_identity(void)
{
    uint8_t slot = g_nv_state.selected_slot;
    if (g_identity_slot == slot) {
        return true;
    }
    bd_addr_t slot_address;
    if (!tp78_ble_get_slot_address(slot, &slot_address)) {
        return false;
    }
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

static void tp78_ble_reset_pending_pair(void)
{
    g_pending_auth_valid = false;
    g_pending_peer_valid = false;
}

static void tp78_ble_reset_link_state(void)
{
    g_authenticated = false;
    g_keyboard_notify_enabled = false;
    g_mouse_notify_enabled = false;
    g_consumer_notify_enabled = false;
    g_last_notify_error = ERRCODE_BT_SUCCESS;
    g_keyboard_report_sent = false;
}

static void tp78_ble_clear_slot_ccc(uint8_t slot)
{
    if (slot >= TP78_BLE_SLOT_COUNT) {
        return;
    }
    uint8_t slot_bit = (uint8_t)(1U << slot);
    g_ccc_nv_state.keyboard_mask &= (uint8_t)~slot_bit;
    g_ccc_nv_state.mouse_mask &= (uint8_t)~slot_bit;
    g_ccc_nv_state.consumer_mask &= (uint8_t)~slot_bit;
    tp78_ble_save_ccc_state();
}

static void tp78_ble_prepare_persisted_ccc(void)
{
    /*
     * gatts_add_descriptor_sync() copies the initial CCC value into the GATT
     * database. Updating g_*_ccc after service registration does not restore
     * the stack's CCC state after reboot, so seed it before adding the service.
     * This server has one connection; the per-slot masks below still gate
     * reports after the bonded link is authenticated.
     */
    g_keyboard_ccc[0] = (g_ccc_nv_state.keyboard_mask & g_nv_state.valid_mask) != 0 ? 1 : 0;
    g_mouse_ccc[0] = (g_ccc_nv_state.mouse_mask & g_nv_state.valid_mask) != 0 ? 1 : 0;
    g_consumer_ccc[0] = (g_ccc_nv_state.consumer_mask & g_nv_state.valid_mask) != 0 ? 1 : 0;
    g_keyboard_ccc[1] = 0;
    g_mouse_ccc[1] = 0;
    g_consumer_ccc[1] = 0;
}

static void tp78_ble_restore_slot_ccc(void)
{
    uint8_t slot = g_connection_slot;
    if (!tp78_ble_slot_valid(slot)) {
        return;
    }
    uint8_t slot_bit = (uint8_t)(1U << slot);
    g_keyboard_notify_enabled = (g_ccc_nv_state.keyboard_mask & slot_bit) != 0;
    g_mouse_notify_enabled = (g_ccc_nv_state.mouse_mask & slot_bit) != 0;
    g_consumer_notify_enabled = (g_ccc_nv_state.consumer_mask & slot_bit) != 0;
    if (g_keyboard_notify_enabled) {
        osal_printk("[tp78] BLE slot %u keyboard subscription loaded\r\n",
            (unsigned int)slot + 1U);
        if (g_authenticated) {
            osal_printk("[tp78] BLE keyboard ready\r\n");
        }
    }
}

static void tp78_ble_commit_pending_pair(void)
{
    uint8_t slot = g_key_capture_slot;
    if (!g_pairing || !g_pending_auth_valid || !g_pending_peer_valid ||
        slot >= TP78_BLE_SLOT_COUNT || slot != g_nv_state.selected_slot) {
        return;
    }
    bd_addr_t own_address;
    if (!tp78_ble_get_slot_address(slot, &own_address)) {
        return;
    }
    errcode_t status = ble_set_nv_pair_keys(&g_pending_auth_info, &own_address, &g_pending_peer_address, slot);
    if (status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE slot %u key save failed:0x%x\r\n",
            (unsigned int)slot + 1U, (unsigned int)status);
        g_pairing = false;
        g_pairing_deadline = 0;
        g_key_capture_slot = TP78_BLE_INVALID_SLOT;
        tp78_ble_reset_pending_pair();
        g_authenticated = false;
        if (g_connected) {
            (void)gap_ble_disconnect_remote_device(&g_connected_address);
        }
        return;
    }

    for (uint8_t other = 0; other < TP78_BLE_SLOT_COUNT; other++) {
        if (other != slot && tp78_ble_slot_valid(other) &&
            tp78_ble_addr_equal(&g_pending_peer_address, &g_nv_state.slots[other])) {
            g_nv_state.valid_mask &= (uint8_t)~(1U << other);
        }
    }
    g_nv_state.slots[slot] = g_pending_peer_address;
    g_nv_state.valid_mask |= (uint8_t)(1U << slot);
    g_pairing = false;
    g_pairing_deadline = 0;
    g_pair_key_retry_attempted = false;
    g_key_capture_slot = TP78_BLE_INVALID_SLOT;
    tp78_ble_reset_pending_pair();
    tp78_ble_save_state();
    tp78_ble_configure_white_list();
    osal_printk("[tp78] BLE paired in slot %u\r\n", (unsigned int)slot + 1U);
}

static void tp78_ble_uuid(uint16_t value, bt_uuid_t *uuid)
{
    uuid->uuid_len = TP78_BLE_UUID_LENGTH;
    uuid->uuid[0] = (uint8_t)(value >> 8);
    uuid->uuid[1] = (uint8_t)value;
}

static errcode_t tp78_ble_add_descriptor(uint16_t service_handle, uint16_t uuid_value, uint8_t *value,
    uint16_t length, uint16_t permissions, uint16_t *value_handle)
{
    gatts_add_desc_info_t descriptor = { 0 };
    uint16_t handle;
    tp78_ble_uuid(uuid_value, &descriptor.desc_uuid);
    descriptor.permissions = permissions;
    descriptor.value = value;
    descriptor.value_len = length;
    errcode_t status = gatts_add_descriptor_sync(g_server_id, service_handle, &descriptor, &handle);
    if (status == ERRCODE_BT_SUCCESS && value_handle != NULL) {
        *value_handle = handle;
    }
    return status;
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
    uint8_t *reference, uint8_t ccc_value[2], uint16_t *value_handle, uint16_t *ccc_handle)
{
    if (tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_REPORT, GATT_ATTRIBUTE_PERMISSION_READ,
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_NOTIFY, value, length, value_handle) !=
        ERRCODE_BT_SUCCESS ||
        tp78_ble_add_descriptor(service_handle, TP78_BLE_UUID_CCC, ccc_value, 2,
        GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE, ccc_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_descriptor(service_handle, TP78_BLE_UUID_REPORT_REFERENCE, reference, 2,
        GATT_ATTRIBUTE_PERMISSION_READ, NULL) != ERRCODE_BT_SUCCESS) {
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
        g_keyboard_reference, g_keyboard_ccc, &g_keyboard_handle, &g_keyboard_ccc_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_input_report(service_handle, g_mouse_value, sizeof(g_mouse_value),
        g_mouse_reference, g_mouse_ccc, &g_mouse_handle, &g_mouse_ccc_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_input_report(service_handle, g_consumer_value, sizeof(g_consumer_value),
        g_consumer_reference, g_consumer_ccc, &g_consumer_handle, &g_consumer_ccc_handle) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_characteristic(service_handle, TP78_BLE_UUID_REPORT,
        GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE,
        GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE |
            GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP,
        g_keyboard_output, sizeof(g_keyboard_output), NULL) != ERRCODE_BT_SUCCESS ||
        tp78_ble_add_descriptor(service_handle, TP78_BLE_UUID_REPORT_REFERENCE, g_output_reference,
        sizeof(g_output_reference), GATT_ATTRIBUTE_PERMISSION_READ, NULL) != ERRCODE_BT_SUCCESS ||
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
        errcode_t status = gap_ble_stop_adv(TP78_BLE_ADV_HANDLE);
        if (status != ERRCODE_BT_SUCCESS) {
            osal_printk("[tp78] BLE stop advertising failed:0x%x\r\n", (unsigned int)status);
        }
    }
}

static void tp78_ble_start_advertising(void)
{
    if (!g_active || g_connected || !g_service_initialized) {
        return;
    }
    if (g_advertising) {
        g_adv_restart = true;
        errcode_t status = gap_ble_stop_adv(TP78_BLE_ADV_HANDLE);
        if (status != ERRCODE_BT_SUCCESS) {
            g_adv_restart = false;
            osal_printk("[tp78] BLE restart advertising failed:0x%x\r\n", (unsigned int)status);
        }
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
        3, TP78_BLE_ADV_COMPLETE_UUID16, (uint8_t)TP78_BLE_UUID_HID_SERVICE,
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
    errcode_t status = gap_ble_set_adv_data(TP78_BLE_ADV_HANDLE, &data);
    if (status == ERRCODE_BT_SUCCESS) {
        status = gap_ble_set_adv_param(TP78_BLE_ADV_HANDLE, &parameters);
    }
    if (status == ERRCODE_BT_SUCCESS) {
        status = gap_ble_start_adv(TP78_BLE_ADV_HANDLE);
    }
    if (status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE start advertising failed:0x%x\r\n", (unsigned int)status);
    }
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
        tp78_ble_reset_link_state();
        g_connection_slot = g_nv_state.selected_slot;
        osal_printk("[tp78] BLE connected, pair_state:%u, pairing:%u\r\n",
            (unsigned int)pair_state, g_pairing ? 1U : 0U);
        g_advertising = false;
        g_connected_address = *address;
        if (g_pairing && pair_state == GAP_BLE_PAIR_PAIRED) {
            osal_printk("[tp78] BLE ignored old bonded host during pairing\r\n");
            (void)gap_ble_disconnect_remote_device(address);
        } else if (g_pairing) {
            errcode_t security_status = gap_ble_pair_remote_device(address);
            osal_printk("[tp78] BLE pairing requested, status:0x%x\r\n",
                (unsigned int)security_status);
            if (security_status != ERRCODE_BT_SUCCESS) {
                (void)gap_ble_disconnect_remote_device(address);
            }
        } else if (pair_state == GAP_BLE_PAIR_PAIRED) {
            g_authenticated = true;
            tp78_ble_restore_slot_ccc();
            osal_printk("[tp78] BLE bonded link restored\r\n");
        } else if (tp78_ble_slot_valid(g_nv_state.selected_slot)) {
            errcode_t security_status = gap_ble_pair_remote_device(address);
            osal_printk("[tp78] BLE bonded key restore requested, status:0x%x\r\n",
                (unsigned int)security_status);
            if (security_status != ERRCODE_BT_SUCCESS) {
                (void)gap_ble_disconnect_remote_device(address);
            }
        } else {
            osal_printk("[tp78] BLE rejected unbonded connection\r\n");
            (void)gap_ble_disconnect_remote_device(address);
        }
    } else {
        tp78_ble_reset_link_state();
        tp78_ble_reset_pending_pair();
        g_connection_slot = TP78_BLE_INVALID_SLOT;
        g_slot_switch_pending = false;
        if (g_active) {
            osal_printk("[tp78] BLE disconnected, reason:%u, pairing:%u\r\n",
                (unsigned int)reason, g_pairing ? 1U : 0U);
            tp78_ble_start_advertising();
        }
    }
}

static void tp78_ble_auth_complete(uint16_t conn_id, const bd_addr_t *address, errcode_t status,
    const ble_auth_info_evt_t *event)
{
    unused(address);
    if (!g_connected || conn_id != g_connection_id) {
        return;
    }
    g_authenticated = status == ERRCODE_BT_SUCCESS;
    if (g_authenticated && !g_pairing) {
        tp78_ble_restore_slot_ccc();
    }
    if (g_pairing && status == ERRCODE_BT_SUCCESS && event != NULL &&
        g_key_capture_slot < TP78_BLE_SLOT_COUNT) {
        g_pending_auth_info = *event;
        g_pending_auth_valid = true;
        tp78_ble_commit_pending_pair();
    }
    osal_printk("[tp78] BLE authentication %s, status:0x%x\r\n",
        g_authenticated ? "ready" : "failed", (unsigned int)status);
}

static void tp78_ble_write_request(uint8_t server_id, uint16_t conn_id, gatts_req_write_cb_t *request,
    errcode_t status)
{
    unused(server_id);
    if (request == NULL || conn_id != g_connection_id || status != ERRCODE_BT_SUCCESS ||
        request->offset != 0 || request->length != 2 || request->value == NULL) {
        return;
    }
    bool enabled = (request->value[0] & 0x01U) != 0;
    if (g_connection_slot >= TP78_BLE_SLOT_COUNT) {
        return;
    }
    uint8_t slot_bit = (uint8_t)(1U << g_connection_slot);
    if (request->handle == g_keyboard_ccc_handle) {
        g_keyboard_notify_enabled = enabled;
        g_keyboard_ccc[0] = enabled ? 1 : 0;
        if (enabled) {
            g_ccc_nv_state.keyboard_mask |= slot_bit;
        } else {
            g_ccc_nv_state.keyboard_mask &= (uint8_t)~slot_bit;
        }
        tp78_ble_save_ccc_state();
        osal_printk("[tp78] BLE keyboard notifications %s\r\n", enabled ? "enabled" : "disabled");
        if (enabled && g_authenticated) {
            osal_printk("[tp78] BLE keyboard ready\r\n");
        }
    } else if (request->handle == g_mouse_ccc_handle) {
        g_mouse_notify_enabled = enabled;
        g_mouse_ccc[0] = enabled ? 1 : 0;
        if (enabled) {
            g_ccc_nv_state.mouse_mask |= slot_bit;
        } else {
            g_ccc_nv_state.mouse_mask &= (uint8_t)~slot_bit;
        }
        tp78_ble_save_ccc_state();
    } else if (request->handle == g_consumer_ccc_handle) {
        g_consumer_notify_enabled = enabled;
        g_consumer_ccc[0] = enabled ? 1 : 0;
        if (enabled) {
            g_ccc_nv_state.consumer_mask |= slot_bit;
        } else {
            g_ccc_nv_state.consumer_mask &= (uint8_t)~slot_bit;
        }
        tp78_ble_save_ccc_state();
    }
}

static void tp78_ble_pair_complete(uint16_t conn_id, const bd_addr_t *address, errcode_t status)
{
    if (address == NULL) {
        return;
    }
    if (status == ERRCODE_BT_KEY_MISSING) {
        errcode_t remove_status = gap_ble_remove_pair(address);
        osal_printk("[tp78] BLE stale key removed, status:0x%x\r\n", (unsigned int)remove_status);
        tp78_ble_reset_pending_pair();
        g_authenticated = false;
        if (g_pairing) {
            if (!g_pair_key_retry_attempted && g_connected && conn_id == g_connection_id) {
                g_pair_key_retry_attempted = true;
                errcode_t retry_status = gap_ble_pair_remote_device(address);
                osal_printk("[tp78] BLE fresh pairing retry, status:0x%x\r\n",
                    (unsigned int)retry_status);
                if (retry_status == ERRCODE_BT_SUCCESS) {
                    return;
                }
            }
            g_pairing = false;
            g_pairing_deadline = 0;
            g_key_capture_slot = TP78_BLE_INVALID_SLOT;
            osal_printk("[tp78] BLE host still uses an old key; remove the device on host and pair again\r\n");
            (void)gap_ble_disconnect_remote_device(address);
            return;
        }
        g_key_capture_slot = TP78_BLE_INVALID_SLOT;
        uint8_t slot = g_nv_state.selected_slot;
        if (tp78_ble_slot_valid(slot)) {
            g_nv_state.valid_mask &= (uint8_t)~(1U << slot);
            tp78_ble_clear_slot_ccc(slot);
            tp78_ble_save_state();
            osal_printk("[tp78] BLE slot %u key missing; hold Fn+F11 to pair again\r\n",
                (unsigned int)slot + 1U);
        }
        (void)gap_ble_disconnect_remote_device(address);
        return;
    }
    if (!g_pairing) {
        if (status == ERRCODE_BT_SUCCESS && g_connected && conn_id == g_connection_id) {
            g_authenticated = true;
            tp78_ble_restore_slot_ccc();
            osal_printk("[tp78] BLE bonded link security ready\r\n");
        } else if (status != ERRCODE_BT_SUCCESS) {
            osal_printk("[tp78] BLE security failed, status:0x%x\r\n", (unsigned int)status);
        }
        return;
    }
    if (status != ERRCODE_BT_SUCCESS) {
        tp78_ble_reset_pending_pair();
        g_authenticated = false;
        osal_printk("[tp78] BLE pairing failed, status:0x%x\r\n", (unsigned int)status);
        if (g_connected && conn_id == g_connection_id) {
            (void)gap_ble_disconnect_remote_device(address);
        }
        return;
    }
    uint8_t slot = g_key_capture_slot;
    if (!g_connected || conn_id != g_connection_id ||
        slot >= TP78_BLE_SLOT_COUNT || slot != g_nv_state.selected_slot) {
        osal_printk("[tp78] BLE ignored stale pairing result\r\n");
        return;
    }
    g_pending_peer_address = *address;
    g_pending_peer_valid = true;
    tp78_ble_commit_pending_pair();
}

static void tp78_ble_enabled(uint8_t status)
{
    if (status != BT_ENABLE_DISABLE_SUCCESS) {
        osal_printk("[tp78] BLE enable failed, status:%u\r\n", (unsigned int)status);
        return;
    }
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
    errcode_t key_mode_status = gap_ble_set_save_smp_keys_mode(GAP_BLE_SAVE_SMP_KEYS_MANU);
    if (key_mode_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE manual key mode failed:0x%x\r\n", (unsigned int)key_mode_status);
        g_service_initialized = false;
        return;
    }
    errcode_t pair_info_status = gap_ble_set_pair_info_available(GAP_BLE_PAIR_INFO_AVAILABLE);
    if (pair_info_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE pair info config failed:0x%x\r\n", (unsigned int)pair_info_status);
        g_service_initialized = false;
        return;
    }
    errcode_t security_status = gap_ble_set_sec_param(&security);
    if (security_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE security config failed:0x%x\r\n", (unsigned int)security_status);
        g_service_initialized = false;
        return;
    }
    tp78_ble_prepare_persisted_ccc();
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
        g_ccc_nv_state.keyboard_mask = 0;
        g_ccc_nv_state.mouse_mask = 0;
        g_ccc_nv_state.consumer_mask = 0;
        tp78_ble_save_ccc_state();
        tp78_ble_save_state();
        g_nv_needs_reset = false;
        osal_printk("[tp78] BLE legacy bonds cleared; pair each slot again\r\n");
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

    /*
     * Phones commonly advertise a public identity address but reconnect with
     * a resolvable private address (RPA). The default SDK policy only adds
     * bonds whose identity address is random to the resolving list, so a
     * phone with a public identity can no longer pass the whitelist after a
     * keyboard reboot. Computers usually reconnect with their stable address
     * and therefore do not expose this bug.
     *
     * Load every bonded peer with an IRK into the controller resolving list.
     * The advertising whitelist can then match the resolved identity while
     * still restricting each keyboard slot to its bonded host.
     */
    errcode_t ral_status = ble_set_feature(BLE_FEATURE_ADD_RAL_POLICY, BLE_FEATURE_ADD_RAL_EVERY_ADDR);
    if (ral_status != ERRCODE_BT_SUCCESS) {
        osal_printk("[tp78] BLE resolving-list policy failed:0x%x\r\n", (unsigned int)ral_status);
        return -1;
    }

    gatt_callbacks.write_request_cb = tp78_ble_write_request;
    gap_callbacks.conn_state_change_cb = tp78_ble_connection_changed;
    gap_callbacks.pair_result_cb = tp78_ble_pair_complete;
    gap_callbacks.auth_complete_cb = tp78_ble_auth_complete;
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
    return g_connected && g_authenticated && g_keyboard_notify_enabled && !g_slot_switch_pending &&
        g_keyboard_handle != TP78_BLE_INVALID_HANDLE;
}

void tp78_ble_keyboard_set_active(bool active)
{
    g_active = active;
    g_pairing = false;
    g_pairing_deadline = 0;
    g_pair_key_retry_attempted = false;
    tp78_ble_reset_pending_pair();
    g_key_capture_slot = TP78_BLE_INVALID_SLOT;
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
        g_pair_key_retry_attempted = false;
        tp78_ble_reset_pending_pair();
        g_key_capture_slot = TP78_BLE_INVALID_SLOT;
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
    g_pair_key_retry_attempted = false;
    tp78_ble_reset_pending_pair();
    g_key_capture_slot = TP78_BLE_INVALID_SLOT;
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
    tp78_ble_clear_slot_ccc(slot);
    tp78_ble_reset_pending_pair();
    g_pair_key_retry_attempted = false;
    g_key_capture_slot = slot;
    g_pairing = true;
    g_pairing_deadline = uapi_tcxo_get_ms() + TP78_BLE_PAIRING_TIMEOUT_MS;
    tp78_ble_stop_advertising();
    osal_printk("[tp78] BLE slot %u pairing for %u seconds\r\n",
        (unsigned int)slot + 1U, (unsigned int)(TP78_BLE_PAIRING_TIMEOUT_MS / 1000U));
    if (g_connected) {
        (void)gap_ble_disconnect_remote_device(&g_connected_address);
    } else {
        tp78_ble_start_advertising();
    }
}

static int32_t tp78_ble_send(uint16_t handle, const uint8_t *data, uint16_t length)
{
    if (!g_connected || !g_authenticated || handle == TP78_BLE_INVALID_HANDLE || data == NULL) {
        return -1;
    }
    gatts_ntf_ind_t notification = {
        .attr_handle = handle,
        .value_len = length,
        .value = (uint8_t *)data,
    };
    errcode_t status = gatts_notify_indicate(g_server_id, g_connection_id, &notification);
    if (status != ERRCODE_BT_SUCCESS) {
        if (status != g_last_notify_error) {
            osal_printk("[tp78] BLE report notify failed, handle:%u status:0x%x\r\n",
                (unsigned int)handle, (unsigned int)status);
            g_last_notify_error = status;
        }
        return -1;
    }
    g_last_notify_error = ERRCODE_BT_SUCCESS;
    return 0;
}

int32_t tp78_ble_keyboard_send(uint8_t modifiers, const uint8_t keys[6])
{
    if (!g_keyboard_notify_enabled) {
        return -1;
    }
    uint8_t report[8] = {
        modifiers, 0, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]
    };
    int32_t status = tp78_ble_send(g_keyboard_handle, report, sizeof(report));
    if (status == 0 && !g_keyboard_report_sent) {
        g_keyboard_report_sent = true;
        osal_printk("[tp78] BLE first keyboard report sent\r\n");
    }
    return status;
}

int32_t tp78_ble_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    if (!g_mouse_notify_enabled) {
        return -1;
    }
    uint8_t report[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel };
    return tp78_ble_send(g_mouse_handle, report, sizeof(report));
}

int32_t tp78_ble_consumer_send(uint16_t usage)
{
    if (!g_consumer_notify_enabled) {
        return -1;
    }
    uint8_t report[2] = { (uint8_t)usage, (uint8_t)(usage >> 8) };
    return tp78_ble_send(g_consumer_handle, report, sizeof(report));
}
