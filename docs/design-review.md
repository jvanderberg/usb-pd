# Design review - 2026-11-10 (commit 23ca8df)

kicad-board-review analysis pack (config: docs/review_config.json) plus an independent adversarial reviewer; every
finding below was checked against the files and datasheets. Stackup: JLC 2-layer 1.6 mm, 1 oz.

## Blockers

None. Every IC pin map matches its datasheet; nothing exceeds an absolute maximum in hardware from 5 to 21 V.

## Major

### M1. 100 W (20 V / 5 A) continuous overheats the board, especially in a closed case
- Losses at 20 V / 5 A:
  - Q1 0.33-0.40 W (AO4407A, 11.5 mOhm max at -10 V, hot)
  - copper 0.37-0.45 W (VBUS 8.4 + GND 5.0 + VOUT 1.5 mOhm, solved)
  - USB-C contacts 0.1-0.5 W (spec max 40 mOhm per contact)
  - R10 0.125 W
  - U2 0.2-0.5 W ((20 - 5) V x 13-35 mA)
  - Total about 1.3-2.1 W.
- Thermal solve over the 67 x 17 mm board (both faces):

  | Case | Board average | Peak |
  |---|---|---|
  | Closed case, 35 C air, h 7 W/m2K, worst-case inputs | ~165 C | ~200 C |
  | Bare board, typical inputs, h 12 W/m2K | ~100 C | ~120 C, near J1 |
  | 5 V / 3 A | 60-86 C | |
- The model ignores heat carried away by the cables, which roughly doubles the cooling, so the absolute numbers are
  pessimistic. The conclusion holds: a PLA (~60 C) or PETG (~80 C) case will soften at 100 W continuous.
- Done 2026-11-10 (user's choice): board temperature sensor RT1 (NTC) on P3.2 / AIN3. It sits beside U4, between the
  20 V / 9 V hot spots (J1, U2); there is no room for a part next to Q1. Firmware trips the output on NTC temperature
  plus an I^2-based Q1 offset from the INA226 (solve: Q1 runs 13-23 K above the board average at 5 A).
- Options:
  - (a) firmware power limit from the INA226 already on the board: about 3 A continuous, 5 A in timed bursts (the
    board's thermal time constant is ~230 s);
  - (b) lower R_DS(on) Q1, e.g. AON6407 (DFN 5x6, SO-8 compatible pinout, C13899, 2,523 in stock, R_DS(on) not yet
    verified against its datasheet). DECLINED by the user 2026-11-10: Q1 stays AO4407A;
  - (c) widen the VBUS / GND copper near J1;
  - (d) vent the case.

### M2. The 5 V CH552 can drive 5 V onto the 3.3 V I2C bus
- Limits on the bus:
  - INA226 SCL/SDA: max VS + 0.3 = 3.6 V (DS Absolute Maximum Ratings).
  - OLED VDD: abs max 4 V (N069 DS).
  - HUSB238: 6 V, fine.
- The CH552 resets P1 to quasi-bidirectional (P1_MOD_OC = P1_DIR_PU = FFh, CH552 DS port table). In that mode it
  drives a pin high for 2 clocks on every low-to-high transition. Push-pull mode would drive a full 5 V.
- Fix (firmware rule): set P1.6 and P1.7 to open drain with no pull-up (P1_MOD_OC bit = 1, P1_DIR_PU bit = 0) before
  the first I2C transaction, and never drive them high.
- Done 2026-11-10: 220R series resistors R12 (SDA) / R13 (SCL) in U4's branch, pull-ups on the bus side. 1k was
  rejected: the low level at the bus would be 3.3 x 1k / 3.2k = 1.03 V, above the OLED's VIL (0.2 VDD = 0.66 V).
  With 220R it is 0.30 V + Vol; a 5 V drive puts about 6 mA, shared, into the INA226 / OLED clamps.

## Minor
- U2 (SGM2203, up to 0.53 W) had no ground via near its tab (14.2 K step to B.Cu). Done: 3 vias just below the tab
  (the bottom under the tab is the VBUS band and the BTN_ON / SW_EN lanes). Step 11.1 K; U2 board temperature -5 K;
  the board area still limits it (M1).
- R10's JLC footprint pads were 1.21 mm long against the RLM12 datasheet land (1.7 x 1.8 mm, gap 1.6). Done
  (tools/footprints.py rlm12_land): pad 2 per datasheet; pad 1 keeps the datasheet inner edge but is 1.0 mm long,
  because a full pad would overlap Q1's drain pads (same net) and the board edge is behind pad 2.
- Kelvin sense traces now leave R10's pads at their inner edges, as the RLM12 datasheet shows; they still touch the
  pour edge about 0.2 mm from the pad (small error, can be calibrated).
- U2 output: 1 uF 0402 at 5 V DC bias is about 0.5 uF. Check the SGM2203 stability minimum, or use 2.2 uF / 0603.
- U3 input capacitor (C3) is 6.5 mm of track from U3 VIN.
- OLED VBAT at 3.3 V is below the app-note range of 3.5-4.2 V (works, dimmer). The XC6206's +2 % tolerance can put
  VDD at 3.37 V, above the 3.3 V recommended maximum but below the 4 V abs max.
- OLED RES# comes only from an RC. A spare GPIO (P1.5) driving it would be more robust.
- Bootloader entry (CYCLE -> BAT54 -> 10k -> D+) depends on undocumented ROM behaviour. A P1.5 test pad was tried and
  NOT added: P1.5 is boxed in under U4 (C5, the CYCLE route, and the D+ bottom route right where its via would go).
  Fallbacks instead: a blank CH552 starts in its bootloader, and the firmware jumps to the bootloader on a button combo.
- Q1 turns on hard with no inrush limit. Turn-off spends ~0.3 ms in the linear region (100k gate pull-up), which is
  fine at these energies.
- USB D+/D- are not impedance-controlled and partly cross a void. They run full speed only, over ~20 mm, which is
  electrically short: acceptable.

## Checked and fine
- Pin maps against datasheets (HUSB238, CH552P, INA226, AO4407A, SGM2203, XC6206, OLED FPC incl. fold orientation).
- Q1 V_GS at 21 V: -19.1 V against +-25 V.
- SW_EN reset state: at most 0.31 V into the 2.2k, so the output stays off.
- BAT54 polarity; I2C addresses (0x08 / 0x3C / 0x40); nothing but copper under the OLED panel.
- DC drops at 5 A: VBUS 44 mV, GND 26 mV, VOUT 8 mV.
- J1 VBUS current density flagged by the solver: an artefact of the ideal source. Each pad solved alone gives 9.2 vs
  10.9 mOhm, an even split.
- Stitching what-if: more ground vias change no temperature (area, airflow and loss limit it).
- DRC 0 errors, ERC 0, 0 unconnected, parity clean.

## Not checked
- USB-C land pattern against the connector drawing (datasheet is image-only).
- The CH552 ROM bootloader entry condition (not documented).
- Enclosure air and the cooling the cables provide.
