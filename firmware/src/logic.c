#include "logic.h"
#include "ntc_table.h"

const NTC_CODE uint8_t volt_v[NVOLT] = {5, 9, 12, 15, 18, 20};
/* HUSB238 register information rev 1.1, table 10: 0001 5V, 0010 9V, 0011 12V, 1000 15V, 1001 18V, 1010 20V */
const NTC_CODE uint8_t volt_sel[NVOLT] = {0x1, 0x2, 0x3, 0x8, 0x9, 0xA};
/* tables 2 / 4-9: 0.5 0.7 1 1.25 1.5 1.75 2 2.25 2.5 2.75 3 3.25 3.5 4 4.5 5 A */
const NTC_CODE uint16_t pdo_current_ma[16] = {500, 700, 1000, 1250, 1500, 1750, 2000, 2250,
                                              2500, 2750, 3000, 3250, 3500, 4000, 4500, 5000};

uint8_t next_offered(uint8_t cur, uint8_t offered)
{
    uint8_t i, n = cur;
    for (i = 0; i < NVOLT; i++) {
        n = (uint8_t)((n + 1) % NVOLT);
        if (offered & (uint8_t)(1u << n))
            return n;
    }
    return cur;
}

uint8_t status_volt_index(uint8_t pd_status0)
{
    /* table 2 PD_SRC_VOLTAGE: 0001 5V, 0010 9V, 0011 12V, 0100 15V, 0101 18V, 0110 20V (NOT the PDO_SELECT codes) */
    uint8_t v = pd_status0 >> 4;
    if (v >= 1 && v <= 6)
        return (uint8_t)(v - 1);
    return 0xFF;
}

int16_t ntc_decic(uint8_t c)
{
    uint8_t i;
    if (c >= ntc_counts[0])
        return NTC_T_MIN_C * 10;
    for (i = 1; i < NTC_N; i++) {
        if (c >= ntc_counts[i]) {
            /* between entry i-1 (warmer count, colder temp) and i */
            uint8_t span = (uint8_t)(ntc_counts[i - 1] - ntc_counts[i]);
            uint8_t into = (uint8_t)(ntc_counts[i - 1] - c);
            return (int16_t)((NTC_T_MIN_C + (int16_t)(i - 1) * NTC_T_STEP_C) * 10 +
                             (int16_t)((uint16_t)(into * (uint8_t)(NTC_T_STEP_C * 10)) / span));
        }
    }
    return (int16_t)((NTC_T_MIN_C + (NTC_N - 1) * NTC_T_STEP_C) * 10);
}

uint16_t div10(uint16_t n)
{
    /* q ~ n * 0.8 / 8 by shifts (Hacker's Delight divu10), then one correction step: exact for all 16-bit n */
    uint16_t q = (uint16_t)((n >> 1) + (n >> 2));
    q += q >> 4;
    q += q >> 8;
    q >>= 3;
    if ((uint16_t)(n - (uint16_t)((q << 3) + (q << 1))) > 9)
        q++;
    return q;
}

int16_t sdiv10(int16_t n)
{
    return n < 0 ? (int16_t)-(int16_t)div10((uint16_t)-n) : (int16_t)div10((uint16_t)n);
}

/* 8x8 -> 16: one MUL AB on the 8051 */
static uint16_t mul8(uint8_t a, uint8_t b) { return (uint16_t)((uint16_t)a * b); }

int16_t ina_current_ma(int16_t shunt_raw)
{
    return shunt_raw < 0 ? (int16_t)-(int16_t)((uint16_t)-shunt_raw >> 1) : (int16_t)((uint16_t)shunt_raw >> 1);
}

uint16_t ina_bus_mv(uint16_t bus_raw) { return (uint16_t)(bus_raw + (bus_raw >> 2)); }

uint16_t power_dw(uint16_t mv, int16_t ma)
{
    /* P[0.1 W] = mv * ma / 100,000. hi = (mv * ma) >> 16 from three 8x8 products (the low x low one only reaches
       the discarded bits), then x 0.65625 = (1 + 1/4 + 1/16) / 2 (100,000 / 65,536 = 1 / 0.65536) */
    uint8_t mh, ml, ah, al;
    uint16_t hi;
    if (ma <= 0)
        return 0;
    mh = (uint8_t)(mv >> 8); ml = (uint8_t)mv;
    ah = (uint8_t)((uint16_t)ma >> 8); al = (uint8_t)ma;
    hi = (uint16_t)(mul8(mh, ah) + (((mul8(mh, al) >> 1) + (mul8(ml, ah) >> 1) + 64u) >> 7));
    return (uint16_t)((hi + (hi >> 2) + (hi >> 4) + 1u) >> 1);
}

/* rise ~ I^2: Q1_RISE_5A_DC x (I / 5 A)^2. a = |I| in 64 mA steps (5 A = 78), a^2 by one MUL; 5 A gives
   a^2 >> 5 = 190, so the scale is Q1_RISE_5A_DC x 16 / 190, rounded up (the estimate errs high) */
#define Q1_K ((Q1_RISE_5A_DC * 16u + 189u) / 190u)
int16_t q1_estimate_dc(int16_t board_dc, int16_t ma)
{
    uint16_t m = ma < 0 ? (uint16_t)-ma : (uint16_t)ma;
    uint16_t a = (uint16_t)((m + 32u) >> 6);
    if (a > 255)
        a = 255;
    return (int16_t)(board_dc + (int16_t)(((mul8((uint8_t)a, (uint8_t)a) >> 5) * Q1_K) >> 4));
}

uint8_t over_temperature(int16_t board_dc, int16_t ma)
{
    return (uint8_t)(board_dc >= T_TRIP_BOARD_DC || q1_estimate_dc(board_dc, ma) >= T_TRIP_Q1_DC);
}

static uint8_t slot_valid(uint8_t s, uint8_t *seq, uint8_t *data)
{
    uint8_t b = (uint8_t)(s * SLOT_SZ);
    if (df_read(b) != 0xA5)
        return 0;
    *seq = df_read((uint8_t)(b + 1));
    *data = df_read((uint8_t)(b + 2));
    return (uint8_t)(df_read((uint8_t)(b + 3)) == (uint8_t)(*seq ^ *data ^ 0x5A));
}

uint8_t settings_load(uint8_t *data, uint8_t *slot, uint8_t *seq)
{
    uint8_t s, sq, d, nsq, nd;
    /* newest = a valid slot whose successor is not the next record in sequence */
    for (s = 0; s < SLOT_N; s++) {
        if (!slot_valid(s, &sq, &d))
            continue;
        if (slot_valid((uint8_t)((s + 1) % SLOT_N), &nsq, &nd) && nsq == (uint8_t)(sq + 1))
            continue;
        *data = d;
        *slot = s;
        *seq = sq;
        return 1;
    }
    return 0;
}

void settings_save(uint8_t data, uint8_t *slot, uint8_t *seq)
{
    uint8_t s = (uint8_t)((*slot + 1) % SLOT_N), sq = (uint8_t)(*seq + 1);
    uint8_t b = (uint8_t)(s * SLOT_SZ);
    df_write(b, 0x00);                       /* invalidate first: a torn write never looks valid */
    df_write((uint8_t)(b + 1), sq);
    df_write((uint8_t)(b + 2), data);
    df_write((uint8_t)(b + 3), (uint8_t)(sq ^ data ^ 0x5A));
    df_write(b, 0xA5);                       /* commit */
    *slot = s;
    *seq = sq;
}

uint8_t fmt_fixed(char *out, int16_t value, uint8_t dp, uint8_t width)
{
    static XDATA char tmp[8];
    uint8_t n = 0, i, neg = 0;
    uint16_t v, q;
    if (value < 0) {
        neg = 1;
        v = (uint16_t)-value;
    } else {
        v = (uint16_t)value;
    }
    do {
        q = div10(v);
        tmp[n++] = (char)('0' + (uint8_t)(v - (uint16_t)((q << 3) + (q << 1))));
        v = q;
        if (dp && n == dp)
            tmp[n++] = '.';
    } while (v || (dp && n <= dp + 1));     /* keep the leading 0 of 0.xx */
    if (neg)
        tmp[n++] = '-';
    for (i = 0; i < width; i++)
        out[i] = ' ';
    if (n > width)
        n = width;
    for (i = 0; i < n; i++)
        out[width - 1 - i] = tmp[i];
    return width;
}

void ui_lines(char XDATA *r0, char XDATA *r1, const struct ui XDATA *u)
{
    uint8_t i;
    for (i = 0; i < 16; i++) { r0[i] = ' '; r1[i] = ' '; }
    r0[16] = 0;
    r1[16] = 0;
    fmt_fixed(r0, volt_v[u->vidx], 0, 2);
    r0[2] = 'V';
    if (u->status == ST_NOPD) { r0[4] = 'N'; r0[5] = 'O'; r0[7] = 'P'; r0[8] = 'D'; }
    else if (u->out_on) { r0[5] = 'O'; r0[6] = 'N'; }
    else { r0[4] = 'O'; r0[5] = 'F'; r0[6] = 'F'; }
    fmt_fixed(r0 + 11, sdiv10(u->t_dc), 0, 4);
    r0[15] = 'C';
    if (u->status == ST_HOT) {
        r1[0] = 'H'; r1[1] = 'O'; r1[2] = 'T';
    } else if (u->status == ST_OVERCURRENT) {
        r1[0] = 'O'; r1[1] = 'V'; r1[2] = 'E'; r1[3] = 'R'; r1[5] = 'I';
    } else if (!u->ina_ok) {
        r1[0] = 'I'; r1[1] = 'N'; r1[2] = 'A'; r1[4] = '-';
    } else if (u->out_on) {
        fmt_fixed(r1, sdiv10(u->ma), 2, 5);                     /* 0.01 A */
        r1[5] = 'A';
        fmt_fixed(r1 + 8, (int16_t)power_dw(u->mv, u->ma), 1, 6);   /* 0.1 W */
        r1[14] = 'W';
    } else if (u->max_ma) {
        r1[0] = 'M'; r1[1] = 'A'; r1[2] = 'X';                  /* what the charger allows at this voltage */
        fmt_fixed(r1 + 4, (int16_t)div10(u->max_ma), 2, 5);
        r1[9] = 'A';
    }
}
