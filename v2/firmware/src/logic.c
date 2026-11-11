#include "logic.h"

void pdo_parse(const uint16_t XDATA *words, struct pdos XDATA *p)
{
    uint8_t i, n;
    uint16_t w;
    p->nfix = 0;
    p->npps = 0;
    for (i = 0; i < NPDO; i++) {
        w = words[i];
        if (!(w & 0x8000))
            continue;                                  /* not detected */
        if (w & 0x4000) {                              /* SPR APDO = PPS */
            n = p->npps++;
            p->pps_idx[n] = (uint8_t)(i + 1);
            p->pps_max_mv[n] = (uint16_t)((w & 0xFF) * 100u);
            /* V_min code 1 = 3.3 V, 2 = above 3.3 V up to 5 V; anything else: assume 5 V and let the AP33772S
               reject a request it cannot serve (PD_MSGRLT) */
            p->pps_min_mv[n] = ((w >> 8) & 3) == 1 ? 3300u : 5000u;
            p->pps_imax[n] = (uint8_t)((w >> 10) & 0x0F);
        } else {
            n = p->nfix++;
            p->fix_idx[n] = (uint8_t)(i + 1);
            p->fix_mv[n] = (uint16_t)((w & 0xFF) * 100u);
            p->fix_imax[n] = (uint8_t)((w >> 10) & 0x0F);
        }
    }
}

uint16_t code_ma(uint8_t c)
{
    if (c >= 15)
        return 5000;
    return (uint16_t)(1000u + (uint16_t)c * 250u);
}

uint8_t ocp_thr(uint8_t c)
{
    if (c >= 15)
        return 100;                                    /* 5.00 A / 50 mA */
    return (uint8_t)(20 + c * 5);                      /* (1000 + 250 c) / 50 */
}

uint8_t pps_range(const struct pdos XDATA *p, uint16_t *lo, uint16_t *hi)
{
    uint8_t i;
    uint16_t l = 0xFFFF, h = 0;
    for (i = 0; i < p->npps; i++) {
        if (p->pps_min_mv[i] < l) l = p->pps_min_mv[i];
        if (p->pps_max_mv[i] > h) h = p->pps_max_mv[i];
    }
    if (l < PPS_MIN_MV) l = PPS_MIN_MV;
    if (h > PPS_MAX_MV) h = PPS_MAX_MV;
    if (!p->npps || h < l)
        return 0;
    *lo = l;
    *hi = h;
    return 1;
}

uint8_t pps_pick(const struct pdos XDATA *p, uint16_t mv)
{
    uint8_t i, best = 0xFF;
    for (i = 0; i < p->npps; i++) {
        if (mv < p->pps_min_mv[i] || mv > p->pps_max_mv[i])
            continue;
        if (best == 0xFF || p->pps_imax[i] > p->pps_imax[best])
            best = i;
    }
    return best;
}

uint16_t pps_step_mv(uint16_t held_ms)
{
    if (held_ms >= 2500)
        return 1000;
    if (held_ms >= 1000)
        return 500;
    return 100;
}

uint16_t pps_adjust(uint16_t mv, int8_t dir, uint16_t step, uint16_t lo, uint16_t hi)
{
    if (dir > 0)
        mv = (uint16_t)(mv + step > hi ? hi : mv + step);
    else
        mv = (uint16_t)(mv < lo + step ? lo : mv - step);
    if (mv < lo) mv = lo;
    if (mv > hi) mv = hi;
    return mv;
}

uint16_t req_fixed(uint8_t pdo_index)
{
    return (uint16_t)(((uint16_t)pdo_index << 12) | 0x0FFF);
}

uint16_t req_pps(uint8_t pdo_index, uint8_t isel, uint16_t mv)
{
    return (uint16_t)(((uint16_t)pdo_index << 12) | ((uint16_t)(isel & 0x0F) << 8) | (div10(div10(mv)) & 0xFF));
}

uint16_t ap_mv(uint16_t raw) { return (uint16_t)((raw << 6) + (raw << 4)); }
uint16_t ap_ma(uint8_t raw) { return (uint16_t)(((uint16_t)raw << 4) + ((uint16_t)raw << 3)); }

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
int16_t q1_estimate_dc(int16_t ntc_dc, int16_t ma)
{
    uint16_t m = ma < 0 ? (uint16_t)-ma : (uint16_t)ma;
    uint16_t a = (uint16_t)((m + 32u) >> 6);
    if (a > 255)
        a = 255;
    return (int16_t)(ntc_dc + (int16_t)(((mul8((uint8_t)a, (uint8_t)a) >> 5) * Q1_K) >> 4));
}

uint8_t over_temperature(int16_t ntc_dc, int16_t ma)
{
    return (uint8_t)(ntc_dc >= T_TRIP_BOARD_DC || q1_estimate_dc(ntc_dc, ma) >= T_TRIP_Q1_DC);
}

static uint8_t slot_valid(uint8_t s, uint8_t *seq, uint8_t XDATA *d)
{
    uint8_t b = (uint8_t)(s * SLOT_SZ), i, chk;
    if (df_read(b) != 0xA5)
        return 0;
    *seq = df_read((uint8_t)(b + 1));
    chk = (uint8_t)(*seq ^ 0x5A);
    for (i = 0; i < SET_N; i++) {
        d[i] = df_read((uint8_t)(b + 2 + i));
        chk ^= d[i];
    }
    return (uint8_t)(df_read((uint8_t)(b + 2 + SET_N)) == chk);
}

uint8_t settings_load(uint8_t XDATA *d, uint8_t *slot, uint8_t *seq)
{
    static uint8_t XDATA nd[SET_N];
    uint8_t s, sq, nsq;
    /* newest = a valid slot whose successor is not the next record in sequence */
    for (s = 0; s < SLOT_N; s++) {
        if (!slot_valid(s, &sq, d))
            continue;
        if (slot_valid((uint8_t)((s + 1) % SLOT_N), &nsq, nd) && nsq == (uint8_t)(sq + 1))
            continue;
        *slot = s;
        *seq = sq;
        return 1;
    }
    return 0;
}

void settings_save(const uint8_t XDATA *d, uint8_t *slot, uint8_t *seq)
{
    uint8_t s = (uint8_t)((*slot + 1) % SLOT_N), sq = (uint8_t)(*seq + 1);
    uint8_t b = (uint8_t)(s * SLOT_SZ), i, chk = (uint8_t)(sq ^ 0x5A);
    df_write(b, 0x00);                       /* invalidate first: a torn write never looks valid */
    df_write((uint8_t)(b + 1), sq);
    for (i = 0; i < SET_N; i++) {
        df_write((uint8_t)(b + 2 + i), d[i]);
        chk ^= d[i];
    }
    df_write((uint8_t)(b + 2 + SET_N), chk);
    df_write(b, 0xA5);                       /* commit */
    *slot = s;
    *seq = sq;
}

uint8_t fmt_fixed(char XDATA *out, int16_t value, uint8_t dp, uint8_t width)
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

static void put(char XDATA *r, uint8_t col, const char CODE *s)
{
    while (*s)
        r[col++] = *s++;
}

void ui_lines(char XDATA *r0, char XDATA *r1, const struct ui XDATA *u)
{
    uint8_t i;
    uint16_t v;
    for (i = 0; i < 16; i++) { r0[i] = ' '; r1[i] = ' '; }
    r0[16] = 0;
    r1[16] = 0;

    /* row 0: voltage, on / off, NTC temperature */
    if (u->pps) {
        /* the setpoint while adjusting (and while off); the measured voltage ~1 s after the last press */
        v = (u->show_set || !u->out_on) ? u->set_mv : (uint16_t)(u->mv + 50u);
        if (!u->edit_i) r0[0] = '>';                          /* the value UP / DOWN moves */
        fmt_fixed(r0 + 1, (int16_t)div10(div10(v)), 1, 4);
        r0[5] = 'V';
        if (u->out_on) put(r0, 8, "ON"); else put(r0, 7, "OFF");
    } else {
        fmt_fixed(r0, (int16_t)div10(div10(div10(u->set_mv))), 0, 2);
        r0[2] = 'V';
        if (u->out_on) put(r0, 5, "ON"); else put(r0, 4, "OFF");
    }
    fmt_fixed(r0 + 11, sdiv10(u->t_dc), 0, 4);
    r0[15] = 'C';

    /* row 1: why the output is off, or current and power / limit */
    switch (u->status) {
    case ST_HOT:    put(r1, 0, "HOT"); return;
    case ST_OCP:    put(r1, 0, "OVER I"); return;
    case ST_OVP:    put(r1, 0, "OVER V"); return;
    case ST_UVP:    put(r1, 0, "UNDER V"); return;
    case ST_FAIL:   put(r1, 0, "PD ERR"); return;
    case ST_DENIED: put(r1, 0, "DENIED"); return;
    default: break;
    }
    if (u->pps) {
        if (u->out_on) {
            fmt_fixed(r1, sdiv10(u->ma), 2, 5);                 /* 0.01 A */
            r1[5] = 'A';
        }
        put(r1, 7, "LIM");
        fmt_fixed(r1 + 10, (int16_t)div10(u->lim_ma), 2, 5);
        r1[15] = 'A';
        if (u->edit_i) r1[10] = '>';
    } else if (u->out_on) {
        fmt_fixed(r1, sdiv10(u->ma), 2, 5);
        r1[5] = 'A';
        fmt_fixed(r1 + 8, (int16_t)power_dw(u->mv, u->ma), 1, 6);   /* 0.1 W */
        r1[14] = 'W';
    } else if (u->legacy) {
        put(r1, 0, "NO PD");
    } else if (u->lim_ma) {
        put(r1, 0, "MAX");                                     /* what the charger allows at this voltage */
        fmt_fixed(r1 + 4, (int16_t)div10(u->lim_ma), 2, 5);
        r1[9] = 'A';
    }
}
