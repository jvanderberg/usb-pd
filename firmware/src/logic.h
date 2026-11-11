/* Hardware-independent logic: compiled by SDCC for the CH552 and by the host compiler for test/test_logic.c */
#ifndef LOGIC_H
#define LOGIC_H
#include <stdint.h>

#ifdef SDCC
#define NTC_CODE __code
#define XDATA __xdata
#else
#define NTC_CODE
#define XDATA
#endif

/* ---- PD voltages: index 0..5 = 5, 9, 12, 15, 18, 20 V (HUSB238 SRC_PDO_5V..20V, registers 0x02..0x07) */
#define NVOLT 6
extern const NTC_CODE uint8_t volt_v[NVOLT];        /* volts per index */
extern const NTC_CODE uint8_t volt_sel[NVOLT];      /* HUSB238 SRC_PDO.PDO_SELECT code per index (reg 0x08 [7:4]) */
extern const NTC_CODE uint16_t pdo_current_ma[16];  /* HUSB238 4-bit current code -> mA (SRC_PDO_xV[3:0], PD_STATUS0[3:0]) */

/* next offered voltage after `cur` (wrapping); `offered` bit i = SRC_PDO for index i detected. Returns cur if none other */
uint8_t next_offered(uint8_t cur, uint8_t offered);
/* HUSB238 PD_STATUS0[7:4] voltage code -> index, or 0xFF (0000 unattached / reserved) */
uint8_t status_volt_index(uint8_t pd_status0);

/* ---- temperature: NTC ADC counts -> 0.1 C (interpolated); clamps at the table ends */
int16_t ntc_decic(uint8_t counts);

/* ---- cheap arithmetic. The 8051 has an 8x8 MUL and an 8/8 DIV and nothing wider; SDCC's 32-bit divide and
 * multiply helpers cost ~30,000 standard-8051 clocks per formatted number (s51). Everything here is 16-bit
 * shift-and-add, accurate to the display's last digit, not to the last mA. */
uint16_t div10(uint16_t n);                           /* exact n / 10 for every uint16_t */
int16_t sdiv10(int16_t n);                            /* rounds toward zero, like C's / */

/* ---- INA226 raw registers -> engineering units (5 mOhm shunt) */
int16_t ina_current_ma(int16_t shunt_raw);            /* 2.5 uV/LSB / 5 mOhm = 0.5 mA/LSB: +-16383 mA */
uint16_t ina_bus_mv(uint16_t bus_raw);                /* 1.25 mV/LSB: raw + raw/4 (bit 15 is always 0) */
uint16_t power_dw(uint16_t mv, int16_t ma);           /* 0.1 W units, ~0.3 % (0 for negative current) */

/* ---- thermal / current policy */
#define T_TRIP_BOARD_DC   850   /* NTC (board, beside U4) trip: 85.0 C */
#define T_TRIP_Q1_DC     1100   /* estimated Q1 board temperature trip: 110.0 C */
#define T_RESUME_DC       650   /* lockout ends below 65.0 C */
#define Q1_RISE_5A_DC     250   /* Q1 above the board average at 5 A, from the thermal solve (13-23 K) + margin */
int16_t q1_estimate_dc(int16_t board_dc, int16_t ma); /* errs a few % high (the safe side) */
/* returns 1 if the output must be turned off now */
uint8_t over_temperature(int16_t board_dc, int16_t ma);

/* ---- settings log in the CH552 DataFlash (128 bytes, byte-writable, ~10K writes per cell: CH552 DS s6.2)
 * 32 slots x 4 bytes: [0xA5][seq][data][seq ^ data ^ 0x5A]; each save writes the next slot, so a cell sees one write
 * per 32 saves. data: bits 2..0 voltage index, bit 7 output on. */
#define SLOT_N 32
#define SLOT_SZ 4
/* storage: provided by hw.c on the CH552, by a fake in test/test_logic.c */
uint8_t df_read(uint8_t off);
void df_write(uint8_t off, uint8_t v);
/* finds the newest valid record; returns 1 and fills *data / *slot / *seq, or 0 if none */
uint8_t settings_load(uint8_t *data, uint8_t *slot, uint8_t *seq);
/* writes `data` to the slot after `*slot`, updating *slot / *seq */
void settings_save(uint8_t data, uint8_t *slot, uint8_t *seq);

/* ---- the screen: two 16-character rows (host-testable; test/screens.c renders them through src/oled.c) */
enum { ST_OK, ST_NOPD, ST_HOT, ST_OVERCURRENT, ST_FAIL };
struct ui {
    uint8_t vidx, status, out_on, ina_ok;
    uint16_t max_ma;            /* what the charger allows at the selected voltage; 0 = not offered */
    uint16_t mv;
    int16_t ma, t_dc;
};
/* row 0 "20V  ON     45C"; row 1 "3.21A    64.2W", "MAX 3.00A", or why the output is off. r0 / r1: 17 bytes */
void ui_lines(char XDATA *r0, char XDATA *r1, const struct ui XDATA *u);

/* ---- tiny text formatting (no printf on the 8051) */
/* writes value/10^dp with `dp` decimals right-aligned in `width` chars; returns chars written */
uint8_t fmt_fixed(char *out, int16_t value, uint8_t dp, uint8_t width);
#endif
