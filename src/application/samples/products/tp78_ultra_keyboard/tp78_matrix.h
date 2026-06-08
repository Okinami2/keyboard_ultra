#ifndef TP78_MATRIX_H
#define TP78_MATRIX_H

#include <stdbool.h>
#include <stdint.h>
#include "tp78_board.h"

void tp78_matrix_init(void);
bool tp78_matrix_scan(void);
bool tp78_matrix_is_pressed(uint8_t row, uint8_t col);

#endif
