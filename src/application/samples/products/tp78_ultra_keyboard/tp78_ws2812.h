#ifndef TP78_WS2812_H
#define TP78_WS2812_H

#include <stdint.h>

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} tp78_rgb_color_t;

void tp78_ws2812_init(void);
void tp78_ws2812_write(const tp78_rgb_color_t *pixels, uint16_t count);

#endif
