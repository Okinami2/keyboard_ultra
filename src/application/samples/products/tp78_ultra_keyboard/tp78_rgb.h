#ifndef TP78_RGB_H
#define TP78_RGB_H

#include <stdint.h>

typedef enum {
    TP78_RGB_EFFECT_OFF = 0,
    TP78_RGB_EFFECT_STATIC,
    TP78_RGB_EFFECT_BREATH,
    TP78_RGB_EFFECT_WATERFALL,
    TP78_RGB_EFFECT_REACTIVE,
    TP78_RGB_EFFECT_RAINBOW,
    TP78_RGB_EFFECT_COUNT
} tp78_rgb_effect_t;

void tp78_rgb_init(void);
void tp78_rgb_key_event(uint8_t row, uint8_t col);
void tp78_rgb_set_effect(tp78_rgb_effect_t effect);
void tp78_rgb_adjust_brightness(int8_t direction);
void tp78_rgb_adjust_speed(int8_t direction);

#endif
