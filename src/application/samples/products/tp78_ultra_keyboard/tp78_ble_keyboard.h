#ifndef TP78_BLE_KEYBOARD_H
#define TP78_BLE_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

int32_t tp78_ble_keyboard_init(void);
bool tp78_ble_keyboard_is_ready(void);
void tp78_ble_keyboard_set_active(bool active);
void tp78_ble_keyboard_process(void);
void tp78_ble_keyboard_select_slot(uint8_t slot);
uint8_t tp78_ble_keyboard_get_slot(void);
void tp78_ble_keyboard_start_pairing(void);
int32_t tp78_ble_keyboard_send(uint8_t modifiers, const uint8_t keys[6]);
int32_t tp78_ble_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
int32_t tp78_ble_consumer_send(uint16_t usage);

#endif
