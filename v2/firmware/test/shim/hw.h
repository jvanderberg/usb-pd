/* host stand-in for src/hw.h: lets src/oled.c run against a model of the panel (test/test_oled.c) */
#ifndef HW_H
#define HW_H
#include <stdint.h>
#define __xdata
#define __code
uint8_t i2c_write_buf(uint8_t addr7, uint8_t ctrl, const uint8_t *buf, uint8_t n);
uint8_t i2c_write_code(uint8_t addr7, uint8_t ctrl, const uint8_t *buf, uint8_t n);
#endif
