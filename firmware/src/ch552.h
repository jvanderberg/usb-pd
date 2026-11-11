/* CH552 special function registers used by this firmware, transcribed from the CH552 datasheet (docs/datasheets/
 * CH552E.pdf) SFR map (s5) and register tables (s5.4 GLOBAL_CFG, s6.4 flash-ROM, s8.2 CLOCK_CFG, s10 ports, s15 ADC). */
#ifndef CH552_H
#define CH552_H

__sfr __at(0x80) ADC_CTRL;    /* b4 ADC_START, b5 ADC_IF, b1..0 ADC_CHAN */
__sfr __at(0x84) ROM_ADDR_L;
__sfr __at(0x85) ROM_ADDR_H;
__sfr __at(0x86) ROM_CTRL;    /* write: 0x9A write, 0x8E read DataFlash */
__sfr __at(0x86) ROM_STATUS;  /* read: b6 bROM_ADDR_OK, b1 bROM_CMD_ERR */
__sfr __at(0x87) PCON;
__sfr __at(0x88) TCON;
__sfr __at(0x89) TMOD;
__sfr __at(0x8A) TL0;
__sfr __at(0x8C) TH0;
__sfr __at(0x8E) ROM_DATA_L;
__sfr __at(0x8F) ROM_DATA_H;
__sfr __at(0x90) P1;
__sfr __at(0x92) P1_MOD_OC;
__sfr __at(0x93) P1_DIR_PU;
__sfr __at(0x96) P3_MOD_OC;
__sfr __at(0x97) P3_DIR_PU;
__sfr __at(0x9A) ADC_CFG;     /* b3 ADC_EN, b0 ADC_CLK (1 = 96 Fosc per conversion) */
__sfr __at(0x9F) ADC_DATA;
__sfr __at(0xA1) SAFE_MOD;
__sfr __at(0xA8) IE;
__sfr __at(0xB0) P3;
__sfr __at(0xB1) GLOBAL_CFG;  /* b4 bSW_RESET, b2 bDATA_WE (safe mode only) */
__sfr __at(0xB9) CLOCK_CFG;   /* b2..0 MASK_SYS_CK_SEL */
__sfr __at(0xFF) WDOG_COUNT;  /* Fsys/65536 per count: from 00h, 1.05 s at 16 MHz (DS s7.3.4) */

__sbit __at(0x8C) TR0;        /* TCON.4 */
__sbit __at(0x8D) TF0;        /* TCON.5 */
__sbit __at(0xA9) ET0;        /* IE.1 */
__sbit __at(0xAF) EA;         /* IE.7 */

/* board pins (hardware/usbpd.kicad_sch) */
__sbit __at(0x90) SW_EN;      /* P1.0 -> Q2 -> Q1 gate: 1 = output on */
__sbit __at(0x91) BTN_ON;     /* P1.1, to GND: 0 = pressed (internal pull-up) */
__sbit __at(0x94) BTN_CYC;    /* P1.4, to 3V3 with R3 10k to GND: 1 = pressed */
__sbit __at(0x96) I2C_SDA;    /* P1.6 open drain, through R12 150R */
__sbit __at(0x97) I2C_SCL;    /* P1.7 open drain, through R13 150R */

#define ADC_CH_P32 3          /* AIN3 = P3.2 (NTC divider) */

#define SAFE_ENTER() do { SAFE_MOD = 0x55; SAFE_MOD = 0xAA; } while (0)
#define SAFE_EXIT()  do { SAFE_MOD = 0x00; } while (0)
#endif
