# v2 (PPS) design review, 2026-11-11

Inputs: the board as built from `v2/tools` (usbpd2, 67 x 17 mm, JLC 2-layer 1.6 mm, 1 oz), `docs/requirements-v2.md`, and the
kicad-board-review analysis pack (`v2/docs/review_config.json`). An independent adversarial reviewer checked the board
against the datasheets as well; its findings are merged below, each re-checked against the files.

## Blockers

### B1. The saved board had no zone fills (fixed)
- The router writes zones unfilled, and the whole power path (VBUS, VBUS_S, OUT) plus most ground connections exist only as
  pours. Gerbers exported from that file would have had no power copper.
- Done: `tools/build_board.sh` now refills the zones as its last stage, saves the fills, and fails if they are missing.
  Still look at the copper gerbers before uploading: the VBUS_S band along the top edge and the OUT pour must be there.

## Major

### M1. 100 W continuous overheats the board, and Q1 runs hotter than the requirements assumed
- Q1's gate drive is lower than assumed. The AP33772S limits PWR_EN to VCC + 4 V (DS abs max, "Voltage from PWR_EN to
  VCC Pin"). VCC is on Q1's drain net, so V_GS is under 4 V, not the 4.5 V used in requirements-v2 s4.
- AON7262E Fig. 5 at 125 C then gives about 10.5-13 mOhm for a typical part and about 20 mOhm for a high-threshold one.
  At 5 A that is 0.4-0.5 W in Q1, not 0.34 W.
- Thermal solve with the worst-case figures (Q1 20 mOhm, J1 contacts at the spec maximum, LDO 0.56 W). Closed case, 35 C
  air, h 7 W/m2K, cables ignored:

  | Corner | Total loss | Board average | Q1 board | U2 board |
  |---|---|---|---|---|
  | 21 V / 5 A | 2.2 W | ~175 C | ~218 C | ~205 C |
  | 5 V / 5 A | 1.7 W | ~140 C | ~169 C | - |

- As in v1 (M1), the absolute numbers are pessimistic: the cables roughly double the cooling. The conclusion holds: the
  board area limits 100 W continuous, not the copper.
  - Ground stitching vias every 3 mm change no part by more than 0.2 K.
  - More Q1 drain vias would gain 5-10 K at most.
- Mitigation is already in place:
  - The NTC (RT1) now sits about 3.5 mm from Q1, so it reads Q1's area directly. In v1 it was beside the MCU.
  - The firmware trips at 85 C on the NTC, or at 110 C on its Q1 estimate. The estimate is the NTC plus 32 K x (I / 5 A)^2,
    from the solve: Q1 runs 30 K above RT1 at 5 V / 5 A with 20 mOhm.
  - The AP33772S OTP (OTPTHR = 90 C) is a hardware backstop.
- Bring-up: measure V_GS (TP8 - TP7) and Q1's V_DS at a known current, then set Q1_RISE_5A_DC from the measured R_DS(on).

## Minor (not changed)
- **R14 (VOUT sense series resistor) is 27 R, as RotoPD. The datasheet figure shows 200 R.** The negative swing it
  guarded against is now clamped by D2.
- **Q1 V_GS can reach about -21 V against a +-20 V rating.** This happens if a large output capacitor holds OUT at 21 V
  when PWR_EN pulls the gate low. It is the same as RotoPD. The fix would be a gate-source zener, if wanted.
- **The AP33772S land pattern (JLC's) sits about 0.125 mm further out than the Diodes pattern.** It is still solderable.
- **Exposed-pad paste coverage is 36 % (U1) and 38 % (U4), with open 0.3 mm vias in both pads.** U4's footprint is the
  same file as v1's. Check the first boards' QFN joints.
- **Some capacitors are low after DC bias, and C3 is 6.9 mm from U2's output.**
  - VCC and V5V caps are under 1 uF effective.
  - C3 sits 6.9 mm from U2's output pin, which matters for its 1-10 uF effective requirement.
- **R10 (shunt) has asymmetric pads.** This is the v1 datasheet-land footprint.
- **There is a 0.8 mm VBUS neck at J1's upper VBUS contact, about 7 A/mm local current density.** It is short, so it warms
  locally but does not limit anything. The VBUS run total is 2.5 mOhm, 13 mV at 5 A.
- **USB D+/D- are not impedance controlled and partly cross a void** (bootloader only, full speed, about 20 mm). Same as v1.
- **Three ground pockets have a single via:** the CH552 EP (one hole by design, as v1), the C5 pad, and 1.9 mm2 between
  the UART pads.

## Fixed during the review
- **Output clamp and bulk (added after comparing RotoPD, user 2026-11-11).** RotoPD fits both C6 10 uF and D3 Schottky
  on its output.
  - D2 (SS34, C8678) runs from OUT to GND, sitting above J2 with its anode straight to J2's GND pin. When Q1 (or an OCP
    trip) switches off an inductive load, it holds OUT near -0.4 V, which covers the R14 / VOUT-pin exposure below. In
    use it is reverse-biased: leakage only, no load-current drop.
  - C18 (10 uF 50 V 1206, C13585 Basic) is the output bulk capacitor, below J2.
  - A second, back-to-back FET was considered and declined. It would only block reverse current from a load that holds
    a voltage, at the cost of doubling the switch loss.
- **Q1's Datasheet field pointed at a Winsok WSD3028DN page** (from JLC's EasyEDA data). It now links to the LCSC
  AON7262E datasheet. The part number C176755 was already correct.
- **U1's exposed-pad vias were open vias in the paste area.** They are now plated holes in a project footprint
  (`usbpd:AP33772S_EPVIA`), with 9 paste windows clear of them, like v1's CH552P_EPVIA.
- **U2's tab vias were in the pad.** They now sit beside the tab, with a mask web between: two above and four below.

## Checked and fine
- **AP33772S, pin by pin (DS Rev 10-2):**
  - Caps on V5V, V18, IFB and VCC are present.
  - ISENP is on the connector side of R10, with VCC after it, and both Kelvin taps start at the shunt pads.
  - VSEL floating gives the 5 V default.
  - GPIO, LED, INT, FLIP, DP, DN and the NC pins are open, each with a decision in `pin-decisions.json`.
- **CC1/CC2 crossover:** J1 CC1 goes to U1 pin 16. This is harmless; it only flips the unused FLIP pin and the
  OPMODE.CCFLIP bit.
- **NTC:** NCP15XH103 matches the chip's default NCP03XH103 table, so no TR25-TR100 writes are needed.
- **Q1:** pinout and body-diode direction are correct. With D = VBUS_S and S = OUT, the diode blocks VBUS -> OUT when off.
- **Every IC's pin map and footprint against its datasheet,** including the CH552P QFN16.
- **I2C bus B on P1.0 / P3.2:** levels have good margin at both ends, neither pin has an alternate function active at
  reset, and the ROM bootloader uses neither.
- **DC drops at 5 A:**

  | Run | Drop | Resistance |
  |---|---|---|
  | VBUS | 13 mV | 2.5 mOhm |
  | VBUS_S | 32 mV | 6.1 mOhm |
  | OUT | 5 mV | 1.0 mOhm |
  | GND | 29 mV | 5.7 mOhm |

- **Placement:** nothing but copper under the folded OLED panel (J3's courtyard).
- **Checks:** DRC 0 errors, ERC clean, 0 unconnected, schematic parity clean, board_lint 0 problems, every unused pin
  decided.
- **Remaining DRC warnings are silk only:** J1 and J2 silk clipped at the board edge (both overhang the edge), and J3's
  silk over its own pads.

## Not checked
- **The real PWR_EN voltage:** the datasheet does not state the charge-pump output.
- **PPS keep-alive:** the AP33772S is expected to re-request at least every 10 s itself.
- **JLC placement rotations:** there are no v2 production files yet.
- **Live LCSC stock:** last checked 2026-11-11 in the requirements.
