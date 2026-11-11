#!/usr/bin/env python3
"""Redrawn symbols (pins grouped by function, real types from the datasheet pin tables) -> hardware/usbpd.kicad_sym.
Each inherits the JLCImport symbol's data (LCSC, datasheet, footprint). Pin numbers = JLC footprint pad numbers."""
import os
import kskills  # noqa: F401
from schgen import SymLib, kicad_symbol_dir
from symgen import ic, fet, save_library, PI, PO, IN, OUT, BI, PA, NC, OC

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HW = os.path.join(ROOT, 'hardware')
J = SymLib('JLCImport', os.path.join(HW, 'JLCImport.kicad_sym'))
DEV = SymLib('Device', kicad_symbol_dir() + '/Device.kicad_sym')


def build():
    s = {}
    # HUSB238 (docs/datasheets/HUSB238_002DD.pdf, table 1 p.3)
    s['HUSB238'] = ic('HUSB238', 7.62,
                      left=[('VIN', ['1'], PI), None, ('CC1', ['4'], BI), ('CC2', ['5'], BI), None,
                            ('D+', ['2'], BI), ('D-', ['3'], BI)],
                      right=[('SDA', ['6'], BI), ('SCL', ['7'], IN), None, ('VSET', ['8'], PA), ('ISET', ['9'], PA),
                             None, ('GATE', ['10'], OC)],
                      bottom=[('GND', ['11'], PI)], desc='USB PD sink controller, I2C, 5-20 V',
                      inherit=J.get('HUSB238_002DD'))
    # CH552P QFN-16 (docs/datasheets/CH552E.pdf section 4, QFN16 column); EP = pad 17
    s['CH552P'] = ic('CH552P', 10.16,
                     left=[('VCC', ['16'], PI), ('V33', ['1'], PO), ('RST', ['7'], IN), None,
                           ('P3.6/UDP', ['14'], BI), ('P3.7/UDM', ['15'], BI), None,
                           ('P3.0/RXD', ['10'], BI), ('P3.1/TXD', ['9'], BI)],
                     right=[('P1.6', ['5'], BI), ('P1.7', ['6'], BI), None, ('P1.4', ['3'], BI), ('P1.5', ['2'], BI),
                            ('P3.4', ['13'], BI), None, ('P1.0', ['8'], BI), ('P1.1', ['11'], BI),
                            ('P3.2', ['4'], BI), ('P3.3', ['12'], BI)],
                     bottom=[('GND', ['17'], PI)], desc='8051 MCU with USB, ROM bootloader, QFN-16',
                     inherit=J.get('CH552P'))
    # SGM2203-5.0 SOT-89-3: 1 VOUT, 2 GND (tab), 3 VIN (SGM2203 DS p.4)
    s['SGM2203-5.0'] = ic('SGM2203-5.0', 5.08, left=[('VIN', ['3'], PI)], right=[('VOUT', ['1'], PO)],
                          bottom=[('GND', ['2'], PI)], desc='36 V 150 mA LDO, 5.0 V',
                          inherit=J.get('SGM2203-5_0YK3G_TR'))
    # Torex XC6206P332MR-G SOT-23: 1 VSS, 2 VOUT, 3 VIN (Torex DS pin assignment)
    s['XC6206P332MR'] = ic('XC6206P332MR', 5.08, left=[('VIN', ['3'], PI)], right=[('VOUT', ['2'], PO)],
                           bottom=[('VSS', ['1'], PI)], desc='6 V 200 mA LDO, 3.3 V',
                           inherit=J.get('XC6206P332MR'))
    # USB-C receptacle, 16-pin (HRO TYPE-C-31-M-12 drawing); JLC footprint merges A4/B9 etc.
    s['USB_C_16P'] = ic('USB_C_16P', 7.62,
                        right=[('VBUS', ['A4B9', 'B4A9'], PO), None, ('CC1', ['A5'], BI), ('CC2', ['B5'], BI), None,
                               ('D+', ['A6', 'B6'], BI), ('D-', ['A7', 'B7'], BI), None,
                               ('SBU1', ['A8'], PA), ('SBU2', ['B8'], PA)],
                        bottom=[('GND', ['A1B12', 'B1A12'], PO), ('SHIELD', ['1', '2', '3', '4'], PA)],
                        ref='J', desc='USB Type-C receptacle, 16 pin, 5 A', inherit=J.get('TYPE-C-31-M-12'))
    s['AO4407A'] = fet(DEV.get('Q_PMOS'), 'AO4407A', D=['5', '6', '7', '8'], G=['4'], S=['1', '2', '3'],
                       desc='-30 V -14 A P-MOSFET, Vgs +-25 V', inherit=J.get('AO4407A_C2841482'))
    s['2N7002'] = fet(DEV.get('Q_NMOS'), '2N7002', D=['3'], G=['1'], S=['2'], desc='60 V N-MOSFET',
                      inherit=J.get('2N7002'))
    # 0.69" 96x16 OLED FPC (Wisevision N069-9616TSWPG02-H14, pin definition table 1.5)
    s['OLED_0.69_96x16'] = ic('OLED_0.69_96x16', 7.62,
                              left=[('C2P', ['1'], PA), ('C2N', ['2'], PA), ('C1P', ['3'], PA), ('C1N', ['4'], PA),
                                    None, ('RES#', ['9'], IN), ('SCL', ['10'], IN), ('SDA', ['11'], BI)],
                              right=[('VCC', ['14'], PO), ('VCOMH', ['13'], PO), ('IREF', ['12'], PA),
                                     None, ('VBREF', ['6'], NC)],
                              top=[('VBAT', ['5'], PI), ('VDD', ['8'], PI)],
                              bottom=[('VSS', ['7'], PI)], ref='J',
                              footprint='usbpd:OLED_0.69in_96x16_FPC14',
                              datasheet='https://www.lcsc.com/datasheet/C5123561.pdf',
                              desc='0.69in 96x16 COG OLED, SSD1306/SSD1315 I2C, 14-pin 0.62 mm FPC (hand-soldered)')
    # INA226 VSSOP-10 (docs/datasheets/INA226.pdf table 4-1)
    s['INA226'] = ic('INA226', 7.62,
                     left=[('VBUS', ['8'], IN), ('IN-', ['9'], IN), ('IN+', ['10'], IN)],   # shunt taps drop in uncrossed
                     right=[('SDA', ['4'], BI), ('SCL', ['5'], IN), None, ('ALERT', ['3'], OC), None,
                            ('A0', ['2'], IN), ('A1', ['1'], IN)],
                     top=[('VS', ['6'], PI)], bottom=[('GND', ['7'], PI)],
                     desc='36 V current / voltage / power monitor, I2C, 16-bit', inherit=J.get('INA226AIDGSR'))
    return s


if __name__ == '__main__':
    p = save_library(build(), os.path.join(HW, 'usbpd.kicad_sym'), 'usbpd', descr='usbpd project symbols')
    print('wrote', os.path.join(HW, 'usbpd.kicad_sym'), 'registered in', p)
