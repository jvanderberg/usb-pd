#ifndef OLED_H
#define OLED_H
#include <stdint.h>
uint8_t oled_init(void);                               /* 0 = acknowledged */
void oled_clear(void);
void oled_text(uint8_t row, uint8_t col, const char *s);
uint8_t oled_flush(void);                              /* sends only what changed since the last flush */
void oled_resync(void);                                /* next flush sends the whole screen */
#endif
