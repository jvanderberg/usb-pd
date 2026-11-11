/* Host tests for src/logic.c (the board-independent part of the firmware). `make test` */
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

static void test_voltages(void)
{
    /* offered: 5, 9, 15, 20 (bits 0, 1, 3, 5) */
    uint8_t off = 0x01 | 0x02 | 0x08 | 0x20;
    CHECK(next_offered(0, off) == 1, "5 -> 9");
    CHECK(next_offered(1, off) == 3, "9 -> 15 (12 not offered)");
    CHECK(next_offered(3, off) == 5, "15 -> 20 (18 not offered)");
    CHECK(next_offered(5, off) == 0, "20 wraps to 5");
    CHECK(next_offered(0, 0x01) == 0, "only 5 V offered: stays");
    /* PD_STATUS0 voltage codes are sequential; PDO_SELECT codes are not (15 V = 1000b) */
    CHECK(status_volt_index(0x40) == 3, "PD_STATUS0 0100 = 15 V");
    CHECK(status_volt_index(0x00) == 0xFF, "unattached");
    CHECK(status_volt_index(0x70) == 0xFF, "reserved");
    CHECK(volt_sel[3] == 0x8 && volt_sel[5] == 0xA, "PDO_SELECT 15 V = 1000b, 20 V = 1010b");
}

static void test_ntc(void)
{
    /* table points: 25 C = 209 counts, 85 C = 103 (gen_ntc_table.py) */
    CHECK(ntc_decic(209) == 250, "25 C got %d", ntc_decic(209));
    CHECK(ntc_decic(103) == 850, "85 C got %d", ntc_decic(103));
    CHECK(ntc_decic(255) == -200, "open/short low end clamps");
    CHECK(ntc_decic(0) == 1250, "high end clamps, got %d", ntc_decic(0));
    {
        int c, prev = -10000, mono = 1;
        for (c = 255; c >= 0; c--) {
            int t = ntc_decic((uint8_t)c);
            if (t < prev) mono = 0;
            prev = t;
        }
        CHECK(mono, "temperature rises as counts fall");
    }
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
}

static void test_ina(void)
{
    CHECK(ina_current_ma(10000) == 5000, "25 mV over 5 mOhm = 5 A");
    CHECK(ina_current_ma(-200) == -100, "negative current");
    CHECK(ina_current_ma(32767) == 16383, "INA226 full scale fits");
    CHECK(ina_bus_mv(16000) == 20000, "16000 x 1.25 mV = 20 V");
    CHECK(ina_bus_mv(0x7FFF) == 40958, "max raw does not overflow");
    CHECK(power_dw(20000, -5) == 0, "no negative power");
    {
        /* against the exact value over the whole range: within 0.5 % + 0.2 W */
        long mv, ma, worst = 0;
        for (mv = 0; mv <= 40000; mv += 97)
            for (ma = 0; ma <= 16383; ma += 61) {
                long exact = mv * ma / 100000, got = power_dw((uint16_t)mv, (int16_t)ma);
                long err = got > exact ? got - exact : exact - got;
                if (err > exact / 200 + 2 && err > worst) worst = err;
            }
        CHECK(worst == 0, "power_dw off by %ld dW", worst);
        printf("power: 20 V x 5 A -> %u dW, 20.012 V x 3.217 A -> %u dW (exact 643)\n",
               power_dw(20000, 5000), power_dw(20012, 3217));
    }
}

static void test_thermal(void)
{
    {
        /* never below the exact I^2 law, at most 6 % + 0.3 K above it */
        int ma, low = 0, high = 0;
        for (ma = 0; ma <= 16383; ma += 7) {
            double exact = Q1_RISE_5A_DC * ((double)ma / 5000.0) * ((double)ma / 5000.0);
            int got = q1_estimate_dc(0, (int16_t)ma);
            if (ma > 255 * 64 - 32) exact = Q1_RISE_5A_DC * (255.0 * 64 / 5000) * (255.0 * 64 / 5000);  /* clamp */
            if (got < exact - 3.5) low++;            /* truncation in a: within a few tenths of a K */
            if (got > exact * 1.06 + 3) high++;
        }
        CHECK(!low && !high, "Q1 rise estimate: %d too low, %d too high", low, high);
        printf("Q1 rise at 5 A: %d (exact 250)\n", q1_estimate_dc(0, 5000));
    }
    CHECK(q1_estimate_dc(500, 0) == 500, "0 A adds nothing");
    CHECK(q1_estimate_dc(500, -5000) == q1_estimate_dc(500, 5000), "sign does not matter");
    CHECK(q1_estimate_dc(500, 16383) > q1_estimate_dc(500, 8191), "full scale does not overflow");
    CHECK(!over_temperature(600, 3000), "60 C at 3 A runs");
    CHECK(over_temperature(850, 0), "board at 85 C trips");
    CHECK(over_temperature(860, 5000), "86 C board trips");
    CHECK(!over_temperature(830, 5000), "83 C + ~26 K: under both trips");
    CHECK(over_temperature(849, 5000), "84.9 C + ~26 K: the Q1 estimate trips (it errs high)");
    CHECK(over_temperature(700, 6400), "70 C board + 41 K at 6.4 A trips on the Q1 estimate");
}

static void test_settings(void)
{
    uint8_t d, slot, seq, i;
    memset(flash, 0xFF, sizeof flash);
    CHECK(!settings_load(&d, &slot, &seq), "blank flash: nothing");
    memset(flash, 0x00, sizeof flash);
    CHECK(!settings_load(&d, &slot, &seq), "zeroed flash: nothing");

    slot = SLOT_N - 1; seq = 0;
    for (i = 0; i < 100; i++)
        settings_save((uint8_t)(i & 0x7F), &slot, &seq);
    CHECK(settings_load(&d, &slot, &seq) && d == 99, "newest after wrap: %u", d);

    /* torn write: magic cleared then power lost -> previous record wins */
    {
        uint8_t s2 = (uint8_t)((slot + 1) % SLOT_N);
        flash[s2 * SLOT_SZ] = 0x00;
        flash[s2 * SLOT_SZ + 1] = (uint8_t)(seq + 1);
        CHECK(settings_load(&d, &slot, &seq) && d == 99, "torn write ignored");
    }
    /* wear: 1000 saves -> each cell written about 1000/32 times (x2 for the magic byte) */
    {
        int i2, maxw = 0;
        memset(writes, 0, sizeof writes);
        for (i2 = 0; i2 < 1000; i2++)
            settings_save((uint8_t)(i2 & 0x7F), &slot, &seq);
        for (i2 = 0; i2 < 128; i2++)
            if (writes[i2] > maxw) maxw = writes[i2];
        CHECK(maxw <= 2 * (1000 / SLOT_N + 1), "max writes per cell %d", maxw);
        printf("wear: 1000 saves -> at most %d writes per cell (10K-cycle cells: ~%d saves)\n",
               maxw, 10000 * 1000 / maxw);
    }
}

static void test_fmt(void)
{
    char b[9];
    memset(b, 0, sizeof b);
    fmt_fixed(b, 321, 2, 5); CHECK(!strcmp(b, " 3.21"), "[%s]", b);
    fmt_fixed(b, 5, 1, 4);   b[4] = 0; CHECK(!strcmp(b, " 0.5"), "[%s]", b);
    fmt_fixed(b, 642, 1, 6); b[6] = 0; CHECK(!strcmp(b, "  64.2"), "[%s]", b);
    fmt_fixed(b, 20, 0, 2);  b[2] = 0; CHECK(!strcmp(b, "20"), "[%s]", b);
    fmt_fixed(b, 1000, 1, 6); b[6] = 0; CHECK(!strcmp(b, " 100.0"), "[%s]", b);
    fmt_fixed(b, -15, 0, 4);  b[4] = 0; CHECK(!strcmp(b, " -15"), "[%s]", b);
    fmt_fixed(b, 32767, 0, 5); b[5] = 0; CHECK(!strcmp(b, "32767"), "[%s]", b);
}

int main(void)
{
    test_voltages();
    test_ntc();
    test_div();
    test_ina();
    test_thermal();
    test_settings();
    test_fmt();
    printf(fails ? "%d FAILED\n" : "all logic tests passed\n", fails);
    return fails ? 1 : 0;
}
