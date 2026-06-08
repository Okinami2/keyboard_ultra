#include <string.h>
#include "gpio.h"
#include "pinctrl.h"
#include "tcxo.h"
#include "tp78_matrix.h"

#define TP78_DEBOUNCE_SCANS 5

static bool g_matrix[TP78_MATRIX_ROWS][TP78_MATRIX_COLS];
static uint8_t g_debounce[TP78_MATRIX_ROWS][TP78_MATRIX_COLS];

void tp78_matrix_init(void)
{
    uapi_gpio_init();
    uapi_pin_init();

    for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
        (void)uapi_pin_set_mode(g_tp78_row_pins[row], HAL_PIO_FUNC_GPIO);
        (void)uapi_pin_set_pull(g_tp78_row_pins[row], PIN_PULL_NONE);
        (void)uapi_gpio_set_dir(g_tp78_row_pins[row], GPIO_DIRECTION_OUTPUT);
        (void)uapi_gpio_set_val(g_tp78_row_pins[row], GPIO_LEVEL_LOW);
    }

    for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
        (void)uapi_pin_set_mode(g_tp78_col_pins[col], HAL_PIO_FUNC_GPIO);
        (void)uapi_pin_set_pull(g_tp78_col_pins[col], PIN_PULL_DOWN);
        (void)uapi_gpio_set_dir(g_tp78_col_pins[col], GPIO_DIRECTION_INPUT);
    }

    (void)memset(g_matrix, 0, sizeof(g_matrix));
    (void)memset(g_debounce, 0, sizeof(g_debounce));
}

bool tp78_matrix_scan(void)
{
    bool changed = false;

    for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
        (void)uapi_gpio_set_val(g_tp78_row_pins[row], GPIO_LEVEL_HIGH);
        (void)uapi_tcxo_delay_us(3);

        for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
            bool sample = uapi_gpio_get_val(g_tp78_col_pins[col]) == GPIO_LEVEL_HIGH;
            if (sample == g_matrix[row][col]) {
                g_debounce[row][col] = 0;
            } else if (++g_debounce[row][col] >= TP78_DEBOUNCE_SCANS) {
                g_matrix[row][col] = sample;
                g_debounce[row][col] = 0;
                changed = true;
            }
        }

        (void)uapi_gpio_set_val(g_tp78_row_pins[row], GPIO_LEVEL_LOW);
    }

    return changed;
}

bool tp78_matrix_is_pressed(uint8_t row, uint8_t col)
{
    return row < TP78_MATRIX_ROWS && col < TP78_MATRIX_COLS && g_matrix[row][col];
}
