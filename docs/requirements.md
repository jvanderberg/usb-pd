# USB-PD PPS board - requirements and architecture

Revision: 1 - Date: 2026-11-11 - Status: board designed and reviewed; not yet built

Scope: hobby/personal project. Not designed or reviewed for safety-critical, medical, automotive, on-board mains or
certified use.

## 1. Purpose

- What it does: USB-C PD sink. It negotiates a fixed voltage from a PD charger, switches it to an output, and shows
  the voltage, current, temperature and ON/OFF on a tiny OLED. It remembers the last selection and ON/OFF state
  across power cycles. With a PPS charger it also sets any voltage from about 3.3 to 21 V in
  100 mV steps (clamped to 5-21 V, see section 4), and a current limit that the charger enforces. It is a pocket bench supply (roughly a cheap, small
  PocketPD), not only a trigger board.
- Users: owner
- Quantity: 10 boards, 10 assembled
- Budget: not tight at this quantity.
- Project type: tool, bare bones (no fuses or extra protection, by request)

## 2. Usage and environment

| Item | Requirement |
|---|---|
| Location | indoor, desk |
| Ambient temperature | 0 to 40 C (assumed) |
| Enclosure | user's 3D-printed case: plungers for the 4 buttons, a window for the OLED; it holds the board by its edges |
| Mounting | none (the case holds the board) |
| External connections | USB-C in (PD charger); output: 2 x through-hole pads at 5.08 mm pitch, for a 2-pin screw terminal or soldered wires |
| User-touchable parts (ESD) | USB-C shell, buttons (through printed plungers), output wires |
| Long cables | output wires, length unknown |
| Mains proximity | none (external certified charger) |
| Display | 0.69 in 96x16 I2C OLED bare panel (26.3 x 8 mm glass, 14-pin FPC), one line of text per row |

The board is long and thin (67 x 17 mm) to fit that case.

## 3. Functional blocks (behaviour agreed 2026-11-11)

### Controls (4 buttons)

| Button | Action |
|---|---|
| ON/OFF | Output on/off. Does only this. |
| CYCLE | Steps through `5V -> 9V -> ... -> 20V -> PPS`. Only what the charger advertises is listed; the PPS entry appears only if the charger offers a PPS APDO. Pressing it while the output is ON turns the output OFF first; it stays OFF until ON/OFF is pressed. |
| UP / DOWN | In PPS: adjust the setpoint. Live while the output is ON. Outside PPS: do nothing. |
| UP + DOWN together | In PPS: switch between editing voltage and editing current limit. |

Hold-to-accelerate for UP/DOWN (starting values, to be tuned on hardware):

| Held for | Voltage step | Repeat |
|---|---|---|
| tap | 100 mV | - |
| 0.4 s | 100 mV | 10 /s |
| 1.0 s | 500 mV | 10 /s |
| 2.5 s | 1 V | 10 /s |

- Current limit: 16 codes from about 1 A to 5 A, about 0.25 A each (AP33772S `CURRENT_SEL`). One step per press; no
  acceleration needed.
- If the charger advertises more than one PPS range (e.g. 3.3-11 V at 5 A and 3.3-21 V at 3 A), show one PPS entry.
  Firmware picks the range that covers the setpoint with the most current.
- While holding a button, the display updates every repeat. Firmware sends a new PD request at most every 100-200 ms
  (latest value wins) and never queues requests.
- The PPS setpoint (V and I) is saved to DataFlash about 2 s after the last press, not on every step.

### Display

- Two rows of text. Fixed voltage: the voltage, ON/OFF and the NTC temperature; with the output on, the current and
  power; with it off, the charger's current limit at that voltage.
- Faults replace the second row: HOT, OVER I, OVER V, UNDER V. No PD charger (plain 5 V): NO PD.
- In PPS, the setpoint is shown while adjusting, then the measured voltage about 1 s after the last press.
- In PPS, show which value (V or I) is being edited, e.g. inverted or blinking.
- Measured V and I come from the AP33772S (`VOLTAGE` 0x11: 80 mV/LSB, `CURRENT` 0x12: 24 mA/LSB). The user does not
  need high accuracy (2026-11-11).
- Temperature comes from the AP33772S `TEMP` (0x13), read from the NTC on its OTP pin.

### Block diagram

```
USB-C (16-pin, 5 A) --VBUS--> R_SENSE 5 mOhm --VBUS_S--+--> Q1 NMOS (AON7262E), D = VBUS_S, S = OUT --> OUT+ (2 x THT, 5.08 mm)
   |  CC1/CC2                  |                       |        ^ gate <- PWR_EN, direct (as RotoPD)
   |                           +-> ISENP               +-> VCC  |
   +--> AP33772S PD sink (I2C 0x52) ------------------ PWR_EN ---+      VOUT pin <- OUT (27R, as RotoPD)
   |       OTP <- NTC 10k (NCP15XH103) to GND          VSEL floating (= 5 V default)
   |       DP/DN, FLIP, LED, GPIO, INT: NC
   |  D+/D- (to MCU only, for the bootloader)
   +--> CH552P MCU (VCC 5 V)
          |  P1.6 SDA / P1.7 SCL (bit-banged, open drain, 3.3 V bus) -> OLED
          |  P1.0 / P3.2 (bit-banged, open drain, 5 V bus, 4.7k pull-ups to 5V) -> AP33772S only
          |  P1.1 <- ON/OFF      P1.4 <- CYCLE (to 3V3; also via BAT54 + 10k to D+ = bootloader entry at plug-in)
          |  P3.3 <- UP          P3.4 <- DOWN        (to GND, internal pull-ups; ON/OFF and UP/DOWN likewise)
          |  front panel, left to right:  [ON]      [SEL]        [UP] [DN]
          |  P1.5: kept free (bootloader alt pin)   P3.0/P3.1: UART debug pads
          '--> OLED 0.69" 96x16 (SSD1306/SSD1315, I2C 0x3C), FPC hand-soldered and folded onto the board
VBUS_S --> SGM2203-5.0 (SOT-89, 36 V) --> 5V --> XC6206-3.3 (Torex) --> 3V3 (OLED VDD + VBAT, bus A pull-ups)
```

The sense resistor sits on the input side, between the connector and VCC/Q1 drain: ISENP on the connector side, VCC
after the shunt. This is how CentyLab's RotoPD (open KiCad, AP33772S + AON7262E) wires it; the datasheet only says
ISENP goes to the shunt's positive node.

### Interfaces

| From | To | Interface | Notes |
|---|---|---|---|
| CH552 | OLED (0x3C) | I2C bus A, bit-banged, 3.3 V | P1.6/P1.7, 150R series, 2.2k pull-ups to 3V3. Open drain only: the CH552 runs at 5 V and must never drive this bus high |
| CH552 | AP33772S (0x52) | I2C bus B, bit-banged, 5 V | P1.0 / P3.2, 4.7k pull-ups to the 5V rail. DS recommends SDA/SCL at 4.37-5.33 V; every open AP33772S design checked runs it at 5 V (see below). Keeps the 5 V bus away from the OLED (VDD abs max 4 V) |
| CH552 | USB host | USB FS | ROM bootloader only; no series resistors |
| AP33772S | Q1 gate | PWR_EN charge pump | MCU controls it through `SYSTEM` (0x06) |

### Startup and control

- At attach, the AP33772S powers from VBUS, sets STATUS.STARTED and waits up to 100 ms for configuration. It then
  negotiates 5 V (VSEL floating). In its default ("auto") mode it turns Q1 on once negotiation succeeds.
- Firmware, first thing in `main()`, inside that 100 ms window:
  1. `SYSTEM` (0x06) = 0x11: output forced OFF.
  2. `PDCONFIG` (0x05): clear EPR_MODE_EN (bit 0). Never go above 21 V. Leave PPS_AVS_EN set.
  3. `CONFIG` (0x04): clear DR_EN (bit 7). No automatic 50 % current de-rating; firmware owns that.
- Then: read `SRCPDO` (0x20, 26 bytes), request the saved voltage or PPS setpoint (`PD_REQMSG` 0x31), wait for
  `PD_MSGRLT`, set `OCPTHR` (0x19), and restore the saved ON/OFF state (`SYSTEM` = 0x12 for ON).
- **Accepted (user, 2026-11-11):** the output is live at 5 V whenever the firmware is not running. That covers a
  blank board from JLC and any time the CH552 sits in its ROM bootloader (flashing). No hardware hold-off FET.
- Flashing: hold CYCLE (SEL) while plugging into a PC: the CH552 ROM bootloader starts (D+ pulled up through D1/R4).
  Later updates: hold ON + SEL for 5 s and the firmware jumps to the bootloader, with the output forced off first.

### Protection (in the AP33772S, each one opens Q1)

| Function | Setting | Notes |
|---|---|---|
| OCP | `OCPTHR` 0x19, 50 mA/LSB; trips at 110 % after 30 ms | 0 = 110 % of the contract. Firmware sets it from the fixed contract or the PPS current limit |
| OVP | `OVPTHR` 0x18, default VREQ + 2 V (110 % above 18 V) | |
| UVP | `UVPTHR` 0x17, default 80 % of VREQ | |
| OTP (NTC) | `OTPTHR` 0x1A, default 120 C | Set to 90 C, below the default, as a backstop to the firmware trip (see design-review M1) |
| OTP (die) | 130 C, fixed | |

After any fault the chip stays off until the MCU sends a new `PD_REQMSG`. Firmware shows the fault (HOT, OVER I, OVER V,
UNDER V) and needs ON/OFF to retry.

Firmware has its own temperature trip on top: board NTC plus an I^2 estimate for Q1, now from the AP33772S
`CURRENT` reading.

### External connections

| Connection | Connector or pad | Label | Polarity / keying | Side |
|---|---|---|---|---|
| Power in / programming | USB-C receptacle | - | keyed | short edge A |
| Output | 2 x THT pad, 5.08 mm | OUT + / - | silk "+" and "-" | short edge B |
| Buttons | 4 x TS-1088 (4 x 3 mm) | ON, SEL, UP, DN | - | top |

The 4 buttons sit in one row, grouped by function (user, 2026-11-11):

```
[ON]      [SEL]        [UP] [DN]
```

ON/OFF on its own, SEL (= CYCLE) in the middle, UP/DOWN as a close pair so one thumb works both. Wide gaps
between the groups so the plungers can't be confused by feel. Which end of the row faces the USB-C connector is
decided at placement.

### Bring-up test points

VBUS, 5V, 3V3, GND, OUT, PWR_EN (Q1 gate), and P3.0/P3.1 (UART debug).

### Why a separate 5 V I2C bus (checked 2026-11-11)

All five open AP33772S schematics found put the chip's SDA/SCL at 5 V:

| Design | AP33772S side | MCU side |
|---|---|---|
| CentyLab RotoPD | BSS138 shifters, 5.1k to the chip's V5V | 3.3 V |
| CentyLab RotoPD Pro | TXS0102 shifter to V5V | 3.3 V |
| hnz1102 mini-power | PCA9306 shifter, 4.7k to +5V | 3.3 V |
| Keyaan-07 USB-PD-Power-Supply | BSS138 shifters, 5.1k | 3.3 V |
| ihsan-sa USB-C trigger board | 10k to +5V | - |

The CH552 already runs at 5 V, so it can drive a 5 V bus directly with no shifter. Two pins that need no other job carry it: P1.0
(no MCU-driven switch) and P3.2 (the NTC is on the AP33772S). Pull-ups go to the board's 5V rail, not the
AP33772S's V5V pin: the CH552's VCC can sit as low as 4.3 V, and V5V (up to 5.33 V) would pull its pins above its
supply. The 5V rail's 4.3 V worst case is a little under the DS's 4.37 V "recommended", but the inputs' VIH is
1.4 V (DS), so the margin is large.

Firmware runs two bit-banged buses. Bus A stays open drain only (3.3 V pull-ups, 5 V MCU).

Gate resistor in the same designs: none (RotoPD), 10R (Keyaan-07), 100R (mini-power), 5.1k (RotoPD Pro). This board
follows RotoPD (direct).

## 4. Power

- Source: USB-C PD charger. Fixed SPR 5/9/12/15/20 V and SPR PPS (about 3.3-21 V). From a PC port: 5 V only.
- EPR (28 V and up): disabled in firmware (`PDCONFIG.EPR_MODE_EN` = 0) and never requested.
- Protection: AP33772S OCP/OVP/UVP/OTP (above). No fuse, no TVS (by request).

### Rails

| Rail | Voltage | Source | Regulator | Loads |
|---|---|---|---|---|
| VBUS | 5-21 V (PPS clamped) | USB-C | - | sense resistor -> AP33772S VCC, Q1, SGM2203 |
| 5V | 4.3-5.0 V | VBUS | SGM2203-5.0YK3G (SOT-89-3, 36 V in, 150 mA) | CH552 |
| 5V (also) | | | | AP33772S I2C pull-ups (bus B) |
| 3V3 | 3.3 V | 5V | XC6206P332MR-G (Torex) | OLED, bus A I2C pull-ups |
| V5V / V18 | internal | AP33772S | internal LDOs | AP33772S only: 1 uF on V5V, 100 nF on V18 (DS) |
| OUT | = VBUS | VBUS | Q1 | user load, up to 5 A |

### PPS minimum clamped to 5 V (user, 2026-11-11)

PPS chargers go down to 3.3 V, but below about 5 V the SGM2203 drops out: the 5V rail follows VBUS, the CH552 and
OLED run near 3 V, and DataFlash writes (which need 5 V) fail. Firmware therefore never requests a PPS voltage below
5.0 V; DOWN stops there. The PPS range offered is max(5.0 V, APDO min) to min(21 V, APDO max).

### Power budget

| Load | Avg mA | Peak mA | From |
|---|---|---|---|
| CH552 | 6 | 11 | 5V (CH552 DS ICC6M5 / ICC24M5 max) |
| OLED VBAT + VDD | 6 | 20 | 3V3 (N069 DS IBAT, IDD) |
| I2C pull-ups, bus A | 1 | 3 | 3V3 |
| I2C pull-ups, bus B (2 x 4.7k) | 1 | 2 | 5V |
| AP33772S | 3.3 | 6 | VBUS (DS IVCC_OPR) |
| **Total** | **~17** | **~42** | |

LDO dissipation: worst case (21 - 5) V x 35 mA = 0.56 W; SGM2203 SOT-89-3 theta-JA 65 C/W -> about +36 C.

### Output switch

- Q1: AON7262E, 60 V N-MOSFET, DFN 3x3. R_DS(on) max 8.5 mOhm at V_GS = 4.5 V and 6.2 mOhm at 10 V (AOS DS); about
  1.6 x at 125 C.
- Loss at 5 A: 25 x 0.0085 = 0.21 W cold, about 0.34 W hot.
- Shunt: 0.125 W at 5 A (RLM12, rated 1 W).
- One NMOS, not the datasheet's back-to-back pair: the datasheet allows a single NMOS when reverse current from the
  load is not a concern. With D = VBUS_S and S = OUT, the body diode blocks VBUS -> OUT when off.
- Gate: PWR_EN straight to the gate, as CentyLab's RotoPD (shipping, open KiCad) does with this same AON7262E and
  AP33772S. The DS suggests 6.2k in series; the proven reference uses none (user, 2026-11-11: follow what is known
  to work). The DS does not give the charge-pump voltage, so the loss figures above use the 4.5 V R_DS(on) as the
  conservative case.

## 5. Physical

| Item | Requirement |
|---|---|
| Board outline | 67 x 17 mm: USB-C / electronics / OLED (folded FPC) with the buttons / switch + output pads |
| Top side | OLED + 4 top-actuated buttons |
| Mounting holes | none; the case holds the board by its edges |
| Assembly side | top only |

## 6. Manufacturing

JLCPCB economic assembly, top side only, 10 / 10, smallest package 0402.

## 7. Stackup

2 layers, 1.6 mm, 1 oz both sides.

Layout notes from the AP33772S DS: VCC/V5V caps close to the pins; exposed pad to GND; sense traces start at the
shunt's pads (Kelvin); CC1/CC2 short.

## 8. Component candidates (stock: LCSC global, 2026-11-11; need 11 each)

| Block | Front-runner | LCSC | Stock | Price | Package | Why / concerns |
|---|---|---|---|---|---|---|
| PD sink | AP33772SDKZ-13-FA02 (Diodes) | C50341643 | 2,923 | $0.96 | W-QFN4040-24 | PPS 100 mV / ~0.25 A steps; PD3.1 certified (TID 10062); used by PocketPD, RotoPD, PicoPD Pro. FA02 = current standard firmware (FA01 dropped from DS Rev 10-2, 0 stock) |
| PD sink (rejected) | HUSB238A-BB001-QN16R | C24833806 | 2,641 | $0.64 | QFN-16 3x3 | no public register map; no published working PPS; BB001 boots requesting 28 V; the 5 V-boot BB004 is not at LCSC |
| Output switch | AON7262E (AOS) | C176755 | 3,732 | $0.60 | DFN-8 3x3 | 60 V, 8.5 mOhm at 4.5 V; same part as RotoPD / PicoPD Pro |
| Output switch (alt) | AON7410 | C269266 | 10,145 | $0.09 | DFN-8 3x3 | 30 V, 26 mOhm at 4.5 V: 3 x the loss of the AON7262E. Only if cost matters |
| Shunt | RLM12FTCMR005 5 mOhm 1 % 1206 | C316225 | - | - | 1206 | the AP33772S expects 5 mOhm (DS OCP section) |
| NTC | NCP15XH103F03RC 10k B3380 | C77131 | - | - | 0402 | The AP33772S default 4-point table is Murata NCP03XH103, the same curve, so no TR25-TR100 writes are needed |
| MCU | CH552P | C968536 | - | - | QFN-16 | smallest CH552 with enough pins; ROM USB bootloader; 128 B DataFlash |
| Buttons | TS-1088-AR02016 x 4 | C720477 | - | - | 4 x 3 mm | Basic part |
| 5 V LDO | SGM2203-5.0YK3G/TR | C4555390 | - | - | SOT-89-3 | 36 V in, 65 C/W |
| 3.3 V LDO | XC6206P332MR-G (Torex) | C5446 | - | - | SOT-23 | genuine Torex pinout 1 VSS / 2 VOUT / 3 VIN (UMW clones disagree on the pinout) |
| USB-C | TYPE-C-31-M-12 | C165948 | - | - | SMD 16-pin | 5 A / 20 V rated (DS) |
| Boot diode | BAT54WS | C7502694 | - | - | SOD-323 | |
| OLED | 0.69" 96x16, 14-pin 0.62 mm FPC (Wisevision N069-9616TSWPG02-H14 pinout) | - | - | - | - | not at LCSC: buy a generic SSD1306/SSD1315 panel with 14 pins at 0.62 mm and 26.3 x 8 mm glass, check its pin table against J3, hand-solder; buy spares |

Stock dashes: check before order with `sourcing.py stock`.

Datasheets: docs/datasheets/AP33772S_rev10-2.pdf (DS46176 Rev 10-2, Aug 2026),
AON7262E.pdf.

### AP33772S facts the datasheet leaves out

From CentyLab's driver (github.com/CentyLab/AP33772S-CentyLab, Aug 2026), confirmed against hnz1102/ap33772s-driver
(Rust, written independently; it backs the hnz1102 mini-power board). truebest/ap33772s says it copies the
CentyLab logic, so it does not count as a second source.

- `SYSTEM` (0x06): bits 1:0 = VOUTCTL (0 auto, 1 forced off, 2 forced on); bits 5:4 = CC status (read only, 0x10 at
  power-on). So write 0x11 = off, 0x12 = on.
- `PD_MSGRLT` (0x33): 0 busy, 1 success, 2 invalid, 3 unsupported, 4 failed (hnz1102).
- SRC PDO word (16 bits, little endian):
  - Fixed: bits 7:0 = V_max (100 mV/LSB SPR, 200 mV/LSB EPR); 9:8 = peak current; 13:10 = I_max code; bit 14 = type
    (0 fixed, 1 APDO); bit 15 = detected.
  - PPS: bits 9:8 = V_min code (1 = 3.3 V; 2 = 3.3-5 V).
- I_max / `CURRENT_SEL` code: 1.00, 1.25, ... 4.50 A for codes 0-14 (0.25 A steps), 5.00 A for 15 (hnz1102 table;
  CentyLab's ranges agree).
- Other drivers to compare against: truebest/ap33772s (platform-agnostic C; a candidate base for SDCC),
  hnz1102/ap33772s-driver, ScottGibb/AP33772S-rs.

## 9. Decisions

| Decision | Choice | Reason | Agreed |
|---|---|---|---|
| MCU | CH552P (not ESP32 / RP2040) | job is I2C + buttons + OLED; firmware fits in 14 KB | 2026-11-11 |
| UI | 4 buttons: ON/OFF, CYCLE, UP, DOWN | a free-form voltage needs up/down; ON/OFF stays one-function; no encoder | 2026-11-11 |
| CYCLE list | fixed PDOs + one "PPS" entry; UP/DOWN adjust in PPS | user | 2026-11-11 |
| PD sink | AP33772S-FA02 | in stock, ~$0.96, documented, proven PPS | 2026-11-11 |
| Power monitor | none; V/I from the AP33772S | user does not need high accuracy | 2026-11-11 |
| Output hold-off at boot | none; firmware writes SYSTEM = 0x11 at boot | output live on blank / flashing boards accepted as corner cases | 2026-11-11 |
| EPR | disabled in firmware | parts and use are 21 V max | 2026-11-11 |
| PPS minimum | 5.0 V (clamped in firmware) | 5 V LDO drops out below that | 2026-11-11 |
| Q1 gate drive | PWR_EN direct, AON7262E, as RotoPD | follow the proven CentyLab circuit | 2026-11-11 |
| Button row | [ON] [SEL] [UP][DN], one row | user | 2026-11-11 |
| AP33772S I2C | own 5 V bit-banged bus on P1.0 / P3.2, 4.7k to 5V | every open design runs it at 5 V; CH552 is 5 V | 2026-11-11 |

## 10. Assumptions

- PPS keepalive: PPS needs the sink to re-request at least every 10 s. The AP33772S is PD3.1-certified, so it should
  do this itself. The datasheet does not say. Verify on the bench.
- The CH552 boots and writes `SYSTEM` well inside the AP33772S's 100 ms configuration window.
- AP33772S DP/DN left unconnected (moisture detection waived, as the DS allows and RotoPD does), so the USB data
  pair stays with the CH552 bootloader.
- VSEL floating = 5 V default request (DS Table 4).

## 11. Open questions

| # | Question | Who | Blocks |
|---|---|---|---|
| - | None open. I2C level and register fields settled 2026-11-11 (section 3, section 8) | | |

## 12. Next steps

1. Schematic (scripted in tools/).
2. Placement and routing, 67 x 17 mm.
3. Design review (kicad-board-review), especially the thermal case with Q1.
4. Firmware: AP33772S driver, PPS UI, 4 buttons, OLED, DataFlash settings.
