# USB-PD trigger board - requirements and architecture

Revision: 1 - Date: 2026-11-10 - Status: draft (parts proposed)

Scope: hobby/personal project. Not designed or reviewed for safety-critical, medical, automotive, on-board mains or
certified use.

## 1. Purpose

- What it does: USB-C PD sink ("trigger") board. Negotiates a voltage from a PD charger, switches it to an output,
  shows ON/OFF and the selected voltage on a tiny display. Two buttons: output on/off, cycle voltage. Remembers the
  last voltage and on/off state across power cycles.
- Users: owner
- Quantity: 10 boards, 10 assembled
- Budget: not tight at this quantity
- Project type: tool (bare bones; no fuses or extra protection by request)

## 2. Usage and environment

| Item | Requirement |
|---|---|
| Location | indoor, desk |
| Ambient temperature | 0 to 40 C (assumed) |
| Enclosure | user's 3D-printed case: button actuators (plungers), window for the OLED |
| Mounting | none specified yet (case holds the board) |
| External connections | USB-C in (PD charger); output: 2 x through-hole pads at 5.08 mm pitch - fits a 2-pin screw terminal or soldered wires |
| User-touchable parts (ESD) | USB-C shell, buttons (through printed plungers), output wires |
| Long cables | output wires, length unknown |
| Mains proximity | none (external certified charger) |
| Display | smaller than 0.91 in; just tall enough for one line of text. Proposed: 0.69 in 96x16 I2C OLED bare panel (26.3 x 8 mm glass, 14-pin FPC) |

## 3. Functional blocks (behaviour agreed 2026-11-10)

- Cycle button: steps through only the PDOs the connected charger advertises. Pressing it while the output is ON
  turns the output OFF and changes the voltage; output stays OFF until the on/off button is pressed.
- Display shows the requested (negotiated) voltage and ON/OFF. No output voltage measurement; MCU needs no ADC.
- Power-up: request the saved voltage if the charger advertises it (else 5 V), restore the saved ON/OFF state.
- User has no programmer; can buy a cheap one, but programming over the board's USB-C port is preferred.
  Proposed: MCU with ROM USB bootloader (CH552E front-runner), entered by holding the cycle button at plug-in.

### Block diagram

```
USB-C (16-pin, 5 A) --VBUS 5-20 V--+------------------------------> AO4407A PMOS high-side --> R10 5 mOhm --> OUT+ (2 x THT, 5.08 mm)
   |  CC1/CC2                      |                                    ^ gate: 100k S-G / 10k to 2N7002
   +--> HUSB238 PD sink (I2C 0x08) <+ VIN                                |
   |       ^ SDA/SCL (3.3 V bus, 2.2k pull-ups)                         |
   |  D+/D- (to MCU only; HUSB238 D+/D- and GATE left NC)                         |
   +--> CH552P MCU (VCC 5 V) ---- SW_EN (P1.0, 2.2k pull-down) ---------+
          |  P1.6 SDA / P1.7 SCL (bit-banged, open-drain); P1.5 left free on purpose (alternate bootloader-entry pin)
          |  P1.1 <- ON/OFF button (to GND)
          |  P3.2 / AIN3 <- board temperature: 2.2k (to 3.3 V) / NTC 10k B3380 (to GND), beside U4 between J1 and U2
          |  P1.4 <- CYCLE button (to 3.3 V; also via BAT54 + 10k to D+ = bootloader entry at plug-in)
          +--> OLED 0.69" 96x16 (SSD1306/SSD1315, I2C 0x3C), FPC hand-soldered
          '--> INA226 power monitor (I2C 0x40, A0 = A1 = GND): Kelvin taps across R10, bus voltage at OUT+
VBUS --> SGM2203-5.0 (SOT-89, 36 V) --> 5V --> XC6206-3.3 (Torex) --> 3V3 (OLED VDD + VBAT, I2C pull-ups)
```

### Interfaces

| From | To | Interface | Speed | Notes |
|---|---|---|---|---|
| CH552 | HUSB238, OLED, INA226 | I2C (bit-banged) | 100-400 kHz | 3.3 V bus; CH552 VIH = 2.4 V at 5 V (DS 18.2) |
| CH552 | USB host | USB FS | 12 Mbit/s | ROM bootloader only; no series resistors |
| CH552 | output switch | GPIO | DC | must default OFF through reset (pull-down) |

### Startup and control

- Before the MCU runs: HUSB238 negotiates on its own; VSET tied to GND -> 5 V request. The output switch is held OFF
  by its pull-down. HUSB238's own GATE output (which turns on at POR in the _002 variant) is not used.
- MCU then reads SRC_PDO_5V..20V (0x02-0x07: detected bit + max current up to 5 A, one register per voltage), requests the saved voltage (or 5 V), and restores the saved ON/OFF state
  only after the new contract is in place.
- Flashing: hold CYCLE while plugging into a PC -> CH552 ROM bootloader (D+ pulled up). Later updates: firmware
  jumps to the bootloader when both buttons are held at plug-in.

### External connections (user-facing)

| Connection | Connector or pad | Label | Polarity / keying | Side |
|---|---|---|---|---|
| Power in / programming | USB-C receptacle | USB-PD IN | keyed | short edge A |
| Output | 2 x THT pad, 5.08 mm, 1.3 mm drill | OUT + / - | silk "+" and "-" | short edge B |
| Buttons | 2 x TS-1088 (4 x 3 mm) | ON/OFF, V | - | top |

### Bring-up test points

| Rail or signal | Test point | Expected |
|---|---|---|
| VBUS | pad, bottom | 5 V (PC) / negotiated V |
| 5V, 3V3 | pads, bottom | 4.3-5.0 V, 3.3 V |
| OUT | output pad | 0 V when OFF |
| GND | pad, bottom | - |
| P3.0/P3.1 (UART debug) | pads, bottom | - |

## 4. Power

- Source: USB-C PD charger, SPR fixed PDOs 5/9/12/15/20 V; up to 5 A only with a 5 A e-marked cable and charger.
  From a PC port: 5 V only (no PD).
- Protection: none by request. HUSB238 has internal OVP (120 %) / UVP / OTP. No fuse, no TVS.

### Rails

| Rail | Voltage | Source | Regulator | Loads |
|---|---|---|---|---|
| VBUS | 5-21 V | USB-C | - | HUSB238, LDO, output switch |
| 5V | 4.3-5.0 V | VBUS | SGM2203-5.0YK3G (SOT-89-3, 36 V in, 150 mA) | CH552 VCC (needs 5 V to write flash / ISP) |
| 3V3 | 3.3 V | 5V | XC6206P332MR-G (Torex) | OLED VDD and VBAT (charge pump input; VBAT 3.3-4.2 V in N069 DS table), I2C pull-ups |
| OUT | = VBUS | VBUS | AO4407A switch | user load, up to 5 A |

### Power budget (5V rail)

| Load | Avg mA | Peak mA | Source |
|---|---|---|---|
| CH552 @ 6 MHz (app) / 24 MHz (bootloader) | 6 | 11 | CH552 DS 18.2 ICC6M5 / ICC24M5 max |
| OLED VBAT (one line of text ~ 30 % lit) | 6 | 20 | N069 DS IBAT max, 100 % lit |
| OLED VDD | 0.2 | 0.3 | N069 DS IDD |
| I2C pull-ups (2 x 2.2k, while low) | 1 | 3 | - |
| INA226 (from 3V3) | 0.33 | 0.42 | INA226 DS IQ max |
| Switch gate drive (10k + 100k at 21 V) | 0 | 0.2 | from VBUS |
| **Total** | **~13** | **~35** | |

LDO dissipation worst case: (21 - 5) V x 35 mA = 0.56 W; SGM2203 SOT-89-3 theta-JA 65 C/W (DS p.3) -> +36 C rise.
Typical: 16 V x 13 mA = 0.21 W -> +14 C. No buck needed (user agreed 2026-11-10).

Output switch: AO4407A R_DS(on) max 18.5 mOhm at V_GS = -4.5 V, 11.5 mOhm at -10 V (UMW DS). Gate divider
100k/10k gives V_GS = -0.91 x VBUS: -4.3 V at 4.75 V, -19.1 V at 21 V (abs max +-25 V). Loss at 5 A / 20 V:
25 x 0.0115 = 0.29 W; at 3 A / 5 V: 9 x 0.0185 = 0.17 W.

## 5. Physical

| Item | Requirement |
|---|---|
| Board outline | long and thin, 67 x 17 mm (widened from 12 mm at layout, user agreed 2026-11-10): USB-C / electronics / buttons / OLED (folded FPC) / switch + output pads |
| Connector positions | USB-C on one short edge; output pads (2 x THT, 5.08 mm) on the opposite short edge |
| Top side | OLED + 2 top-actuated tactile switches (4 x 3 mm), for 3D-printed plungers and a display window. Nothing under the OLED glass (it lies on the board on tape) |
| Mounting holes | none (decided at 12 mm width; still none at 17 mm); the case holds the board by its edges (changed 2026-11-10, user told) |
| Assembly side | top only |

## 6. Manufacturing

| Item | Choice |
|---|---|
| Fab / assembly | JLCPCB, economic assembly, top side only (agreed) |
| Quantity | 10 / 10 |
| Smallest package | 0402 |

## 7. Stackup

| Item | Choice |
|---|---|
| Layers | 2 (agreed) |
| Reason | simple board: I2C only, no switching regulator, short power path |
| Thickness / copper | 1.6 mm, 1 oz both sides (user: no 2 oz) |
| Power path | VBUS: bottom-layer band along the top edge (~3.2 mm) from J1 to Q1's source via field (16 vias), plus copper under the J1 body joining both receptacle VBUS pads (3 + 2 vias). VOUT: Q1's drains -> top pour VOUT_SW -> R10 (vertical, under Q1) -> top pour VOUT (>= 2.3 mm neck, then full height) -> J2. INA226 IN+/IN- leave R10's pads on the side away from the current (Kelvin) and touch no pour; I2C reaches U5 on two bottom rows under the panel. GND: top and bottom pours, stitched. Nothing but copper under the folded OLED panel (x 124.5-152) |

## 8. Component candidates (stock: LCSC global, 2026-11-10; need 11 each)

| Block | Front-runner | LCSC | Stock | Class | Package | Why / concerns |
|---|---|---|---|---|---|---|
| PD sink | HUSB238_002DD | C7471904 | 17,926 | Ext | DFN-10 3x3 | per-voltage SRC_PDO registers (5/9/12/15/18/20 V, current codes to 5 A); I2C request overrides VSET/ISET (DS p.11). GATE (on at POR) and D+/D- left NC |
| PD sink (rejected) | CH224Q | C46061833 | 22,337 | Ext | DFN-10 2x2 | SRCCAP readable only for <100 W sources -> would need probing (user rejected 2026-11-10) |
| MCU | CH552P | C968536 | 3,784 | Ext | QFN-16 3x3 | smallest CH552 with enough pins; ROM USB bootloader; 128 B DataFlash. CH552E (MSOP-10) has only 4 GPIO and is "not recommended for new designs" by WCH |
| MCU (alt) | CH552G | C111292 | 6,423 | Ext | SOP-16 | same die, easier to hand-rework |
| 5 V LDO | SGM2203-5.0YK3G/TR | C4555390 | 1,177 | Ext | SOT-89-3 | 36 V in, 65 C/W |
| 3.3 V LDO | XC6206P332MR-G (Torex) | C5446 | 486,611 | Basic | SOT-23 | genuine Torex pinout 1 VSS / 2 VOUT / 3 VIN. UMW clones (C347376, C351446) rejected: their datasheet and JLC symbols disagree on the pinout. 3.6 V rail dropped (OLED VBAT runs from 3.3 V) |
| Output switch | AO4407A (UMW) | C2841482 | 66,168 | Ext | SOP-8 | -30 V, V_GS +-25 V, 11.5 mOhm |
| Power monitor | INA226AIDGSR (TI) | C49851 | 67,268 (global, 2026-11-10) | Ext | VSSOP-10 | bus 0-36 V, shunt +-81.92 mV full scale; displays output V, I, W. ALERT unused |
| Shunt | RLM12FTCMR005 (Ta-I) 5 mOhm 1 % 1206 | C316225 | 132,154 (global, 2026-11-10) | Ext | 1206 | 25 mV / 0.125 W at 5 A (rating 1 W); 5 A = 31 % of the INA226 range. Footprint NoOutline:RLM12FTCMR005 (JLC's, silk outline removed: its top pad sits against Q1's drain pads) |
| I2C series | 0402WGF2200TCE 220R x2 (R12 SDA, R13 SCL) | C25091 | 1,787,359 (global, 2026-11-10) | Basic | 0402 | in U4's branch: limit clamp current if a 5 V CH552 pin drives high; bus low 0.30 V + Vol (OLED VIL 0.66 V). Firmware still runs P1.6/P1.7 open drain, no pull-up |
| Board temperature | NCP15XH103F03RC (Murata) NTC 10k 1 % B3380, 0402 | C77131 | 289,467 (global, 2026-11-10) | Ext | 0402 | with R11 2.2k: 85 C = 68 counts, 25 C = 138 (8-bit ADC, ref 5 V), ~1.1 count/K near the trip; self-heating ~1.2 K (1 mW/C). RT1 and R11 use outline-free copies (NoOutline.pretty): placed tight beside U4 and D1 |
| Gate driver | 2N7002 | C8545 | 5.9 M | Basic | SOT-23 | |
| USB-C | TYPE-C-31-M-12 | C165948 | 289,362 | Ext | SMD 16-pin | 5 A / 20 V rated (DS) |
| Buttons | TS-1088-AR02016 | C720477 | 933,292 | Basic | 4 x 3 mm SMD | top-actuated |
| Boot diode | BAT54WS | C7502694 | 81,063 | Ext | SOD-323 | |
| OLED | 0.69" 96x16 panel, 14-pin 0.62 mm FPC (Wisevision N069-9616TSWPG02-H14 pinout) | C5123561 | 0 | - | - | NOT at LCSC (0 stock; the Wisevision/Newvisio MPN is not sold elsewhere): buy the generic panel - must be SSD1306/SSD1315, 14 pins at 0.62 mm, 26.3 x 8 mm glass, internal charge pump (e.g. Electropeak "0.69inch OLED IIC 14 pin SSD1306", ~$0.95, Nov 2026; BuyDisplay ER-OLED0.69-1W; NOT ER-OLED0.69-4W = SSD1312, 0.5 mm). Check the seller's pin table against J3 before soldering. Hand-solder the FPC tail flat, then fold the panel onto tape (user agreed 2026-11-10); buy spares |

Datasheets saved in docs/datasheets/.

## 9. Decisions

| Decision | Choice | Reason | Agreed |
|---|---|---|---|
| Project type | tool, bare bones, no fuse/TVS | user | 2026-11-10 |
| Programming | USB bootloader over the USB-C port | user has no programmer | 2026-11-10 |
| Cycle while ON | output turns OFF, voltage changes | safety | 2026-11-10 |
| Display content | requested voltage + ON/OFF; only advertised PDOs offered | user | 2026-11-10 |
| PD sink | HUSB238 (not CH224Q) | full PDO list in one read, no probing | 2026-11-10 |
| Regulation | LDOs, no buck | 0.2-0.56 W in SOT-89 is fine | 2026-11-10 |
| Layers / copper | 2 layers, 1 oz | user | 2026-11-10 |

## 10. Assumptions

- Power range = standard USB PD (SPR) fixed PDOs: 5 / 9 / 12 / 15 / 20 V; output path sized for 5 A (100 W with an
  e-marked cable, 3 A otherwise). EPR (28-48 V) and PPS fine-step voltages out of scope. (stated 2026-11-10)
- No fuse, no TVS beyond what the parts need to work. (user, 2026-11-10)
- 0-40 C indoor desk use.
- First flash needs a PC USB-C port (5 V); CH552 runs at ~4.3-5 V, which meets its 5 V flash-write requirement.
- OLED panel supplied by the user; SSD1306 or SSD1315 controller (firmware handles both).
- Output is held OFF until the MCU has requested the saved voltage and the new contract is in place.

## 11. Open questions

| Question | Who | Blocks |
|---|---|---|
| Exact OLED listing to buy (FPC pinout must match the Wisevision N069 drawing) | user | footprint check before order |
| Firmware toolchain: ch55xduino (Arduino) vs SDCC + ch554_sdcc | Claude, after schematic | firmware |

## 12. Next steps

1. Create the KiCad project - done (hardware/usbpd.kicad_pro).
2. Schematic capture with JLC footprints - done (tools/schematic.py -> hardware/usbpd.kicad_sch).
3. Board (2-layer, 67 x 17 mm), INA226 + 5 mOhm shunt added 2026-11-10 (user approved): placed and routed from tools/place.json + tools/route_plan.py (tools/build_board.sh); DRC 0 errors, 0 unconnected, parity clean.
4. Design review (kicad-board-review), production files.
5. Firmware.
