#ifndef TP78_I2C_H
#define TP78_I2C_H

#include <stdbool.h>
#include <stdint.h>

#define TP78_I2C_EXPECTED_DEVICE_COUNT 3
#define TP78_I2C_ADDR_TRACKPOINT 0x15
#define TP78_I2C_ADDR_OLED 0x3C
#define TP78_I2C_ADDR_MPR121 0x5A

typedef enum {
    TP78_I2C_DEVICE_UNKNOWN = 0,
    TP78_I2C_DEVICE_TRACKPOINT,
    TP78_I2C_DEVICE_OLED,
    TP78_I2C_DEVICE_MPR121,
} tp78_i2c_device_type_t;

typedef struct {
    uint8_t address;
    tp78_i2c_device_type_t type;
} tp78_i2c_device_t;

int32_t tp78_i2c_init(void);
int32_t tp78_i2c_reset(void);
uint8_t tp78_i2c_get_device_count(void);
uint8_t tp78_i2c_copy_devices(tp78_i2c_device_t *devices, uint8_t max_devices);
bool tp78_i2c_device_present(uint8_t address);
bool tp78_i2c_has_device(tp78_i2c_device_type_t type);
bool tp78_i2c_trackpoint_irq_active(void);

int32_t tp78_i2c_write(uint8_t address, const uint8_t *data, uint32_t length);
int32_t tp78_i2c_read(uint8_t address, uint8_t *data, uint32_t length);
int32_t tp78_i2c_write_read(uint8_t address, const uint8_t *tx_data, uint32_t tx_length,
    uint8_t *rx_data, uint32_t rx_length);

#endif
