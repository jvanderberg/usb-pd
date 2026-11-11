/* USB-PD / PPS supply firmware (CH552P + AP33772S). docs/requirements.md has the behaviour.
 *
 * Boot: inside the AP33772S's 100 ms configuration window, force its output off (SYSTEM = 0x11), disable EPR and
 * automatic de-rating, set the OTP backstop. Read the source's PDOs, restore the saved selection (a fixed voltage or
 * the PPS setpoint), and turn the output on again only if it was on and that contract was obtained.
 * ON/OFF toggles the output. SEL (CYCLE) steps 5V -> 9V -> ... -> 20V -> PPS through what the charger offers, and
 * turns the output off first. In PPS, UP / DOWN move the voltage (100 mV, accelerating while held) or the current
 * limit (one 0.25 A code per press); UP + DOWN together switch between the two. PD requests go out at most every
 * 150 ms, latest value wins. Faults (AP33772S OTP / OCP / OVP / UVP, firmware thermal trip) turn the output off;
 * ON retries with a fresh request. ON + SEL held 5 s: ROM bootloader. */
#include <stdint.h>
#include "hw.h"
#include "logic.h"
#include "oled.h"

void timer0_isr(void) __interrupt(1);    /* SDCC: the ISR prototype must be visible in the file with main() */

/* AP33772S registers (DS table 19) */
#define R_STATUS   0x01
#define R_OPMODE   0x03
#define R_CONFIG   0x04
#define R_PDCONFIG 0x05
#define R_SYSTEM   0x06
#define R_VOLTAGE  0x11
#define R_CURRENT  0x12
#define R_TEMP     0x13
#define R_VREQ     0x14
#define R_OCPTHR   0x19
#define R_OTPTHR   0x1A
#define R_SRCPDO   0x20
#define R_REQMSG   0x31
#define R_MSGRLT   0x33
#define SYS_OFF    0x11     /* VOUTCTL = 1: forced off (CentyLab / hnz1102 drivers; requirements s8) */
#define SYS_ON     0x12     /* VOUTCTL = 2: forced on */
#define CONFIG_V   0x78     /* DS table 10: OTP, OCP, OVP, UVP on; DR_EN (bit 7) off - firmware owns de-rating */
#define PDCONFIG_V 0x02     /* DS table 3: PPS_AVS_EN on, EPR_MODE_EN off - nothing on this board is above 21 V */
#define ST_FAULTS  0x78     /* STATUS OTP | OCP | OVP | UVP (DS table 12) */
#define ST_NEWPDO  0x04
#define OP_PD      0x02
#define OP_LEGACY  0x01

#define UPDATE_MS      100  /* measure / protect / display period */
#define REQ_GAP_MS     150  /* minimum time between PD requests (requirements s3) */
#define REQ_FIRST_MS    30  /* first PD_MSGRLT poll after a request */
#define REQ_TIMEOUT_MS 1500
#define SHOW_SET_MS   1000  /* PPS: show the setpoint this long after the last press, then the measured voltage */
#define SAVE_DELAY_MS 2000
#define PPS_MAX_STEP  1000  /* mV per request: inside UVP (80 % of VREQ at 5 V) and OVP (VREQ + 2 V) while the source slews */

static struct pdos __xdata pd;
static uint16_t __xdata words[13];
static uint8_t __xdata buf[2];
static uint8_t __xdata sv[SET_N], saved[SET_N];

static uint8_t legacy, has_pps;
static uint16_t pps_lo, pps_hi;
static uint8_t sel;                              /* 0 .. nfix-1 fixed PDOs, nfix = PPS */
static uint16_t pps_mv = PPS_MIN_MV;
static uint8_t pps_isel, edit_i;
static uint8_t act_ok, act_sel, act_isel;        /* the last contract the AP33772S confirmed */
static uint16_t act_mv;
static uint8_t req_pending, req_inflight, on_after, req_isel, req_sel;
static uint16_t req_t, req_mv;
static uint8_t out_on, status, fault, hot_lock;
static uint16_t mv, ma_u;
static int16_t t_dc = 250;
static uint8_t log_slot = SLOT_N - 1, log_seq, dirty;
static uint16_t dirty_since, last_press;

static uint8_t ap_w8(uint8_t reg, uint8_t v)
{
    buf[0] = v;
    return ap_write(reg, buf, 1);
}

static void output(uint8_t on)
{
    ap_w8(R_SYSTEM, on ? SYS_ON : SYS_OFF);
    out_on = on;
}

#define IS_PPS(s) ((s) == pd.nfix)
static uint8_t entries(void) { return (uint8_t)(pd.nfix + (has_pps ? 1 : 0)); }

static void mark_dirty(void)
{
    dirty = 1;
    dirty_since = ticks();
}

static void clamp_pps(void)
{
    if (!has_pps) return;                        /* no PPS on this charger: keep the saved setpoint as it is */
    if (pps_mv < pps_lo) pps_mv = pps_lo;
    if (pps_mv > pps_hi) pps_mv = pps_hi;
}

static uint8_t read_pdos(void)
{
    uint8_t i;
    if (ap_read(R_SRCPDO, (uint8_t __xdata *)words, 26))
        return 0;
    for (i = 0; i < NPDO; i++)                   /* little endian on the wire; SDCC stores uint16_t little endian */
        words[i] = (uint16_t)(((uint8_t __xdata *)words)[2 * i] | ((uint16_t)((uint8_t __xdata *)words)[2 * i + 1] << 8));
    pdo_parse(words, &pd);
    has_pps = pps_range(&pd, &pps_lo, &pps_hi);
    clamp_pps();
    if (sel >= entries()) sel = 0;
    return 1;
}


static void send_request(void)
{
    uint16_t w;
    uint8_t k, ocp = 0;
    req_pending = 0;
    if (legacy || !entries())
        return;
    if (IS_PPS(sel)) {
        k = pps_pick(&pd, pps_mv);
        if (k == 0xFF) {                         /* a gap between two PPS ranges: keep the last good setpoint */
            status = ST_DENIED;
            if (act_ok && IS_PPS(act_sel)) pps_mv = act_mv;
            return;
        }
        if (pps_isel > pd.pps_imax[k]) pps_isel = pd.pps_imax[k];
        req_mv = pps_mv;
        if (act_ok && IS_PPS(act_sel)) {         /* big moves go out as 1 V steps (req_pending stays set) */
            if (req_mv > act_mv + PPS_MAX_STEP) { req_mv = act_mv + PPS_MAX_STEP; req_pending = 1; }
            if (req_mv + PPS_MAX_STEP < act_mv) { req_mv = act_mv - PPS_MAX_STEP; req_pending = 1; }
            k = pps_pick(&pd, req_mv);
            if (k == 0xFF) { req_mv = pps_mv; req_pending = 0; k = pps_pick(&pd, pps_mv); }
            if (pps_isel > pd.pps_imax[k]) pps_isel = pd.pps_imax[k];
        }
        w = req_pps(pd.pps_idx[k], pps_isel, req_mv);
        ocp = ocp_thr(pps_isel);                 /* trip at 110 % of the limit (requirements s3) */
    } else {
        w = req_fixed(pd.fix_idx[sel]);          /* OCPTHR 0 = 110 % of the PDO's I_max */
        req_mv = pd.fix_mv[sel];
    }
    ap_w8(R_OCPTHR, ocp);
    buf[0] = (uint8_t)w;                         /* little endian */
    buf[1] = (uint8_t)(w >> 8);
    if (ap_write(R_REQMSG, buf, 2)) {
        status = ST_FAIL;
        on_after = 0;
        req_pending = 0;
        return;
    }
    req_sel = sel;
    req_isel = pps_isel;
    req_inflight = 1;
    req_t = ticks();
}

static void poll_request(void)
{
    uint8_t r = 0;
    uint16_t age = (uint16_t)(ticks() - req_t);
    if (age < REQ_FIRST_MS)
        return;
    if (ap_read(R_MSGRLT, buf, 1)) {
        if (age < REQ_TIMEOUT_MS)
            return;                              /* retry the read next loop */
        r = 4;
    } else {
        r = buf[0];
    }
    if (r == 1) {
        /* PD_MSGRLT may still hold the previous result for a moment: also require VREQ (50 mV/LSB) to match */
        if (!ap_read(R_VREQ, buf, 2)) {
            uint16_t vreq = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
            if ((uint16_t)((vreq << 5) + (vreq << 4) + (vreq << 1)) != req_mv && age < REQ_TIMEOUT_MS)
                return;                          /* x50 = 32 + 16 + 2 */
        }
        req_inflight = 0;
        act_ok = 1;
        act_sel = req_sel;
        act_mv = req_mv;
        act_isel = req_isel;
        fault = 0;
        if (status == ST_DENIED || status == ST_FAIL) status = ST_OK;
        if (on_after && !hot_lock) output(1);
        on_after = 0;
        return;
    }
    if (r == 0 && age < REQ_TIMEOUT_MS)
        return;                                  /* busy */
    /* invalid / unsupported / failed / timed out: back to what we have */
    req_inflight = 0;
    on_after = 0;
    req_pending = 0;
    status = ST_DENIED;
    if (act_ok && sel == req_sel) {              /* the user has not moved on: show what we really have */
        sel = act_sel;
        if (IS_PPS(sel)) { pps_mv = act_mv; pps_isel = act_isel; }
    }
}

static void show(void)
{
    static __xdata char r0[17], r1[17];
    static __xdata struct ui u;
    u.pps = (uint8_t)(!legacy && has_pps && IS_PPS(sel));
    u.legacy = legacy;
    u.status = status;
    u.out_on = out_on;
    u.edit_i = edit_i;
    u.show_set = (uint8_t)((uint16_t)(ticks() - last_press) < SHOW_SET_MS);
    if (legacy || !entries()) {
        u.set_mv = 5000;
        u.lim_ma = 0;
    } else if (u.pps) {
        u.set_mv = pps_mv;
        u.lim_ma = code_ma(pps_isel);
    } else {
        u.set_mv = pd.fix_mv[sel];
        u.lim_ma = code_ma(pd.fix_imax[sel]);
    }
    u.mv = mv;
    u.ma = (int16_t)ma_u;
    u.t_dc = t_dc;
    ui_lines(r0, r1, &u);
    oled_clear();
    oled_text(0, 0, r0);
    oled_text(1, 0, r1);
    oled_flush();
}

/* ---------------------------------------------------------------- boot */
static void boot(void)
{
    uint8_t i, op = 0;

    /* 1. output off, before anything else (the AP33772S accepts configuration for 100 ms after it starts) */
    for (i = 0; i < 100; i++) {
        WDOG_FEED();
        if (!ap_w8(R_SYSTEM, SYS_OFF))
            break;
        delay_ms(2);
    }
    ap_w8(R_PDCONFIG, PDCONFIG_V);
    ap_w8(R_CONFIG, CONFIG_V);
    ap_w8(R_OTPTHR, OTP_THR_C);
    out_on = 0;

    delay_ms(50);                                /* OLED VDD stable */
    oled_init();

    /* 2. wait for the PD contract (or a legacy source): PD takes 100s of ms */
    for (i = 0; i < 120; i++) {
        WDOG_FEED();
        if (!ap_read(R_OPMODE, buf, 1)) {
            op = buf[0];
            if (op & (OP_PD | OP_LEGACY))
                break;
        }
        delay_ms(25);
    }
    ap_read(R_STATUS, buf, 1);                   /* read-to-clear whatever happened during attach */
    legacy = (uint8_t)(!(op & OP_PD) && (op & OP_LEGACY));
    if (!(op & (OP_PD | OP_LEGACY)))
        status = ST_FAIL;
    if (op & OP_PD)
        read_pdos();

    /* 3. restore the saved selection */
    if (settings_load(sv, &log_slot, &log_seq)) {
        for (i = 0; i < SET_N; i++) saved[i] = sv[i];
        edit_i = (uint8_t)((sv[0] & SET_EDI) ? 1 : 0);
        pps_isel = (uint8_t)(sv[3] & 0x0F);
        pps_mv = (uint16_t)(sv[2] * 100u);
        clamp_pps();
        sel = 0;
        if ((sv[0] & SET_PPS) && has_pps) {
            sel = pd.nfix;
        } else {
            for (i = 0; i < pd.nfix; i++)
                if (pd.fix_mv[i] == (uint16_t)(sv[1] * 100u))
                    sel = i;
        }
        /* back on only if the saved choice is what we get */
        on_after = (uint8_t)((sv[0] & SET_ON) && (IS_PPS(sel) ? (sv[0] & SET_PPS) : pd.fix_mv[sel] == (uint16_t)(sv[1] * 100u)));
    }
    if (legacy) {
        if (on_after) output(1);
        on_after = 0;
    } else if (op & OP_PD) {
        send_request();
    }
}

static void save_settings(void)
{
    uint8_t i, diff = 0;
    sv[0] = (uint8_t)((out_on ? SET_ON : 0) | (IS_PPS(sel) && has_pps ? SET_PPS : 0) | (edit_i ? SET_EDI : 0));
    sv[1] = saved[1];
    if (!legacy && pd.nfix && !IS_PPS(sel))
        sv[1] = (uint8_t)div10(div10(pd.fix_mv[sel]));
    sv[2] = (uint8_t)div10(div10(pps_mv));
    sv[3] = pps_isel;
    for (i = 0; i < SET_N; i++)
        if (sv[i] != saved[i]) diff = 1;
    if (diff) {
        settings_save(sv, &log_slot, &log_seq);
        for (i = 0; i < SET_N; i++) saved[i] = sv[i];
    }
}

/* one UP / DOWN step: voltage (accelerating) or current code */
static void step(int8_t dir, uint16_t held_ms)
{
    uint8_t k, imax = 15;
    if (edit_i) {
        k = pps_pick(&pd, pps_mv);
        if (k != 0xFF) imax = pd.pps_imax[k];
        if (dir > 0 && pps_isel < imax) pps_isel++;
        if (dir < 0 && pps_isel > 0) pps_isel--;
    } else {
        pps_mv = pps_adjust(pps_mv, dir, pps_step_mv(held_ms), pps_lo, pps_hi);
    }
    last_press = ticks();
    if (status == ST_DENIED) status = ST_OK;
    req_pending = 1;
    mark_dirty();
}

void main(void)
{
    uint8_t ev, held, combo = 0, rep_dir = 0;
    uint16_t last_meas = 0, both_since = 0, press_t = 0, rep_t = 0, prev_mv = 0, last_req = 0;
    uint8_t prev_isel = 0, fails = 0;

    hw_init();
    boot();
    last_press = (uint16_t)(ticks() - SHOW_SET_MS);
    show();

    for (;;) {
        WDOG_FEED();
        delay_ms(10);
        ev = buttons_take();
        held = buttons_held();

        /* ------------------------------------------------------------ ON + SEL 5 s: bootloader */
        if ((held & (BTN_EV_ON | BTN_EV_CYC)) == (BTN_EV_ON | BTN_EV_CYC)) {
            if (!both_since) both_since = ticks() | 1;
            else if ((uint16_t)(ticks() - both_since) > 5000) {
                output(0);                       /* stays off in the AP33772S while the ROM bootloader runs */
                jump_to_bootloader();
            }
            ev &= (uint8_t)~(BTN_EV_ON | BTN_EV_CYC);
        } else {
            both_since = 0;
        }

        /* ------------------------------------------------------------ ON / OFF */
        if (ev & BTN_EV_ON) {
            if (out_on) {
                output(0);
            } else if (!hot_lock && status != ST_FAIL) {
                status = ST_OK;
                if (legacy) output(1);
                else if (fault || !act_ok || act_sel != sel || req_inflight || req_pending ||
                         (IS_PPS(sel) && (act_mv != pps_mv || act_isel != pps_isel))) {
                    on_after = 1;                /* a fault needs a fresh request first (DS "VOUT-Enable Switch") */
                    if (!req_inflight) send_request();
                } else {
                    output(1);
                }
            }
            mark_dirty();
            show();                              /* answer the press now, not at the next 100 ms tick */
        }

        /* ------------------------------------------------------------ SEL: next offered entry, output off first */
        if ((ev & BTN_EV_CYC) && !legacy && entries() > 1) {
            output(0);
            on_after = 0;
            sel = (uint8_t)((sel + 1) % entries());
            if (status != ST_HOT) status = ST_OK;
            last_press = (uint16_t)(ticks() - SHOW_SET_MS);
            req_pending = 1;
            last_req = (uint16_t)(ticks() - REQ_GAP_MS);   /* send at once */
            mark_dirty();
            show();
        }

        /* ------------------------------------------------------------ UP / DOWN (PPS only) */
        if (!legacy && has_pps && IS_PPS(sel)) {
            if ((held & (BTN_EV_UP | BTN_EV_DN)) == (BTN_EV_UP | BTN_EV_DN)) {
                if (!combo) {                    /* both: switch V <-> I, and undo the first button's single step */
                    combo = 1;
                    pps_mv = prev_mv;
                    pps_isel = prev_isel;
                    edit_i ^= 1;
                    last_press = ticks();
                    req_pending = 1;
                    mark_dirty();
                    show();
                }
                rep_dir = 0;
            } else if (!(held & (BTN_EV_UP | BTN_EV_DN))) {
                combo = 0;
                rep_dir = 0;
            } else if (!combo) {
                if (ev & (BTN_EV_UP | BTN_EV_DN)) {
                    prev_mv = pps_mv;
                    prev_isel = pps_isel;
                    rep_dir = (ev & BTN_EV_UP) ? 1 : 0xFF;
                    press_t = rep_t = ticks();
                    step((int8_t)rep_dir, 0);
                    show();
                } else if (rep_dir && !edit_i) {
                    uint16_t h = (uint16_t)(ticks() - press_t);
                    if (h >= REPEAT_START_MS && (uint16_t)(ticks() - rep_t) >= REPEAT_EVERY_MS) {
                        rep_t = ticks();
                        step((int8_t)rep_dir, h);
                        show();
                    }
                }
            }
        }

        /* ------------------------------------------------------------ PD requests: one at a time, latest wins */
        if (req_inflight) {
            poll_request();
        } else if (req_pending && (uint16_t)(ticks() - last_req) >= REQ_GAP_MS) {
            last_req = ticks();
            send_request();
        }

        /* ------------------------------------------------------------ measure, protect, display (10 Hz) */
        if ((uint16_t)(ticks() - last_meas) >= UPDATE_MS) {
            last_meas = ticks();
            if (ap_read(R_STATUS, buf, 1)) {
                if (++fails >= 5) {              /* the AP33772S stopped answering */
                    status = ST_FAIL;
                    out_on = 0;
                }
            } else {
                fails = 0;
                if (status == ST_FAIL && !req_inflight) status = ST_OK;
                if (buf[0] & ST_FAULTS) {        /* the AP33772S has opened Q1 (FAULT state) */
                    fault = 1;
                    if (out_on || on_after) {
                        status = (buf[0] & 0x40) ? ST_HOT : (buf[0] & 0x20) ? ST_OCP :
                                 (buf[0] & 0x10) ? ST_OVP : ST_UVP;
                    }
                    output(0);
                    on_after = 0;
                }
                if (buf[0] & ST_NEWPDO) {        /* the source changed its capabilities */
                    output(0);
                    read_pdos();
                    act_ok = 0;
                }
                if (!legacy && !entries() && !ap_read(R_OPMODE, buf, 1)) {   /* a contract that came late */
                    if (buf[0] & OP_PD) {
                        if (read_pdos()) send_request();
                    } else if (buf[0] & OP_LEGACY) {
                        legacy = 1;
                    }
                }
                if (!ap_read(R_VOLTAGE, buf, 2)) mv = ap_mv((uint16_t)(buf[0] | ((uint16_t)buf[1] << 8)));
                if (!ap_read(R_CURRENT, buf, 1)) ma_u = ap_ma(buf[0]);
                if (!ap_read(R_TEMP, buf, 1)) t_dc = (int16_t)((int16_t)buf[0] * 10);
            }
            if (!out_on) ma_u = 0;
            if (out_on && over_temperature(t_dc, (int16_t)ma_u)) {
                output(0);
                status = ST_HOT;
                hot_lock = 1;
            }
            if (hot_lock && t_dc < T_RESUME_DC) {
                hot_lock = 0;
                if (status == ST_HOT) status = ST_OK;
            }
            show();
        }

        /* ------------------------------------------------------------ save late, only on change */
        if (dirty && (uint16_t)(ticks() - dirty_since) >= SAVE_DELAY_MS) {
            dirty = 0;
            save_settings();
        }
    }
}
