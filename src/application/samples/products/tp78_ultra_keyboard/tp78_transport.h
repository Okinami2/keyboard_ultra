#ifndef TP78_TRANSPORT_H
#define TP78_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TP78_TRANSPORT_USB = 0,
    TP78_TRANSPORT_BLE,
    TP78_TRANSPORT_SLE,
} tp78_transport_mode_t;

int32_t tp78_transport_init(void);
tp78_transport_mode_t tp78_transport_get_mode(void);
bool tp78_transport_is_ready(void);
void tp78_transport_set_mode(tp78_transport_mode_t mode);
int32_t tp78_transport_keyboard_send(uint8_t modifiers, const uint8_t keys[6]);
int32_t tp78_transport_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
int32_t tp78_transport_consumer_send(uint16_t usage);

#endif
