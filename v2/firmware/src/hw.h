#ifndef HW_H
#define HW_H
#include <stdint.h>
#include "ch552.h"

#define FSYS_HZ 16000000UL   /* 16 MHz: allowed down to VCC 3.8 V (CH552 DS s23 Fsys rows, table 8.2.2). 24 MHz needs
                                VCC >= 4.5 V, and at a 5 V contract VCC is VBUS minus the SGM2203 dropout, ~4.4-4.6 V */

extern volatile uint16_t ms_ticks;
uint16_t ticks(void);               /* atomic read of ms_ticks (16 bits on an 8-bit CPU: never read it directly) */
#define BTN_EV_ON  0x01
#define BTN_EV_CYC 0x02
#define BTN_EV_UP  0x04
#define BTN_EV_DN  0x08
uint8_t buttons_take(void);         /* press events latched by the 1 ms interrupt (10 ms debounce); clears them */
uint8_t buttons_held(void);         /* debounced state, same bits */

void hw_init(void);                 /* clock, pins, 1 ms timer, watchdog */
void delay_ms(uint16_t ms);
#define WDOG_FEED() (WDOG_COUNT = 0)   /* 1.05 s at 16 MHz to the next feed or a reset */

uint8_t df_read(uint8_t off);       /* DataFlash byte 0..127 */
void df_write(uint8_t off, uint8_t v);

void jump_to_bootloader(void);      /* CH552 ROM ISP at 0x3800 */

/* bus A: bit-banged I2C on P1.6 / P1.7 (open drain, external 2.2k pull-ups to 3.3 V) - the OLED. 0 = ACK */
uint8_t i2c_write_buf(uint8_t addr7, uint8_t ctrl, const uint8_t __xdata *buf, uint8_t n);
uint8_t i2c_write_code(uint8_t addr7, uint8_t ctrl, const uint8_t __code *buf, uint8_t n);

/* bus B: bit-banged I2C on P1.0 SCL5 / P3.2 SDA5 (open drain, external 4.7k pull-ups to 5 V) - the AP33772S at
   0x52 only. Multi-byte registers are little endian (AP33772S DS "I2C Command Format"). 0 = ACK */
#define AP_ADDR 0x52
uint8_t ap_write(uint8_t reg, const uint8_t __xdata *buf, uint8_t n);
uint8_t ap_read(uint8_t reg, uint8_t __xdata *buf, uint8_t n);
#endif
