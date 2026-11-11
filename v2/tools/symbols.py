#!/usr/bin/env python3
"""v2 symbols (pins grouped by function, real types from the datasheet pin tables) -> hardware/usbpd.kicad_sym.
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
    # AP33772S W-QFN4040-24 (docs/datasheets/AP33772S_rev10-2.pdf, table 1 p.3); EP = pad 25 (JLC footprint)
    s['AP33772S'] = ic('AP33772S', 10.16,
                       left=[('VCC', ['24'], PI), ('ISENP', ['1'], IN), None, ('CC2', ['16'], BI), ('CC1', ['17'], BI),
                             None, ('DP', ['19'], BI), ('DN', ['18'], BI)],
                       right=[('SDA', ['4'], BI), ('SCL', ['5'], BI), None, ('PWR_EN', ['23'], OUT),
                              ('VOUT', ['22'], IN), None, ('OTP', ['13'], PA), ('IFB', ['15'], PA), ('V5V', ['20'], PO),
                              ('V18', ['12'], PO), None, ('VSEL', ['11'], IN), ('INT', ['9'], OUT), ('LED', ['8'], OUT),
                              ('FLIP', ['6'], OUT), ('GPIO', ['7'], BI)],
                       bottom=[('NC', ['2', '10', '14', '21'], NC), ('GND', ['3', '25'], PI)],
                       desc='USB PD3.1 sink controller with PPS/AVS, I2C 0x52, NMOS gate driver',
                       inherit=J.get('AP33772SDKZ-13-FA02'))
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
    # AON7262E DFN 3x3: 1-3 S, 4 G, 5 D (AOS DS, JLC footprint pads)
    s['AON7262E'] = fet(DEV.get('Q_NMOS'), 'AON7262E', D=['5'], G=['4'], S=['1', '2', '3'],
                        desc='60 V N-MOSFET, 8.5 mOhm at Vgs 4.5 V, Vgs +-20 V', inherit=J.get('AON7262E'))
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
    return s


if __name__ == '__main__':
    p = save_library(build(), os.path.join(HW, 'usbpd.kicad_sym'), 'usbpd', descr='usbpd project symbols')
    print('wrote', os.path.join(HW, 'usbpd.kicad_sym'), 'registered in', p)
