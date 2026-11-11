"""Intended netlist of the USB-PD PPS board (docs/requirements.md). REF.PIN = footprint pad numbers.

U1 AP33772S (25 = EP), U2 SGM2203-5.0, U3 XC6206-3.3, U4 CH552P (17 = EP), Q1 AON7262E (1-3 S, 4 G, 5 D),
R10 5 mOhm input shunt (ISENP at the connector side, VCC after it, as CentyLab RotoPD)."""
NETS = {
    # power path: connector -> shunt -> (AP33772S VCC, Q1 drain, 5 V LDO); Q1 source -> output pads
    'VBUS': ['J1.A4B9', 'J1.B4A9', 'R10.1', 'U1.1', 'TP1.1'],
    'VBUS_S': ['R10.2', 'U1.24', 'C1.1', 'Q1.5', 'U2.3', 'C2.1'],
    'OUT': ['Q1.1', 'Q1.2', 'Q1.3', 'J2.1', 'R14.2', 'TP7.1', 'C18.1', 'D2.1'],
    'VOUT_MON': ['R14.1', 'U1.22'],
    'PWR_EN': ['U1.23', 'Q1.4', 'TP8.1'],
    'GND': ['J1.A1B12', 'J1.B1A12', 'J1.1', 'J1.2', 'J1.3', 'J1.4', 'U1.3', 'U1.25', 'C1.2', 'C2.2', 'U2.2', 'C3.2',
            'U3.1', 'C4.2', 'U4.17', 'C5.2', 'C6.2', 'R3.2', 'R5.2', 'R6.2', 'SW1.2', 'SW3.2', 'SW4.2', 'J2.2',
            'J3.7', 'C9.2', 'C10.2', 'C11.2', 'C12.2', 'C13.2', 'C14.2', 'R11.2', 'TP4.1', 'C15.2', 'C16.2',
            'C17.2', 'RT1.2', 'C18.2', 'D2.2'],
    '+5V': ['U2.1', 'C3.1', 'U3.3', 'U4.16', 'C6.1', 'TP2.1', 'R7.1', 'R8.1'],
    '+3V3': ['U3.2', 'C4.1', 'R1.1', 'R2.1', 'SW2.2', 'J3.5', 'J3.8', 'R9.1', 'C13.1', 'C14.1', 'TP3.1'],
    # AP33772S
    # CC1/CC2 cross over at the AP33772S (J1 CC1 -> U1 pin 16 'CC2'): the sink treats both CC pins alike (only the
    # unused FLIP output changes sense), and with U1 rotated 180 deg toward J1 the two traces then run straight
    'CC1': ['J1.A5', 'U1.16', 'R5.1'],
    'CC2': ['J1.B5', 'U1.17', 'R6.1'],
    'V5V': ['U1.20', 'C15.1'],
    'V18': ['U1.12', 'C16.1'],
    'IFB': ['U1.15', 'C17.1'],
    'NTC': ['U1.13', 'RT1.1'],
    # I2C bus B (5 V): CH552 P3.2 / P1.0 <-> AP33772S, 4.7k to +5V
    'SDA5': ['U4.4', 'U1.4', 'R7.2'],
    'SCL5': ['U4.8', 'U1.5', 'R8.2'],
    # I2C bus A (3.3 V): CH552 P1.6 / P1.7 -> 150R -> OLED, 2.2k to +3V3
    'SDA_MCU': ['U4.5', 'R12.1'],
    'SCL_MCU': ['U4.6', 'R13.1'],
    'SDA': ['R12.2', 'R1.2', 'J3.11'],
    'SCL': ['R13.2', 'R2.2', 'J3.10'],
    # USB (bootloader only)
    'USB_DP': ['J1.A6', 'J1.B6', 'U4.14', 'R4.2'],
    'USB_DM': ['J1.A7', 'J1.B7', 'U4.15'],
    # buttons: ON P1.1 / UP P3.3 / DN P3.4 to GND (internal pull-ups); SEL = CYCLE P1.4 to 3V3 + bootloader entry
    'BTN_ON': ['U4.11', 'SW1.1'],
    'BTN_V': ['U4.3', 'SW2.1', 'R3.1', 'D1.2'],
    'BOOT': ['D1.1', 'R4.1'],
    'BTN_UP': ['U4.12', 'SW3.1'],
    'BTN_DN': ['U4.13', 'SW4.1'],
    'V33': ['U4.1', 'C5.1'],
    'TXD': ['U4.9', 'TP5.1'],
    'RXD': ['U4.10', 'TP6.1'],
    # OLED
    'OLED_C2P': ['J3.1', 'C7.1'],
    'OLED_C2N': ['J3.2', 'C7.2'],
    'OLED_C1P': ['J3.3', 'C8.1'],
    'OLED_C1N': ['J3.4', 'C8.2'],
    'OLED_RES': ['J3.9', 'R9.2', 'C9.1'],
    'OLED_IREF': ['J3.12', 'R11.1'],
    'OLED_VCOMH': ['J3.13', 'C10.1'],
    'OLED_VCC': ['J3.14', 'C11.1', 'C12.1'],
}
