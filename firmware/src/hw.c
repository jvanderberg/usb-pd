#include "hw.h"

volatile uint16_t ms_ticks;
static volatile uint8_t btn_state, btn_events;
static uint8_t btn_cnt[4];

/* Timer0, mode 1, Fsys/12 = 1.333 MHz: 1333 counts per ms (0.025 % fast, which nothing here notices) */
#define T0_RELOAD (65536u - (uint16_t)(FSYS_HZ / 12u / 1000u))

void timer0_isr(void) __interrupt(1)
{
    uint8_t raw, i, bit;
    TH0 = (uint8_t)(T0_RELOAD >> 8);
    TL0 = (uint8_t)T0_RELOAD;
    ms_ticks++;
    /* buttons: a level must hold 10 consecutive ms to count; a new press latches an event for the main loop.
       ON, UP, DN pull to GND; SEL (CYCLE) pulls to 3V3 against R3 */
    raw = 0;
    if (!BTN_ON) raw |= BTN_EV_ON;
    if (BTN_CYC) raw |= BTN_EV_CYC;
    if (!BTN_UP) raw |= BTN_EV_UP;
    if (!BTN_DN) raw |= BTN_EV_DN;
    for (i = 0, bit = 1; i < 4; i++, bit <<= 1) {
        if ((uint8_t)(raw & bit) != (uint8_t)(btn_state & bit)) {
            if (++btn_cnt[i] >= 10) {
                btn_cnt[i] = 0;
                btn_state ^= bit;
                if (btn_state & bit) btn_events |= bit;
            }
        } else {
            btn_cnt[i] = 0;
        }
    }
}

uint16_t ticks(void)
{
    uint16_t t;
    ET0 = 0;
    t = ms_ticks;
    ET0 = 1;
    return t;
}

uint8_t buttons_take(void)
{
    uint8_t e;
    ET0 = 0;
    e = btn_events;
    btn_events = 0;
    ET0 = 1;
    return e;
}

uint8_t buttons_held(void) { return btn_state; }

void hw_init(void)
{
    /* I2C bus B (P1.0 SCL5, P3.2 SDA5) and bus A (P1.6 SDA, P1.7 SCL): open drain, NO internal pull-up, released
       (latch 1). Bus B pulls up to 5 V, bus A to 3.3 V (the OLED allows VDD + 0.3 V): never push-pull. Both are
       already high-ish at reset (quasi-bidirectional, weak pull-up), which is an idle bus */
    SCL5 = 1;
    SDA5 = 1;
    I2C_SDA = 1;
    I2C_SCL = 1;
    P1_MOD_OC |= 0xC1;
    P1_DIR_PU &= (uint8_t)~0xC1;
    P3_MOD_OC |= 0x04;
    P3_DIR_PU &= (uint8_t)~0x04;

    /* P1.1 ON, P3.3 UP, P3.4 DN to GND: open drain + pull-up (latch stays 1, so they are only ever inputs) */
    BTN_ON = 1;
    BTN_UP = 1;
    BTN_DN = 1;
    P1_MOD_OC |= 0x02;
    P1_DIR_PU |= 0x02;
    P3_MOD_OC |= 0x18;
    P3_DIR_PU |= 0x18;
    /* P1.4 SEL (CYCLE) to 3.3 V with R3 10k pull-down: high-impedance input (an internal pull-up to 5 V would feed
       the 3.3 V rail through the button and fight R3) */
    P1_MOD_OC &= (uint8_t)~0x10;
    P1_DIR_PU &= (uint8_t)~0x10;

    /* system clock 16 MHz (MASK_SYS_CK_SEL = 101b). bROM_CLK_FAST stays 0: it follows Fosc (24 MHz internal) */
    SAFE_ENTER();
    CLOCK_CFG = (uint8_t)((CLOCK_CFG & ~0x07) | 0x05);
    SAFE_EXIT();

    TMOD = (uint8_t)((TMOD & 0xF0) | 0x01);
    TH0 = (uint8_t)(T0_RELOAD >> 8);
    TL0 = (uint8_t)T0_RELOAD;
    ET0 = 1;
    TR0 = 1;
    EA = 1;

    /* watchdog reset: a hung loop must not leave the output on without its thermal checks (after a reset the
       firmware forces the output off again first thing) */
    WDOG_COUNT = 0;
    SAFE_ENTER();
    GLOBAL_CFG |= 0x01;         /* bWDOG_EN */
    SAFE_EXIT();
}

void delay_ms(uint16_t ms)
{
    uint16_t start = ticks();
    while ((uint16_t)(ticks() - start) < ms)
        ;
}

uint8_t df_read(uint8_t off)
{
    ROM_ADDR_H = 0xC0;                 /* DataFlash C000h, even addresses only (DS s6.2 / s6.5.3) */
    ROM_ADDR_L = (uint8_t)(off << 1);
    ROM_CTRL = 0x8E;
    return ROM_DATA_L;
}

void df_write(uint8_t off, uint8_t v)
{
    uint8_t ea = EA;
    EA = 0;                            /* keep the 13-23 cycle safe-mode window */
    SAFE_ENTER();
    GLOBAL_CFG |= 0x04;                /* bDATA_WE */
    SAFE_EXIT();
    ROM_ADDR_H = 0xC0;
    ROM_ADDR_L = (uint8_t)(off << 1);
    ROM_DATA_L = v;
    ROM_CTRL = 0x9A;                   /* write; the CPU pauses until done */
    SAFE_ENTER();
    GLOBAL_CFG &= (uint8_t)~0x0C;      /* write protect again */
    SAFE_EXIT();
    EA = ea;
}

void jump_to_bootloader(void)
{
    /* the caller has already forced the AP33772S output off (SYSTEM = 0x11): it stays off while we flash */
    EA = 0;
    SAFE_ENTER();
    GLOBAL_CFG &= (uint8_t)~0x01;      /* watchdog off: the ROM bootloader does not feed it */
    SAFE_EXIT();
    __asm
        ljmp 0x3800
    __endasm;
}

/* ------------------------------------------------------------------ bit-banged I2C, one copy per bus
 * SDA / SCL: the sbits; DLY: nops per quarter bit. Bus A ~130-250 kHz, bus B ~100 kHz at 16 MHz (check on a scope).
 * SCL release waits for clock stretching, with a timeout. */
#define I2C_BUS(P, SDA, SCL, DLY)                                                                               \
static void P##_dly(void) { uint8_t i; for (i = 0; i < (DLY); i++) __asm nop __endasm; }                      \
static void P##_scl_high(void) { uint16_t t = 2000; SCL = 1; while (!SCL && --t) ; }                           \
static void P##_start(void) { SDA = 1; P##_scl_high(); P##_dly(); SDA = 0; P##_dly(); SCL = 0; P##_dly(); }    \
static void P##_stop(void) { SDA = 0; P##_dly(); P##_scl_high(); P##_dly(); SDA = 1; P##_dly(); }              \
static uint8_t P##_tx(uint8_t b)                                                                                \
{                                                                                                               \
    uint8_t i, nack;                                                                                            \
    for (i = 0; i < 8; i++) {                                                                                   \
        SDA = (b & 0x80) ? 1 : 0;                                                                               \
        b <<= 1;                                                                                                \
        P##_dly(); P##_scl_high(); P##_dly();                                                                   \
        SCL = 0;                                                                                                \
    }                                                                                                           \
    SDA = 1;                           /* release for ACK */                                                   \
    P##_dly(); P##_scl_high(); P##_dly();                                                                       \
    nack = SDA;                                                                                                 \
    SCL = 0; P##_dly();                                                                                         \
    return nack;                                                                                                \
}

I2C_BUS(ia, I2C_SDA, I2C_SCL, 6)
I2C_BUS(ib, SDA5, SCL5, 14)

uint8_t i2c_write_buf(uint8_t a, uint8_t ctrl, const uint8_t __xdata *buf, uint8_t n)
{
    uint8_t e;
    ia_start();
    e = ia_tx((uint8_t)(a << 1));
    if (!e) e = ia_tx(ctrl);
    while (!e && n--) e = ia_tx(*buf++);
    ia_stop();
    return e;
}

uint8_t i2c_write_code(uint8_t a, uint8_t ctrl, const uint8_t __code *buf, uint8_t n)
{
    uint8_t e;
    ia_start();
    e = ia_tx((uint8_t)(a << 1));
    if (!e) e = ia_tx(ctrl);
    while (!e && n--) e = ia_tx(*buf++);
    ia_stop();
    return e;
}

static uint8_t ib_rx(uint8_t ack)
{
    uint8_t i, b = 0;
    SDA5 = 1;
    for (i = 0; i < 8; i++) {
        ib_dly(); ib_scl_high(); ib_dly();
        b = (uint8_t)((b << 1) | (SDA5 ? 1 : 0));
        SCL5 = 0;
    }
    SDA5 = ack ? 0 : 1;
    ib_dly(); ib_scl_high(); ib_dly();
    SCL5 = 0;
    ib_dly();                          /* hold the ACK past SCL low before releasing SDA */
    SDA5 = 1;
    return b;
}

uint8_t ap_write(uint8_t reg, const uint8_t __xdata *buf, uint8_t n)
{
    uint8_t e;
    ib_start();
    e = ib_tx((uint8_t)(AP_ADDR << 1));
    if (!e) e = ib_tx(reg);
    while (!e && n--) e = ib_tx(*buf++);
    ib_stop();
    return e;
}

uint8_t ap_read(uint8_t reg, uint8_t __xdata *buf, uint8_t n)
{
    uint8_t e;
    ib_start();
    e = ib_tx((uint8_t)(AP_ADDR << 1));
    if (!e) e = ib_tx(reg);
    if (!e) {
        ib_start();                    /* repeated start */
        e = ib_tx((uint8_t)((AP_ADDR << 1) | 1));
    }
    if (!e)
        while (n--)
            *buf++ = ib_rx(n != 0);    /* ACK every byte but the last */
    ib_stop();
    return e;
}
