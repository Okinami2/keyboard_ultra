#ifndef TP78_OLED_ASSETS_H
#define TP78_OLED_ASSETS_H

#include <stdint.h>

typedef struct {
    const uint8_t *data;
    uint16_t length;
} tp78_oled_frame_t;

extern const tp78_oled_frame_t g_tp78_oled_frames[];
extern const uint8_t g_tp78_oled_frame_count;

#endif
