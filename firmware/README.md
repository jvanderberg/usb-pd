# usbpd firmware (CH552P + AP33772S)

Build with SDCC (`brew install sdcc`), flash with [wchisp](https://github.com/ch32-rs/wchisp).

```
make          # build/usbpd.hex, build/usbpd.bin (12.6 KB of 14 KB)
make test     # host tests of src/logic.c and the OLED partial flush (src/oled.c against a panel model)
make screens  # ../docs/images/screen-*.png: what the firmware draws, rendered on the host
make flash    # hold SEL while plugging the board into USB, then run this
```

- `src/logic.c`: board-independent logic: AP33772S PDO words, the PPS range and APDO choice, PD_REQMSG words,
  hold-to-accelerate steps, thermal trip, DataFlash settings log, the two screen rows. Tested on the host by
  `test/test_logic.c`.
- `src/hw.c`: clock (16 MHz), pins, 1 ms timer with 4-button debounce, DataFlash, watchdog, two bit-banged I2C buses:
  bus A (P1.6 / P1.7, 3.3 V) for the OLED, bus B (P1.0 SCL5 / P3.2 SDA5, 5 V) for the AP33772S at 0x52.
- `src/oled.c`: 96x16 SSD1306/SSD1315 panel.
- `src/main.c`: behaviour (../docs/requirements.md s3):
  - boot: inside the AP33772S's 100 ms window, `SYSTEM` = 0x11 (output off), `PDCONFIG` = 0x02 (EPR off, PPS on),
    `CONFIG` = 0x78 (de-rating off), `OTPTHR` = 90 C; read `SRCPDO`; restore the saved fixed voltage or PPS setpoint
    and the ON state (only when that contract was obtained);
  - ON/OFF: `SYSTEM` 0x12 / 0x11; after a fault, a fresh `PD_REQMSG` first;
  - SEL: 5V -> 9V -> ... -> 20V -> PPS (only what the charger offers), output off first;
  - PPS: UP / DOWN = 100 mV taps, 100 / 500 / 1000 mV steps at 10/s after 0.4 / 1 / 2.5 s; UP + DOWN switches to the
    current limit (0.25 A codes, 1.00-5.00 A). A `>` marks the value being edited. Requests go out at most every
    150 ms (latest wins) and at most 1 V apart, so UVP (80 % of VREQ) and OVP (VREQ + 2 V) do not trip while the
    charger slews. `OCPTHR` follows the PPS limit (trips at 110 %); fixed contracts use 110 % of the PDO's I_max;
  - every 100 ms: `STATUS` (faults -> output off, shown as HOT / OVER I / OVER V / UNDER V), `VOLTAGE`,
    `CURRENT`, `TEMP`; firmware thermal trip (NTC 85 C, or Q1 estimated 110 C) with lockout to 65 C;
  - settings saved 2 s after the last change; ON + SEL held 5 s = ROM bootloader (output forced off first).

Bring-up checks on the real board: bus B ACKs at 0x52 within the 100 ms window (scope SCL5 at power-up), bus A
~100-250 kHz, `TEMP` near room temperature (default NCP03XH103 table = the fitted NCP15XH103), each fixed PDO and a
PPS sweep on a known charger, PPS keep-alive past 10 s (the AP33772S is expected to do it, requirements s10),
UVP / OCP behaviour when the charger limits current.
