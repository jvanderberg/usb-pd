#include "hw.h"

volatile uint16_t ms_ticks;
static volatile uint8_t btn_state, btn_events;
static uint8_t btn_cnt_on, btn_cnt_cyc;

/* Timer0, mode 1, Fsys/12 = 1.333 MHz: 1333 counts per ms (0.025 % fast, which nothing here notices) */
#define T0_RELOAD (65536u - (uint16_t)(FSYS_HZ / 12u / 1000u))

void timer0_isr(void) __interrupt(1)
{
    TH0 = (uint8_t)(T0_RELOAD >> 8);
    TL0 = (uint8_t)T0_RELOAD;
    ms_ticks++;
    /* buttons: a level must hold 10 consecutive ms to count; a new press latches an event for the main loop */
    if ((uint8_t)(!BTN_ON) != (uint8_t)(btn_state & BTN_EV_ON)) {
        if (++btn_cnt_on >= 10) {
            btn_cnt_on = 0;
            btn_state ^= BTN_EV_ON;
            if (btn_state & BTN_EV_ON) btn_events |= BTN_EV_ON;
        }
    } else {
        btn_cnt_on = 0;
    }
    if ((uint8_t)(BTN_CYC ? BTN_EV_CYC : 0) != (uint8_t)(btn_state & BTN_EV_CYC)) {
        if (++btn_cnt_cyc >= 10) {
            btn_cnt_cyc = 0;
            btn_state ^= BTN_EV_CYC;
            if (btn_state & BTN_EV_CYC) btn_events |= BTN_EV_CYC;
        }
    } else {
        btn_cnt_cyc = 0;
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
    /* output switch first: P1.0 push-pull low (reset state is quasi-bidirectional, weak high into R7 2.2k = off) */
    SW_EN = 0;
    P1_MOD_OC &= (uint8_t)~0x01;
    P1_DIR_PU |= 0x01;

    /* I2C P1.6/P1.7: open drain, NO internal pull-up (the pull-ups go to 3.3 V; the CH552 runs at 5 V and the
       INA226 / OLED inputs allow only VS + 0.3 V). Released (latch 1). Never push-pull. */
    I2C_SDA = 1;
    I2C_SCL = 1;
    P1_MOD_OC |= 0xC0;
    P1_DIR_PU &= (uint8_t)~0xC0;

    /* P1.1 ON button to GND: open drain + pull-up (latch stays 1, so it is only ever an input) */
    P1_MOD_OC |= 0x02;
    P1_DIR_PU |= 0x02;
    /* P1.4 CYCLE button to 3.3 V with R3 10k pull-down: high-impedance input (an internal pull-up to 5 V would feed
       the 3.3 V rail through the button and fight R3) */
    P1_MOD_OC &= (uint8_t)~0x10;
    P1_DIR_PU &= (uint8_t)~0x10;
    /* P3.2 NTC: high-impedance analog input (CH552 DS s15: pull-ups off for ADC pins) */
    P3_MOD_OC &= (uint8_t)~0x04;
    P3_DIR_PU &= (uint8_t)~0x04;

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

    ADC_CFG = 0x08 | 0x01;      /* ADC_EN, fast clock (96 Fosc per conversion = 4 us) */

    /* watchdog reset: a hung loop must not leave the output on without its thermal / current checks */
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

uint8_t adc_read(uint8_t ch)
{
    ADC_CTRL = (uint8_t)(ch & 0x03);   /* channel, clear flags */
    ADC_CTRL |= 0x10;                  /* ADC_START */
    while (ADC_CTRL & 0x10)
        ;
    return ADC_DATA;
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
    EA = 0;                            /* (no delay_ms after this: the tick interrupt is off) */
    SW_EN = 0;
    SAFE_ENTER();
    GLOBAL_CFG &= (uint8_t)~0x01;      /* watchdog off: the ROM bootloader does not feed it */
    SAFE_EXIT();
    __asm
        ljmp 0x3800
    __endasm;
}

/* ------------------------------------------------------------------ I2C, ~130-250 kHz at 16 MHz (verify on a scope) */
static void i2c_dly(void)
{
    uint8_t i;
    for (i = 0; i < 6; i++)
        __asm nop __endasm;
}

static void scl_high(void)
{
    uint16_t t = 2000;                 /* clock stretching, with a timeout */
    I2C_SCL = 1;
    while (!I2C_SCL && --t)
        ;
}

static void i2c_start(void)
{
    I2C_SDA = 1; scl_high(); i2c_dly();
    I2C_SDA = 0; i2c_dly();
    I2C_SCL = 0; i2c_dly();
}

static void i2c_stop(void)
{
    I2C_SDA = 0; i2c_dly();
    scl_high(); i2c_dly();
    I2C_SDA = 1; i2c_dly();
}

static uint8_t i2c_tx(uint8_t b)
{
    uint8_t i, nack;
    for (i = 0; i < 8; i++) {
        I2C_SDA = (b & 0x80) ? 1 : 0;
        b <<= 1;
        i2c_dly(); scl_high(); i2c_dly();
        I2C_SCL = 0;
    }
    I2C_SDA = 1;                       /* release for ACK */
    i2c_dly(); scl_high(); i2c_dly();
    nack = I2C_SDA;
    I2C_SCL = 0; i2c_dly();
    return nack;
}

static uint8_t i2c_rx(uint8_t ack)
{
    uint8_t i, b = 0;
    I2C_SDA = 1;
    for (i = 0; i < 8; i++) {
        i2c_dly(); scl_high(); i2c_dly();
        b = (uint8_t)((b << 1) | (I2C_SDA ? 1 : 0));
        I2C_SCL = 0;
    }
    I2C_SDA = ack ? 0 : 1;
    i2c_dly(); scl_high(); i2c_dly();
    I2C_SCL = 0;
    i2c_dly();                         /* hold the ACK past SCL low before releasing SDA */
    I2C_SDA = 1;
    return b;
}

uint8_t i2c_write_reg8(uint8_t a, uint8_t reg, uint8_t v)
{
    uint8_t e;
    i2c_start();
    e = i2c_tx((uint8_t)(a << 1));
    if (!e) e = i2c_tx(reg);
    if (!e) e = i2c_tx(v);
    i2c_stop();
    return e;
}

uint8_t i2c_write_reg16(uint8_t a, uint8_t reg, uint16_t v)
{
    uint8_t e;
    i2c_start();
    e = i2c_tx((uint8_t)(a << 1));
    if (!e) e = i2c_tx(reg);
    if (!e) e = i2c_tx((uint8_t)(v >> 8));    /* INA226: MSB first */
    if (!e) e = i2c_tx((uint8_t)v);
    i2c_stop();
    return e;
}

static uint8_t read_start(uint8_t a, uint8_t reg)
{
    uint8_t e;
    i2c_start();
    e = i2c_tx((uint8_t)(a << 1));
    if (!e) e = i2c_tx(reg);
    if (e) { i2c_stop(); return e; }
    i2c_start();                              /* repeated start */
    e = i2c_tx((uint8_t)((a << 1) | 1));
    if (e) i2c_stop();
    return e;
}

uint8_t i2c_read_reg8(uint8_t a, uint8_t reg, uint8_t *v)
{
    if (read_start(a, reg)) return 1;
    *v = i2c_rx(0);
    i2c_stop();
    return 0;
}

uint8_t i2c_read_reg16(uint8_t a, uint8_t reg, uint16_t *v)
{
    uint8_t hi;
    if (read_start(a, reg)) return 1;
    hi = i2c_rx(1);
    *v = (uint16_t)((uint16_t)hi << 8 | i2c_rx(0));
    i2c_stop();
    return 0;
}

uint8_t i2c_write_buf(uint8_t a, uint8_t ctrl, const uint8_t __xdata *buf, uint8_t n)
{
    uint8_t e;
    i2c_start();
    e = i2c_tx((uint8_t)(a << 1));
    if (!e) e = i2c_tx(ctrl);
    while (!e && n--) e = i2c_tx(*buf++);
    i2c_stop();
    return e;
}

uint8_t i2c_write_code(uint8_t a, uint8_t ctrl, const uint8_t __code *buf, uint8_t n)
{
    uint8_t e;
    i2c_start();
    e = i2c_tx((uint8_t)(a << 1));
    if (!e) e = i2c_tx(ctrl);
    while (!e && n--) e = i2c_tx(*buf++);
    i2c_stop();
    return e;
}
