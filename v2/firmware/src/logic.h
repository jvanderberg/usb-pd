/* Hardware-independent logic: compiled by SDCC for the CH552 and by the host compiler for test/test_logic.c */
#ifndef LOGIC_H
#define LOGIC_H
#include <stdint.h>

#ifdef SDCC
#define CODE __code
#define XDATA __xdata
#else
#define CODE
#define XDATA
#endif

/* ---- AP33772S source capabilities. SRCPDO (0x20) returns 13 little-endian 16-bit words; words 0..6 are SPR PDO1..7
 * (DS "Source PDO Information Getting"). Word layout (CentyLab and hnz1102 drivers; requirements-v2 s8):
 *   bit 15 detected, bit 14 type (0 fixed, 1 APDO = PPS in SPR), bits 13:10 I_max code,
 *   fixed: bits 7:0 V in 100 mV;  PPS: bits 7:0 V_max in 100 mV, bits 9:8 V_min code (1 = 3.3 V, 2 = 3.3-5 V) */
#define NPDO 7
struct pdos {
    uint8_t nfix;                       /* fixed PDOs, in the source's order (rising voltage) */
    uint8_t fix_idx[NPDO];              /* PDO_INDEX (1..7) for PD_REQMSG */
    uint16_t fix_mv[NPDO];
    uint8_t fix_imax[NPDO];             /* current code 0..15 */
    uint8_t npps;
    uint8_t pps_idx[NPDO];
    uint16_t pps_min_mv[NPDO], pps_max_mv[NPDO];
    uint8_t pps_imax[NPDO];
};
void pdo_parse(const uint16_t XDATA *words, struct pdos XDATA *p);

/* current code (PDO I_max, PD_REQMSG CURRENT_SEL) -> mA: 1.00 A + 0.25 A per code, code 15 = 5.00 A */
uint16_t code_ma(uint8_t c);
/* OCPTHR (50 mA/LSB) for a current code: the AP33772S trips at 110 % of it after 30 ms (DS table 7) */
uint8_t ocp_thr(uint8_t c);

/* ---- PPS: one entry for every PPS APDO together, clamped to 5.0-21.0 V (requirements-v2 s4) */
#define PPS_MIN_MV 5000u
#define PPS_MAX_MV 21000u
/* the range the PPS entry offers; returns 0 if there is no PPS APDO (or the clamp leaves nothing) */
uint8_t pps_range(const struct pdos XDATA *p, uint16_t *lo, uint16_t *hi);
/* the APDO (index into pps_*) that covers mv with the most current, or 0xFF */
uint8_t pps_pick(const struct pdos XDATA *p, uint16_t mv);
/* UP / DOWN hold-to-accelerate (requirements-v2 s3): the step for a press held `held_ms` (100 / 500 / 1000 mV) */
uint16_t pps_step_mv(uint16_t held_ms);
#define REPEAT_START_MS 400
#define REPEAT_EVERY_MS 100
/* setpoint + dir * step, kept inside [lo, hi] */
uint16_t pps_adjust(uint16_t mv, int8_t dir, uint16_t step, uint16_t lo, uint16_t hi);

/* ---- PD_REQMSG (0x31) words (DS table 2): PDO_INDEX 15:12, CURRENT_SEL 11:8, VOLTAGE_SEL 7:0 (100 mV for PPS) */
uint16_t req_fixed(uint8_t pdo_index);          /* max current of a fixed PDO: CURRENT_SEL 0xF, VOLTAGE_SEL 0xFF */
uint16_t req_pps(uint8_t pdo_index, uint8_t isel, uint16_t mv);

/* ---- readings: VOLTAGE (0x11) 80 mV/LSB, CURRENT (0x12) 24 mA/LSB (DS table 19) */
uint16_t ap_mv(uint16_t raw);
uint16_t ap_ma(uint8_t raw);

/* ---- cheap arithmetic. The 8051 has an 8x8 MUL and an 8/8 DIV and nothing wider; SDCC's 32-bit helpers cost
 * ~30,000 clocks per formatted number. Everything here is 16-bit shift-and-add. */
uint16_t div10(uint16_t n);                           /* exact n / 10 for every uint16_t */
int16_t sdiv10(int16_t n);                            /* rounds toward zero, like C's / */
uint16_t power_dw(uint16_t mv, int16_t ma);           /* 0.1 W units, ~0.3 % (0 for negative current) */

/* ---- thermal policy (on top of the AP33772S OTP). The NTC (RT1, on the AP33772S OTP pin) sits ~3.5 mm from Q1 */
#define T_TRIP_BOARD_DC   850   /* NTC trip: 85.0 C */
#define T_TRIP_Q1_DC     1100   /* estimated Q1 board temperature trip: 110.0 C */
#define T_RESUME_DC       650   /* lockout ends below 65.0 C */
#define Q1_RISE_5A_DC     320   /* Q1 board above the NTC at 5 A: 30 K solved (5 V / 5 A, Q1 20 mOhm at the AP33772S
                                   gate drive, v2 review M1; thermal.md) + margin */
#define OTP_THR_C          90   /* AP33772S OTPTHR: a hardware backstop just above the firmware's 85 C trip */
int16_t q1_estimate_dc(int16_t ntc_dc, int16_t ma);   /* errs a few % high (the safe side) */
uint8_t over_temperature(int16_t ntc_dc, int16_t ma); /* 1 = turn the output off now */

/* ---- settings log in the CH552 DataFlash (128 bytes, byte-writable, ~10K writes per cell: CH552 DS s6.2)
 * 16 slots x 8 bytes: [0xA5][seq][d0][d1][d2][d3][chk][-], chk = seq ^ d0..d3 ^ 0x5A; each save writes the next
 * slot, so a cell sees one write per 16 saves. */
#define SLOT_N 16
#define SLOT_SZ 8
#define SET_N 4
/* d0: bit 7 output on, bit 6 PPS selected, bit 5 editing current. d1: fixed voltage / 100 mV.
   d2: PPS setpoint / 100 mV. d3: PPS current code */
#define SET_ON   0x80
#define SET_PPS  0x40
#define SET_EDI  0x20
uint8_t df_read(uint8_t off);           /* provided by hw.c on the CH552, by a fake in test/test_logic.c */
void df_write(uint8_t off, uint8_t v);
uint8_t settings_load(uint8_t XDATA *d, uint8_t *slot, uint8_t *seq);   /* newest valid record -> d[SET_N]; 0 = none */
void settings_save(const uint8_t XDATA *d, uint8_t *slot, uint8_t *seq);

/* ---- the screen: two 16-character rows (host-testable; test/screens.c renders them through src/oled.c).
 * In PPS a '>' points at the value UP / DOWN edits (voltage or current limit). */
enum { ST_OK, ST_HOT, ST_OCP, ST_OVP, ST_UVP, ST_FAIL, ST_DENIED };
struct ui {
    uint8_t pps, legacy, status, out_on, edit_i, show_set;
    uint16_t set_mv;            /* fixed: the PDO voltage; PPS: the setpoint */
    uint16_t lim_ma;            /* fixed: what the charger allows (0 = unknown); PPS: the current limit */
    uint16_t mv;                /* measured output voltage */
    int16_t ma, t_dc;           /* measured output current, NTC temperature (0.1 C) */
};
/* fixed: "20V  ON     45C" / " 3.21A    64.2W" or "MAX 3.00A"; PPS: ">12.3V  ON   45C" / " 2.95A LIM 3.00A".
   r0 / r1: 17 bytes */
void ui_lines(char XDATA *r0, char XDATA *r1, const struct ui XDATA *u);

/* ---- tiny text formatting (no printf on the 8051): value/10^dp with `dp` decimals right-aligned in `width` chars */
uint8_t fmt_fixed(char XDATA *out, int16_t value, uint8_t dp, uint8_t width);
#endif
