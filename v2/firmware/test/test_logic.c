/* Host tests for src/logic.c (the board-independent part of the v2 firmware). `make test` */
#include <stdio.h>
#include <string.h>
#include "logic.h"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t flash[128];
static int writes[128];
uint8_t df_read(uint8_t off) { return flash[off & 0x7F]; }
void df_write(uint8_t off, uint8_t v) { flash[off & 0x7F] = v; writes[off & 0x7F]++; }

/* SRCPDO words as the AP33772S reports them */
static uint16_t fixed(uint16_t mv, uint8_t icode) { return (uint16_t)(0x8000 | (icode << 10) | (mv / 100)); }
static uint16_t pps(uint8_t vmin_code, uint16_t vmax_mv, uint8_t icode)
{
    return (uint16_t)(0xC000 | (icode << 10) | (vmin_code << 8) | (vmax_mv / 100));
}

static void test_pdos(void)
{
    /* a typical 65 W PPS charger: 5/9/15/20 V fixed, PPS 3.3-11 V 5 A and 3.3-21 V 3.25 A */
    uint16_t w[NPDO] = {fixed(5000, 12), fixed(9000, 12), fixed(15000, 12), fixed(20000, 9), pps(1, 11000, 15),
                        pps(1, 21000, 9), 0};
    struct pdos p;
    uint16_t lo = 0, hi = 0;
    pdo_parse(w, &p);
    CHECK(p.nfix == 4 && p.npps == 2, "nfix %u npps %u", p.nfix, p.npps);
    CHECK(p.fix_idx[3] == 4 && p.fix_mv[3] == 20000 && p.fix_imax[3] == 9, "20 V PDO4, 3.25 A code");
    CHECK(p.pps_idx[0] == 5 && p.pps_min_mv[0] == 3300 && p.pps_max_mv[0] == 11000 && p.pps_imax[0] == 15, "APDO5");
    CHECK(pps_range(&p, &lo, &hi) && lo == 5000 && hi == 21000, "one PPS entry 5.0-21.0 V, got %u-%u", lo, hi);
    CHECK(pps_pick(&p, 9000) == 0, "9 V: the 5 A range");
    CHECK(pps_pick(&p, 11000) == 0, "11 V still the 5 A range");
    CHECK(pps_pick(&p, 11100) == 1, "11.1 V: only the 21 V range");
    CHECK(pps_pick(&p, 21100) == 0xFF, "above every range");

    /* no PPS: no entry */
    {
        uint16_t w2[NPDO] = {fixed(5000, 8), fixed(9000, 8), 0, 0, 0, 0, 0};
        pdo_parse(w2, &p);
        CHECK(p.nfix == 2 && p.npps == 0 && !pps_range(&p, &lo, &hi), "fixed only");
    }
    /* a PPS range that ends below 5 V gives nothing after the 5 V clamp; one to 25 V is clamped to 21 V */
    {
        uint16_t w3[NPDO] = {fixed(5000, 8), pps(1, 4500, 8), 0, 0, 0, 0, 0};
        pdo_parse(w3, &p);
        CHECK(!pps_range(&p, &lo, &hi), "3.3-4.5 V PPS is unusable");
        w3[1] = pps(2, 25000, 8);
        pdo_parse(w3, &p);
        CHECK(pps_range(&p, &lo, &hi) && lo == 5000 && hi == 21000, "clamped to 21 V");
    }
    /* undetected words are skipped, and indices follow the PDO position */
    {
        uint16_t w4[NPDO] = {fixed(5000, 8), 0x0000, fixed(12000, 8), 0, 0, 0, 0};
        pdo_parse(w4, &p);
        CHECK(p.nfix == 2 && p.fix_idx[1] == 3, "12 V is PDO3");
    }
}

static void test_codes(void)
{
    CHECK(code_ma(0) == 1000 && code_ma(8) == 3000 && code_ma(14) == 4500 && code_ma(15) == 5000, "current codes");
    CHECK(ocp_thr(0) == 20 && ocp_thr(8) == 60 && ocp_thr(15) == 100, "OCPTHR = mA / 50");
    {
        int c, bad = 0;
        for (c = 0; c < 16; c++)
            if (ocp_thr((uint8_t)c) * 50 != code_ma((uint8_t)c)) bad++;
        CHECK(!bad, "ocp_thr x 50 == code_ma");
    }
    /* DS table 2 example: PDO3 at max -> 0x3FFF */
    CHECK(req_fixed(3) == 0x3FFF, "fixed request 0x%04X", req_fixed(3));
    CHECK(req_pps(5, 8, 12300) == 0x587B, "PPS PDO5, 3.00 A, 12.3 V -> 0x%04X", req_pps(5, 8, 12300));
    CHECK(req_pps(6, 15, 21000) == 0x6FD2, "PPS 21.0 V 5 A -> 0x%04X", req_pps(6, 15, 21000));
    CHECK(ap_mv(250) == 20000, "VOLTAGE 250 x 80 mV");
    CHECK(ap_ma(208) == 4992, "CURRENT 208 x 24 mA");
    CHECK(ap_mv(819) == 65520, "VOLTAGE max without overflow");
}

static void test_adjust(void)
{
    CHECK(pps_step_mv(0) == 100 && pps_step_mv(999) == 100, "tap / short hold: 100 mV");
    CHECK(pps_step_mv(1000) == 500 && pps_step_mv(2499) == 500, "after 1 s: 500 mV");
    CHECK(pps_step_mv(2500) == 1000 && pps_step_mv(60000) == 1000, "after 2.5 s: 1 V");
    CHECK(pps_adjust(5000, -1, 100, 5000, 21000) == 5000, "DOWN stops at 5.0 V");
    CHECK(pps_adjust(20800, 1, 500, 5000, 21000) == 21000, "UP stops at 21.0 V");
    CHECK(pps_adjust(5300, -1, 1000, 5000, 21000) == 5000, "no underflow below the floor");
    CHECK(pps_adjust(12300, 1, 100, 5000, 21000) == 12400, "12.3 -> 12.4");
}

static void test_div(void)
{
    long n, bad = 0;
    for (n = 0; n <= 0xFFFF; n++)
        if (div10((uint16_t)n) != n / 10) bad++;
    CHECK(bad == 0, "div10 wrong for %ld of 65536 inputs", bad);
    for (n = -32767; n <= 32767; n++)
        if (sdiv10((int16_t)n) != n / 10) bad++;
    CHECK(bad == 0, "sdiv10 wrong for %ld inputs", bad);
    {
        long mv, ma, worst = 0;
        for (mv = 0; mv <= 21000; mv += 97)
            for (ma = 0; ma <= 6120; ma += 13) {
                long exact = mv * ma / 100000, got = power_dw((uint16_t)mv, (int16_t)ma);
                long err = got > exact ? got - exact : exact - got;
                if (err > exact / 200 + 2 && err > worst) worst = err;
            }
        CHECK(worst == 0, "power_dw off by %ld dW", worst);
    }
}

static void test_thermal(void)
{
    CHECK(q1_estimate_dc(500, 0) == 500, "0 A adds nothing");
    CHECK(!over_temperature(600, 3000), "60 C at 3 A runs");
    CHECK(over_temperature(850, 0), "NTC at 85 C trips");
    CHECK(!over_temperature(750, 5000), "75 C + ~33 K: under both trips");
    CHECK(over_temperature(790, 5000), "79 C + ~33 K: the Q1 estimate trips");
    CHECK(OTP_THR_C * 10 > T_TRIP_BOARD_DC, "the AP33772S OTP is a backstop above the firmware trip");
}

static void test_settings(void)
{
    uint8_t d[SET_N], slot, seq, i;
    memset(flash, 0xFF, sizeof flash);
    CHECK(!settings_load(d, &slot, &seq), "blank flash: nothing");
    memset(flash, 0x00, sizeof flash);
    CHECK(!settings_load(d, &slot, &seq), "zeroed flash: nothing");

    slot = SLOT_N - 1; seq = 0;
    for (i = 0; i < 100; i++) {
        uint8_t w[SET_N] = {(uint8_t)(i & 0xE0), 200, (uint8_t)(50 + i), (uint8_t)(i & 15)};
        settings_save(w, &slot, &seq);
    }
    CHECK(settings_load(d, &slot, &seq) && d[2] == 149 && d[3] == (99 & 15) && d[1] == 200, "newest after wrap: %u",
          d[2]);

    /* torn write: magic cleared then power lost -> previous record wins */
    {
        uint8_t s2 = (uint8_t)((slot + 1) % SLOT_N);
        flash[s2 * SLOT_SZ] = 0x00;
        flash[s2 * SLOT_SZ + 1] = (uint8_t)(seq + 1);
        CHECK(settings_load(d, &slot, &seq) && d[2] == 149, "torn write ignored");
    }
    /* a corrupted data byte fails the check byte */
    {
        uint8_t keep = flash[slot * SLOT_SZ + 3];
        flash[slot * SLOT_SZ + 3] ^= 0x01;
        uint8_t s0 = slot, q0 = seq;
        CHECK(settings_load(d, &slot, &seq) && d[2] == 148, "corrupt newest record: the one before wins (%u)", d[2]);
        slot = s0; seq = q0;
        flash[slot * SLOT_SZ + 3] = keep;
    }
    /* wear: 1000 saves -> each cell written about 1000/16 times (x2 for the magic byte) */
    {
        int i2, maxw = 0;
        uint8_t w[SET_N] = {0, 50, 120, 4};
        memset(writes, 0, sizeof writes);
        for (i2 = 0; i2 < 1000; i2++) {
            w[2] = (uint8_t)(50 + i2 % 160);
            settings_save(w, &slot, &seq);
        }
        for (i2 = 0; i2 < 128; i2++)
            if (writes[i2] > maxw) maxw = writes[i2];
        CHECK(maxw <= 2 * (1000 / SLOT_N + 1), "max writes per cell %d", maxw);
        printf("wear: 1000 saves -> at most %d writes per cell (10K-cycle cells: ~%d saves)\n",
               maxw, 10000 * 1000 / maxw);
    }
}

static void test_ui(void)
{
    char r0[17], r1[17];
    struct ui u;
    memset(&u, 0, sizeof u);
    u.set_mv = 20000; u.lim_ma = 3250; u.out_on = 1; u.mv = 20012; u.ma = 3217; u.t_dc = 452;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r0, "20V  ON      45C"), "[%s]", r0);
    CHECK(!strcmp(r1, " 3.21A    64.4W "), "[%s]", r1);
    u.out_on = 0;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r0, "20V OFF      45C"), "[%s]", r0);
    CHECK(!strcmp(r1, "MAX  3.25A      "), "[%s]", r1);

    /* PPS, editing voltage: '>' points at the voltage */
    u.pps = 1; u.set_mv = 12300; u.lim_ma = 3000; u.out_on = 1; u.show_set = 1; u.ma = 2950; u.edit_i = 0;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r0, ">12.3V  ON   45C"), "[%s]", r0);
    CHECK(!strcmp(r1, " 2.95A LIM 3.00A"), "[%s]", r1);
    /* a second after the last press: the measured voltage (80 mV steps, rounded to 0.1 V) */
    u.show_set = 0; u.mv = 12240;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r0, ">12.2V  ON   45C"), "[%s]", r0);
    /* editing current: '>' points at the limit; off shows the setpoint */
    u.edit_i = 1; u.out_on = 0;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r0, " 12.3V OFF   45C"), "[%s]", r0);
    CHECK(!strcmp(r1, "       LIM>3.00A"), "[%s]", r1);
    u.status = ST_UVP;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r1, "UNDER V         "), "[%s]", r1);
    /* legacy (non-PD) 5 V */
    memset(&u, 0, sizeof u);
    u.legacy = 1; u.set_mv = 5000; u.t_dc = 250;
    ui_lines(r0, r1, &u);
    CHECK(!strcmp(r0, " 5V OFF      25C") && !strcmp(r1, "NO PD           "), "[%s][%s]", r0, r1);
}

static void test_fmt(void)
{
    char b[9];
    memset(b, 0, sizeof b);
    fmt_fixed(b, 321, 2, 5); CHECK(!strcmp(b, " 3.21"), "[%s]", b);
    fmt_fixed(b, 5, 1, 4);   b[4] = 0; CHECK(!strcmp(b, " 0.5"), "[%s]", b);
    fmt_fixed(b, 210, 1, 4); b[4] = 0; CHECK(!strcmp(b, "21.0"), "[%s]", b);
    fmt_fixed(b, -15, 0, 4);  b[4] = 0; CHECK(!strcmp(b, " -15"), "[%s]", b);
}

int main(void)
{
    test_pdos();
    test_codes();
    test_adjust();
    test_div();
    test_thermal();
    test_settings();
    test_ui();
    test_fmt();
    printf(fails ? "%d FAILED\n" : "all logic tests passed\n", fails);
    return fails ? 1 : 0;
}
