#ifndef TP78_OLED_H
#define TP78_OLED_H

#include <stdint.h>

int32_t tp78_oled_init(void);
int32_t tp78_oled_clear(void);
int32_t tp78_oled_show_art(uint8_t index);
int32_t tp78_oled_show_next_art(void);
int32_t tp78_oled_debug_all_on(void);
int32_t tp78_oled_debug_cursor(void);
int32_t tp78_oled_debug_write_data(uint8_t length);
uint8_t tp78_oled_get_art_count(void);

#endif
