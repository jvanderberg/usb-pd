/* USB-PD trigger board firmware (CH552P). docs/requirements.md has the behaviour; docs/design-review.md the limits.
 *
 * Power-up: HUSB238 has already negotiated 5 V (VSET = GND). Read the source's PDOs, restore the saved voltage if
 * the charger offers it, and restore the saved ON state only when that voltage was actually obtained.
 * ON/OFF toggles the output. CYCLE steps to the next OFFERED voltage (never one the charger did not list) and turns
 * the output off first. The output trips off on over-temperature (NTC + I^2 Q1 estimate) and on current above the
 * contract. Both buttons held 5 s: jump to the ROM bootloader (fallback to holding CYCLE at plug-in). */
#include <stdint.h>
#include "hw.h"
#include "logic.h"
#include "oled.h"

void timer0_isr(void) __interrupt(1);    /* SDCC: the ISR prototype must be visible in the file with main() */

#define HUSB_ADDR 0x08
#define INA_ADDR  0x40
#define UPDATE_MS 100      /* measure / protect / display period */
#define INA_CFG   0x4527   /* AVG 16, VBUSCT 1.1 ms, VSHCT 1.1 ms, shunt + bus continuous (INA226 DS 7.1.1) */

#define SAVE_DELAY_MS  3000
#define OC_TRIP_MS      500

static uint8_t offered;           /* bit i: voltage index i offered by the source */
static uint8_t pdo_code[NVOLT];   /* 4-bit current code per offered voltage */
static uint8_t vidx;              /* selected voltage index */
static uint8_t contract_idx;      /* HUSB238 reported contract, 0xFF = none */
static uint16_t contract_ma;      /* current the contract allows */
static uint8_t out_on;
static uint8_t status;
static uint8_t ina_ok;
static uint8_t log_slot = SLOT_N - 1, log_seq;
static uint8_t saved_data, dirty;
static uint16_t dirty_since;

static void set_output(uint8_t on)
{
    out_on = on;
    SW_EN = on ? 1 : 0;
}

static void read_pdos(void)
{
    uint8_t i, r;
    offered = 0;
    for (i = 0; i < NVOLT; i++) {
        if (i2c_read_reg8(HUSB_ADDR, (uint8_t)(0x02 + i), &r))
            continue;
        if (r & 0x80) {
            offered |= (uint8_t)(1u << i);
            pdo_code[i] = (uint8_t)(r & 0x0F);
        }
    }
}

static void read_contract(void)
{
    static const uint16_t __code typec_ma[4] = {500, 1500, 2400, 3000};   /* PD_STATUS1[1:0] (reg. info table 3) */
    uint8_t s0 = 0, s1 = 0;
    contract_idx = 0xFF;
    contract_ma = 0;
    if (i2c_read_reg8(HUSB_ADDR, 0x00, &s0) || i2c_read_reg8(HUSB_ADDR, 0x01, &s1))
        return;
    contract_idx = status_volt_index(s0);
    if (contract_idx != 0xFF) {
        contract_ma = pdo_current_ma[s0 & 0x0F];         /* PD_STATUS0[3:0]: the explicit contract's current */
    } else if (s1 & 0x04) {                            /* PD_STATUS1.5V_VOLTAGE: 5 V without PD (Type-C current) */
        contract_idx = 0;
        contract_ma = typec_ma[s1 & 0x03];
    }
}

/* request voltage index i; returns 1 when the HUSB238 reports that contract */
static uint8_t request_voltage(uint8_t i)
{
    uint8_t t;
    if (!(offered & (uint8_t)(1u << i)))
        return 0;                                      /* never request what the charger did not offer */
    i2c_write_reg8(HUSB_ADDR, 0x08, (uint8_t)(volt_sel[i] << 4));
    i2c_write_reg8(HUSB_ADDR, 0x09, 0x01);            /* GO_COMMAND: request the PDO set by PDO_SELECT */
    for (t = 0; t < 50; t++) {                         /* up to 1 s */
        WDOG_FEED();
        delay_ms(20);
        read_contract();
        if (contract_idx == i)
            return 1;
    }
    return 0;
}

static void mark_dirty(void)
{
    dirty = 1;
    dirty_since = ticks();
}

static void ina_init(void)
{
    uint16_t id = 0;
    ina_ok = (uint8_t)(!i2c_read_reg16(INA_ADDR, 0xFE, &id) && id == 0x5449);
    if (ina_ok)
        i2c_write_reg16(INA_ADDR, 0x00, INA_CFG);
}

static void show(uint16_t mv, int16_t ma, int16_t t_dc)
{
    static __xdata char r0[17], r1[17];
    static __xdata struct ui u;
    u.vidx = vidx; u.status = status; u.out_on = out_on; u.ina_ok = ina_ok;
    u.max_ma = (offered & (uint8_t)(1u << vidx)) ? pdo_current_ma[pdo_code[vidx]] : 0;
    u.mv = mv; u.ma = ma; u.t_dc = t_dc;
    ui_lines(r0, r1, &u);
    oled_clear();
    oled_text(0, 0, r0);
    oled_text(1, 0, r1);
    oled_flush();
}

void main(void)
{
    uint8_t ev, held;
    uint16_t last_meas = 0, both_since = 0, oc_since = 0;
    uint8_t oc = 0, d, want_on = 0, hot_lock = 0, resync_n = 0;
    uint16_t mv = 0, raw;
    int16_t ma = 0;
    int16_t t_dc = 250;

    hw_init();
    delay_ms(100);                                   /* OLED VDD stable */
    oled_init();
    ina_init();

    /* wait for the HUSB238's contract: PD takes ~100s of ms; without PD it tries legacy detection after 1.5 s */
    for (raw = 0; raw < 100; raw++) {
        WDOG_FEED();
        read_contract();
        if (contract_idx != 0xFF)
            break;
        delay_ms(25);
    }
    read_pdos();
    vidx = 0;
    if (settings_load(&d, &log_slot, &log_seq)) {
        saved_data = d;
        vidx = (uint8_t)(d & 0x07);
        if (vidx >= NVOLT) vidx = 0;
        want_on = (uint8_t)(d >> 7);
    }
    status = ST_OK;
    if (contract_idx == 0xFF) {
        status = ST_NOPD;
        vidx = 0;
    } else if (!(offered & (uint8_t)(1u << vidx))) {
        vidx = contract_idx;                         /* saved voltage not on this charger: stay where we are */
        want_on = 0;
    } else if (vidx != contract_idx && !request_voltage(vidx)) {
        vidx = contract_idx;
        want_on = 0;
    }
    read_contract();
    if (want_on && contract_idx == vidx)
        set_output(1);

    for (;;) {
        /* ---------------------------------------------------------------- buttons (10 ms poll, edge on press) */
        WDOG_FEED();
        delay_ms(10);
        ev = buttons_take();
        held = buttons_held();

        if ((held & (BTN_EV_ON | BTN_EV_CYC)) == (BTN_EV_ON | BTN_EV_CYC)) {
            if (!both_since) both_since = ticks() | 1;
            else if ((uint16_t)(ticks() - both_since) > 5000) jump_to_bootloader();
        } else {
            both_since = 0;
            if ((ev & BTN_EV_ON) && status != ST_NOPD) {
                if (out_on) set_output(0);
                else if (!hot_lock && contract_idx == vidx) { status = ST_OK; set_output(1); }
                mark_dirty();
                show(mv, ma, t_dc);                      /* answer the press now, not at the next 100 ms tick */
            }
            if ((ev & BTN_EV_CYC) && status != ST_NOPD) {
                set_output(0);                           /* requirement: cycling while on turns the output off */
                vidx = next_offered(vidx, offered);
                show(mv, ma, t_dc);                      /* the new voltage appears at once; negotiation takes ~0.1-1 s */
                if (vidx != contract_idx && !request_voltage(vidx)) {
                    vidx = contract_idx;                 /* show what we really have */
                    show(mv, ma, t_dc);
                }
                mark_dirty();
            }
        }

        /* ---------------------------------------------------------------- measure, protect, display (10 Hz) */
        /* the INA226 makes a fresh averaged pair every 16 x (1.1 + 1.1) ms = 35 ms, so each update reads new data */
        if ((uint16_t)(ticks() - last_meas) >= UPDATE_MS) {
            last_meas = ticks();
            t_dc = ntc_decic(adc_read(ADC_CH_P32));
            if (ina_ok) {
                if (!i2c_read_reg16(INA_ADDR, 0x01, &raw)) ma = ina_current_ma((int16_t)raw);
                if (!i2c_read_reg16(INA_ADDR, 0x02, &raw)) mv = ina_bus_mv(raw);
            }
            if (out_on && over_temperature(t_dc, ma)) {
                set_output(0);
                status = ST_HOT;
                hot_lock = 1;
            }
            if (hot_lock && t_dc < T_RESUME_DC) {
                hot_lock = 0;
                if (status == ST_HOT) status = ST_OK;
            }
            /* current above the contract (+10 %) for 0.5 s: the charger or cable is being overloaded */
            oc = (uint8_t)(out_on && contract_ma && ma > (int16_t)(contract_ma + div10(contract_ma)));
            if (!oc) oc_since = 0;
            else if (!oc_since) oc_since = ticks() | 1;
            else if ((uint16_t)(ticks() - oc_since) >= OC_TRIP_MS) { set_output(0); status = ST_OVERCURRENT; }
            read_contract();
            if (out_on && contract_idx != vidx) set_output(0);   /* the source renegotiated (or its OTP fell to 5 V) */
            if (status == ST_NOPD && contract_idx != 0xFF) {     /* a contract arrived late */
                read_pdos();
                vidx = contract_idx;
                status = ST_OK;
            }
            if (++resync_n >= 100) {                    /* every 10 s, resend the whole screen: repairs anything a */
                resync_n = 0;                            /* glitch left in the panel's RAM that we cannot see */
                oled_resync();
            }
            show(mv, ma, t_dc);
        }

        /* ---------------------------------------------------------------- save late, only on change */
        if (dirty && (uint16_t)(ticks() - dirty_since) >= SAVE_DELAY_MS) {
            d = (uint8_t)(vidx | (out_on ? 0x80 : 0));
            if (d != saved_data) {
                settings_save(d, &log_slot, &log_seq);
                saved_data = d;
            }
            dirty = 0;
        }
    }
}
