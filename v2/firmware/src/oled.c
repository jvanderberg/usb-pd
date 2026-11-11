/* 0.69in 96x16 SSD1306/SSD1315 panel on I2C 0x3C, internal charge pump.
 * Init values from the N069-9616TSWPG02-H14 datasheet s4.4.2 (VCC from the internal DC/DC): multiplex 0x0F
 * (1/16 duty), COM pins 0x02, segment remap 0xA1 + COM scan 0xC8, charge pump 0x8D 0x14. The glass shows
 * SEG127..SEG32 as columns 1..96, so with remap A1 the visible RAM columns are 0..95. */
#include "oled.h"
#include "hw.h"

#define OLED_ADDR 0x3C
#define W 96

static uint8_t __xdata fb[W * 2];     /* what the next flush should show */
static uint8_t __xdata sent[W * 2];   /* what the panel's RAM holds (as far as we know) */
static uint8_t resync;                /* 1: the panel's RAM is unknown, send everything */

static const uint8_t __code init_seq[] = {
    0xAE,             /* display off */
    0xD5, 0x80,       /* clock */
    0xA8, 0x0F,       /* multiplex 16 */
    0xD3, 0x00,       /* offset */
    0x40,             /* start line */
    0x8D, 0x14,       /* charge pump on (internal) */
    0x20, 0x00,       /* horizontal addressing */
    0xA1,             /* segment remap */
    0xC8,             /* COM scan reversed */
    0xDA, 0x02,       /* COM pins: sequential */
    0x81, 0x4F,       /* contrast */
    0xD9, 0x1F,       /* pre-charge (datasheet s4.4.2, internal DC/DC) */
    0xDB, 0x40,       /* VCOMH */
    0xA4, 0xA6,       /* RAM display, normal */
    0xAF              /* display on */
};

/* 5x7 glyphs for the characters the UI uses; column-major, LSB = top */
static const uint8_t __code glyphs[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},  /* '0'..'9' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x00,0x00,0x00,0x00,0x00}  /* unknown */
};
static const char __code glyph_chars[] = " .->0123456789ACDEFHILMNOPRSTUVWX";

static uint8_t glyph_index(char c)
{
    uint8_t i;
    for (i = 0; glyph_chars[i]; i++)
        if (glyph_chars[i] == c)
            return i;
    return (uint8_t)(sizeof(glyph_chars) - 1);
}

uint8_t oled_init(void)
{
    uint8_t i, e;
    e = i2c_write_code(OLED_ADDR, 0x00, init_seq, sizeof(init_seq));
    for (i = 0; i < sizeof(fb); i++)
        fb[i] = 0;
    resync = 1;                       /* panel RAM is random at power-up: the first flush clears all of it */
    if (!e) e = oled_flush();
    return e;
}

void oled_text(uint8_t row, uint8_t col, const char *s)
{
    /* row 0..1 (8 px each), col in 6 px cells (16 per row) */
    uint8_t __xdata *p = fb + row * W + col * 6;
    uint8_t g, k;
    while (*s && col < 16) {
        g = glyph_index(*s++);
        for (k = 0; k < 5; k++)
            *p++ = glyphs[g][k];
        *p++ = 0;
        col++;
    }
}

void oled_clear(void)
{
    uint8_t i;
    for (i = 0; i < sizeof(fb); i++)
        fb[i] = 0;
}

/* A changed span costs its bytes plus ~10 bytes of window setup (two I2C transactions), so two spans closer than
   GAP_MERGE unchanged bytes go out as one. */
#define GAP_MERGE 10

static uint8_t send_span(uint8_t page, uint8_t c0, uint8_t c1)
{
    static uint8_t __xdata win[6];
    uint8_t e, i;
    win[0] = 0x21; win[1] = c0; win[2] = c1;       /* column window (horizontal addressing mode) */
    win[3] = 0x22; win[4] = page; win[5] = page;   /* one page */
    e = i2c_write_buf(OLED_ADDR, 0x00, win, 6);
    if (!e) e = i2c_write_buf(OLED_ADDR, 0x40, fb + page * W + c0, (uint8_t)(c1 - c0 + 1));
    if (!e)
        for (i = c0; ; i++) {
            sent[page * W + i] = fb[page * W + i];
            if (i == c1) break;
        }
    return e;
}

uint8_t oled_flush(void)
{
    /* send only the bytes that differ from what the panel already shows */
    uint8_t page, c, start, last, e = 0;
    uint8_t __xdata *f, *s;
    for (page = 0; page < 2 && !e; page++) {
        f = fb + page * W;
        s = sent + page * W;
        start = 0xFF;
        last = 0;
        for (c = 0; c < W; c++) {
            if (!resync && f[c] == s[c])
                continue;
            if (start != 0xFF && (uint8_t)(c - last) > GAP_MERGE) {
                e = send_span(page, start, last);
                if (e) break;
                start = 0xFF;
            }
            if (start == 0xFF) start = c;
            last = c;
        }
        if (!e && start != 0xFF)
            e = send_span(page, start, last);
    }
    resync = e ? 1 : 0;               /* after a failed transfer the panel's RAM is unknown: resend it all next time */
    return e;
}

void oled_resync(void) { resync = 1; }
