#ifndef TP78_BOARD_H
#define TP78_BOARD_H

#include "platform_core.h"

#define TP78_MATRIX_ROWS 6
#define TP78_MATRIX_COLS 14

/* BS21E GPIO numbers, ordered by the unchanged M.2 gold-finger signals. */
extern const pin_t g_tp78_row_pins[TP78_MATRIX_ROWS];
extern const pin_t g_tp78_col_pins[TP78_MATRIX_COLS];

#define TP78_I2C_SCL_PIN S_MGPIO6
#define TP78_I2C_SDA_PIN S_MGPIO9
#define TP78_TRACKPOINT_INT_PIN S_MGPIO23
#define TP78_BATTERY_ADC_PIN S_MGPIO31
#define TP78_BATTERY_CHARGE_PIN S_MGPIO24
#define TP78_MOTOR_PIN S_MGPIO10
#define TP78_WS2812_PIN S_MGPIO5

#endif
