#!/usr/bin/env python3
"""Generate hardware/usbpd.kicad_sch from the design (docs/requirements.md, tools/nets.py).
Run tools/symbols.py and tools/footprints.py first.

Drawing rules (kicad-pcb-workflow new-design.md s3): one block per area, real wires inside a block, power symbols
for rails and ground, net labels only for signals that travel between blocks (I2C, USB D+/D-, SW_EN). Shunt parts
hang below their rail (ladder); nothing runs through a shunt part's span."""
import os
import sys
import kskills  # noqa: F401
from schgen import Sch, kicad_symbol_dir, find_merges, snap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nets import NETS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HW = os.path.join(ROOT, 'hardware')
KS = kicad_symbol_dir()
G = 2.54

s = Sch('usbpd', paper='A3', title='USB-PD trigger board', rev='1', date='2026-11-10')
for n in ('Device', 'power', 'Switch', 'Connector'):
    s.lib(n, '%s/%s.kicad_sym' % (KS, n))
s.lib('usbpd', os.path.join(HW, 'usbpd.kicad_sym'))

RES = {'2.2k': ('0402WGF2201TCE', 'C25879'), '10k': ('0402WGF1002TCE', 'C25744'),
       '100k': ('0402WGF1003TCE', 'C25741'), '910k': ('0402WGF9103TCE', 'C25800')}
CAPS = {'1u 50V': ('CL21B105KBFNNNE', 'C28323'), '1u': ('CL05A105KA5NQNC', 'C52923'),
        '100n': ('CL05B104KO5NNNC', 'C1525'), '2.2u 16V': ('CL10A225KO8NNNC', 'C23630'),
        '4.7u 25V': ('CL10A475KA8NQNC', 'C69335'), '4.7u': ('CL05A475MP5NRNC', 'C23733')}


def at(part, pin, pt):
    """move `part` so that `pin` lands on `pt`"""
    px, py = part.p(pin)
    part.x, part.y = snap(part.x + pt[0] - px), snap(part.y + pt[1] - py)
    return part


def R(ref, val, rot=0):
    fp, lc = RES[val]
    return s.add('Device:R', ref, val, 0, 0, rot, footprint='JLCImport:' + fp, lcsc=lc)


def C(ref, val, rot=0):
    fp, lc = CAPS[val]
    return s.add('Device:C', ref, val, 0, 0, rot, footprint='JLCImport:' + fp, lcsc=lc)


def gnd(pt, rot=0):
    s.power('GND', pt[0], pt[1], rot)


def rail(net, pt, rot=0):
    s.power(net, pt[0], pt[1], rot, lib_id='power:' + net)


def hang(part, rail_pt, to_gnd=True):
    """shunt part hanging below a rail wire: pin 1 on the rail (declared tap), pin 2 to GND"""
    at(part, 1, rail_pt)
    s.tap(rail_pt)
    if to_gnd:
        gnd(part.p(2))
    return part


def L(pt, dx=0, dy=0):
    return (snap(pt[0] + dx), snap(pt[1] + dy))


# ================================================================== USB-C + PD sink
s.text('USB-C input + PD sink (HUSB238, I2C 0x08)', 25.4, 38.1, 2, True)
j1 = s.add('usbpd:USB_C_16P', 'J1', 'TYPE-C-31-M-12', 40.64, 81.28, footprint='JLCImport:TYPE-C-31-M-12',
           lcsc='C165948', fields={'_val_at': (25.4, 110.49)})
u1 = s.add('usbpd:HUSB238', 'U1', 'HUSB238', 0, 0, footprint='JLCImport:HUSB238_002DD', lcsc='C7471904')
at(u1, 4, (101.6 - 10.16, j1.p('A5')[1]))               # CC rows line up with the connector's
s.wire(j1.p('A5'), u1.p(4))
s.wire(j1.p('B5'), u1.p(5))
# VBUS: up and over the CC pair, C1 hangs from the top run
vb0, vb1 = j1.p('A4B9'), u1.p(1)
top = vb0[1] - 12.7
s.wire(vb0, L(vb0, 5.08), (vb0[0] + 5.08, top), (vb1[0] - 5.08, top), L(vb1, -5.08), vb1)
rail('VBUS', (vb0[0] + 10.16, top)); s.tap((vb0[0] + 10.16, top))
hang(C('C1', '1u 50V'), (vb0[0] + 22.86, top))
# USB data pair leaves the block (to the MCU)
for pin, net in (('A6', 'USB_DP'), ('A7', 'USB_DM')):
    p = j1.p(pin)
    s.wire(p, L(p, 5.08)); s.label(net, *L(p, 5.08))
for pin in ('A8', 'B8'):
    s.nc(j1.p(pin))
g0, sh = j1.p('A1B12'), j1.p('1')
s.wire(g0, L(g0, 0, G)); s.wire(sh, L(sh, 0, G), L(g0, 0, G)); gnd(L(g0, 0, G))
for pin in (2, 3, 9, 10):
    s.nc(u1.p(pin))
gp = u1.p(11); s.wire(gp, L(gp, 0, G)); gnd(L(gp, 0, G))
vs = u1.p(8); s.wire(vs, L(vs, 5.08), L(vs, 5.08, 5.08)); gnd(L(vs, 5.08, 5.08))
for pin, net in ((6, 'SDA'), (7, 'SCL')):
    p = u1.p(pin)
    s.wire(p, L(p, 10.16)); s.label(net, *L(p, 10.16))
s.text('VSET = GND: 5 V request at power-up (DS table 6). ISET open.\n'
       'D+/D- (legacy detection) and GATE (on at POR) unused:\nthe MCU drives its own output switch.',
       63.5, 109.22, 1.27)

# ================================================================== regulators
s.text('Logic supplies', 160.02, 38.1, 2, True)
u2 = s.add('usbpd:SGM2203-5.0', 'U2', 'SGM2203-5.0', 190.5, 66.04, footprint='JLCImport:SGM2203-5_0YK3G_TR',
           lcsc='C4555390')
u3 = s.add('usbpd:XC6206P332MR', 'U3', 'XC6206-3.3', 238.76, 66.04, footprint='JLCImport:XC6206P332MR',
           lcsc='C5446')
y = u2.p(3)[1]
x0 = u2.p(3)[0] - 20.32
s.wire((x0, y), u2.p(3)); rail('VBUS', (x0, y))
hang(C('C2', '1u 50V'), (x0 + 10.16, y))
s.wire(u2.p(1), u3.p(3))
# SGM2203 needs 1-10 uF EFFECTIVE output capacitance (DS p.10 'Output Capacitor Selection'); a 1 uF 0402 X5R keeps
# ~0.5 uF at 5 V bias, so C3 is 4.7 uF 10 V 0402 (~1.5-2 uF at 5 V), same footprint
hang(C('C3', '4.7u'), L(u2.p(1), 7.62))
rail('+5V', L(u2.p(1), 15.24)); s.tap(L(u2.p(1), 15.24))
x3 = u3.p(2)[0] + 17.78
s.wire(u3.p(2), (x3, y)); rail('+3V3', (x3, y))
hang(C('C4', '1u'), L(u3.p(2), 7.62))
for u, pin in ((u2, 2), (u3, 1)):
    p = u.p(pin); s.wire(p, L(p, 0, G)); gnd(L(p, 0, G))
s.text('SGM2203: 36 V in, SOT-89 65 C/W -> <= +36 C at 21 V / 35 mA\n'
       'CH552 needs VCC ~5 V to run its USB bootloader / write flash', 160.02, 93.98, 1.27)

# ================================================================== output switch
s.text('Output switch (OFF through MCU reset)', 287.02, 38.1, 2, True)
q1 = s.add('usbpd:AO4407A', 'Q1', 'AO4407A', 0, 0, rot=90, mirror='y', footprint='JLCImport:AO4407A_C2841482',
           lcsc='C2841482')
at(q1, 1, (320.04, 66.04))                                  # S left, D right, G down
yr = q1.p(1)[1]
xv = 294.64
s.wire((xv, yr), q1.p(1)); rail('VBUS', (xv, yr))
r5 = hang(R('R5', '100k'), (307.34, yr), to_gnd=False)
gq = q1.p(4)
gate = (gq[0], r5.p(2)[1] + 5.08)
s.wire(gq, gate)
s.wire(r5.p(2), (r5.p(2)[0], gate[1]), gate)
r6 = R('R6', '10k')
at(r6, 1, gate)
q2 = s.add('usbpd:2N7002', 'Q2', '2N7002', 0, 0, footprint='JLCImport:2N7002', lcsc='C8545')
at(q2, 3, L(r6.p(2), 0, 5.08))
s.wire(r6.p(2), q2.p(3))
s.wire(q2.p(2), L(q2.p(2), 0, G)); gnd(L(q2.p(2), 0, G))
gq2 = q2.p(1)
sw_en = L(gq2, -15.24)
s.wire(gq2, sw_en); s.label('SW_EN', *sw_en, rot=180)
r7 = R('R7', '2.2k')
hang(r7, L(gq2, -7.62))
j2 = s.add('Connector:Screw_Terminal_01x02', 'J2', 'OUT', 0, 0,
           footprint='TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal',
           fields={'Datasheet': 'https://www.phoenixcontact.com/en-us/products/pcb-terminal-block-mkds-15-2-508-1715721'},
           in_bom=False)
at(j2, 1, (398.78, yr))
# output current shunt R10 (5 mOhm) in the switched line, sensed by U5 (INA226, I2C 0x40)
d = q1.p(5)
r10 = s.add('Device:R', 'R10', '5m', 0, 0, 90, footprint='NoOutline:RLM12FTCMR005', lcsc='C316225')
at(r10, 1, L(d, 15.24))
assert abs(r10.p(2)[1] - yr) < 0.01 and r10.p(2)[0] > r10.p(1)[0]
n1, n2 = L(r10.p(1), -5.08), L(r10.p(2), 5.08)
s.wire(d, r10.p(1))
s.wire(r10.p(2), j2.p(1))
s.label('VOUT_SW', *L(d, 5.08))                             # net names for the board's power class
s.label('VOUT', *L(n2, 5.08))
u5 = s.add('usbpd:INA226', 'U5', 'INA226', 0, 0, footprint='JLCImport:INA226AIDGSR', lcsc='C49851')
at(u5, 9, L(n2, 7.62, 27.94))                              # IN- row below the load-side tap
s.tap(n1); s.tap(n2)
s.wire(n2, (n2[0], u5.p(9)[1]), u5.p(9))
s.wire(n1, (n1[0], u5.p(10)[1]), u5.p(10))
vb = L(u5.p(9), -2.54)
s.tap(vb); s.wire(vb, (vb[0], u5.p(8)[1]), u5.p(8))         # VBUS pin reads the output (load side of the shunt)
vs = u5.p(6); s.wire(vs, L(vs, 0, -7.62)); rail('+3V3', L(vs, 0, -7.62))
hang(C('C15', '100n'), L(vs, 7.62, -7.62)); s.wire(L(vs, 0, -7.62), L(vs, 7.62, -7.62))
gp = u5.p(7); s.wire(gp, L(gp, 0, G)); gnd(L(gp, 0, G))
for pin, net in ((4, 'SDA'), (5, 'SCL')):
    p = u5.p(pin); s.wire(p, L(p, 5.08)); s.label(net, *L(p, 5.08))
s.nc(u5.p(3))
a0, a1 = u5.p(2), u5.p(1)
s.wire(a0, L(a0, 2.54)); s.wire(a1, L(a1, 2.54)); s.wire(L(a0, 2.54), L(a1, 2.54), L(a1, 2.54, G)); s.tap(L(a1, 2.54))
gnd(L(a1, 2.54, G))
p2 = j2.p(2); s.wire(p2, L(p2, -2.54), L(p2, -2.54, 5.08)); gnd(L(p2, -2.54, 5.08))
s.text('VGS = -0.91 x VBUS (R5 100k / R6 10k): -4.3 V at 4.75 V, -19 V at 21 V (abs max 25 V)\n'
       'R7 holds SW_EN low against the CH552 reset pull-up (140 uA max -> 0.3 V)\n'
       'R10 5 mOhm: 25 mV / 0.125 W at 5 A (INA226 range 81.9 mV). U5 A0 = A1 = GND -> I2C 0x40\n'
       'J2: pads for a 5.08 mm terminal or wires (not assembled)', 287.02, 132.08, 1.27)

# ================================================================== MCU, buttons, bootloader entry
s.text('MCU (CH552P) + buttons', 25.4, 144.78, 2, True)
s.text('Hold CYCLE while plugging in: D1 + R4 pull D+ up -> CH552 ROM USB bootloader', 25.4, 152.4, 1.27)
u4 = s.add('usbpd:CH552P', 'U4', 'CH552P', 81.28, 200.66, footprint='usbpd:CH552P_EPVIA', lcsc='C968536')
# +5V: up and over, C6 hangs from the top run
vc = u4.p(16)
yt = vc[1] - 12.7
s.wire(vc, L(vc, -2.54), (vc[0] - 2.54, yt), (vc[0] - 22.86, yt)); rail('+5V', (vc[0] - 22.86, yt))
hang(C('C6', '100n'), (vc[0] - 12.7, yt))
v33 = u4.p(1)
c5x = v33[0] - 27.94
s.wire(v33, (c5x, v33[1]))
c5 = C('C5', '100n'); at(c5, 1, (c5x, v33[1])); gnd(c5.p(2))
s.nc(u4.p(7))
for pin, net in ((14, 'USB_DP'), (15, 'USB_DM')):
    p = u4.p(pin)
    s.wire(p, L(p, -5.08)); s.label(net, *L(p, -5.08), rot=180)
for pin, ref, net in ((10, 'TP6', 'RXD'), (9, 'TP5', 'TXD')):
    p = u4.p(pin)
    tp = s.add('Connector:TestPoint', ref, net, 0, 0, rot=90, footprint='TestPoint:TestPoint_Pad_D1.0mm', in_bom=False,
               fields={'_ref_at': L(p, -19.05), '_val_at': L(p, -24.13)})
    at(tp, 1, L(p, -10.16)); s.wire(p, tp.p(1))
    s.accept_missing(ref, 'Datasheet', 'bare test pad')
gp = u4.p(17); s.wire(gp, L(gp, 0, G)); gnd(L(gp, 0, G))
for pin in (13, 2, 12):          # P3.4, P1.5 (alternate bootloader-entry pin: left alone), P3.3
    s.nc(u4.p(pin))
# board temperature: NTC divider into P3.2 / AIN3. The CH552 ADC reads relative to VCC, so the divider runs from VCC
# (+5V) too: ratiometric (VCC sags to ~4.4 V on a 5 V contract). 85 C = 103 counts, 25 C = 209, ~1.7 counts/K near the
# trip (firmware/tools/gen_ntc_table.py); NTC self-heating ~2.7 mW / 1 mW/K at 85 C, less once soldered
p4 = u4.p(4); s.wire(p4, L(p4, 5.08)); s.label('NTC', *L(p4, 5.08))
node = (114.3, 228.6)
r11 = s.add('Device:R', 'R11', '2.2k', 0, 0, footprint='NoOutline:0402WGF2201TCE', lcsc='C25879'); at(r11, 2, node); rail('+5V', r11.p(1))
rt1 = s.add('Device:Thermistor_NTC', 'RT1', '10k B3380', 0, 0, footprint='NoOutline:NCP15XH103F03RC', lcsc='C77131',
            fields={'Datasheet': 'https://www.lcsc.com/datasheet/C77131.pdf'})
at(rt1, 1, node); gnd(rt1.p(2))
s.wire(node, L(node, -7.62)); s.label('NTC', *L(node, -7.62), rot=180); s.tap(node)
# I2C with pull-ups hanging above
sda, scl = u4.p(5), u4.p(6)
# 150R series: limit current into the 3.3 V parts' clamps if a 5 V CH552 pin ever drives high (INA226 SCL max VS+0.3,
# OLED VDD abs max 4 V; 9.3 mA total). Low level at the bus 0.26 V worst case (3.3 V +2 %, 2.2k -1 %, CH552 33R),
# under the HUSB238's VIL max 0.4 V (the strictest on the bus) with 139 mV margin
for ref, p, end, lab in (('R12', sda, 22.86, 'SDA'), ('R13', scl, 35.56, 'SCL')):
    r = s.add('Device:R', ref, '150', 0, 0, 90, footprint='NoOutline:0402WGF1500TCE', lcsc='C25082')
    at(r, 1, L(p, 2.54)); s.wire(p, r.p(1)); s.wire(r.p(2), L(p, end)); s.label(lab, *L(p, end))
for ref, p, dx in (('R1', sda, 12.7), ('R2', scl, 30.48)):
    r = R(ref, '2.2k'); at(r, 2, L(p, dx)); s.tap(L(p, dx)); rail('+3V3', r.p(1))
# buttons
on, cyc, en = u4.p(11), u4.p(3), u4.p(8)    # P1.1, P1.4, P1.0: the QFN edges facing the button / switch routes
sw1 = s.add('Switch:SW_Push', 'SW1', 'ON/OFF', 0, 0, footprint='JLCImport:TS-1088-AR02016', lcsc='C720477')
at(sw1, 1, L(on, 55.88, 20.32))                           # ON/OFF below R4's USB_DP label
s.wire(on, L(on, 40.64), L(on, 40.64, 20.32), sw1.p(1))
p = sw1.p(2); s.wire(p, L(p, 5.08)); gnd(L(p, 5.08), 90)
sw2 = s.add('Switch:SW_Push', 'SW2', 'CYCLE', 0, 0, footprint='JLCImport:TS-1088-AR02016', lcsc='C720477')
at(sw2, 1, L(cyc, 76.2))                                  # CYCLE runs past the I2C labels before R3 / D1 hang
s.wire(cyc, sw2.p(1))
p = sw2.p(2); s.wire(p, L(p, 5.08)); rail('+3V3', L(p, 5.08), 270)
r3 = R('R3', '10k'); hang(r3, L(cyc, 50.8))
d1 = s.add('Device:D_Schottky', 'D1', 'BAT54WS', 0, 0, rot=90, footprint='JLCImport:BAT54WS_C7502694',
           lcsc='C7502694', fields={'_ref_at': L(cyc, 66.04, 3.81), '_val_at': L(cyc, 66.04, 6.35), '_just': 'left'})
at(d1, 2, L(cyc, 63.5)); s.tap(L(cyc, 63.5))
r4 = R('R4', '10k'); at(r4, 1, d1.p(1)); s.wire(d1.p(1), r4.p(1))
p = r4.p(2); s.wire(p, L(p, 0, G)); s.label('USB_DP', *L(p, 0, G), rot=270)
s.wire(en, L(en, 5.08)); s.label('SW_EN', *L(en, 5.08))
s.text('SW1, SW2: TS-1088 4 x 3 mm, top-actuated (printed plungers)\n'
       'R1/R2 2.2k to 3.3 V: CH552 reset pull-up (140 uA) keeps the bus < 3.7 V (OLED VDD abs max 4 V)\n'
       'R12/R13 150R: firmware runs P1.6/P1.7 open drain, no pull-up (P1_MOD_OC=1, P1_DIR_PU=0); the resistors limit a mistake',
       25.4, 251.46, 1.27)

# ================================================================== OLED
s.text('OLED 0.69in 96x16, internal charge pump (FPC hand-soldered)', 287.02, 144.78, 2, True)
j3 = s.add('usbpd:OLED_0.69_96x16', 'J3', 'OLED 0.69in 96x16', 342.9, 218.44, in_bom=False)
# supplies: VBAT + VDD joined, decoupling hangs from the run
vb, vd = j3.p(5), j3.p(8)
yo = vb[1] - 7.62
s.wire(vb, (vb[0], yo)); s.wire(vd, (vd[0], yo))
s.wire((vb[0] - 7.62, yo), (vd[0] + 25.4, yo)); rail('+3V3', (vb[0] - 7.62, yo))
hang(C('C13', '4.7u'), (vd[0] + 12.7, yo))
hang(C('C14', '100n'), (vd[0] + 25.4, yo))
gp = j3.p(7); s.wire(gp, L(gp, 0, G)); gnd(L(gp, 0, G))
# charge-pump flying caps, left
c2p, c2n, c1p, c1n = j3.p(1), j3.p(2), j3.p(3), j3.p(4)
c7 = C('C7', '1u'); at(c7, 2, L(c2p, -12.7))                # caps stand above their P row
s.wire(c2p, c7.p(2))
t7 = L(c7.p(1), 0, -2.54)
s.wire(c2n, L(c2n, -15.24), (c2n[0] - 15.24, t7[1]), t7, c7.p(1))
c8 = C('C8', '1u'); at(c8, 2, L(c1p, -25.4))
s.wire(c1p, c8.p(2))
t8 = L(c8.p(1), 0, -2.54)
s.wire(c1n, L(c1n, -27.94), (c1n[0] - 27.94, t8[1]), t8, c8.p(1))
# RES#: RC power-on reset, below the flying caps
rs = j3.p(9)
xr = rs[0] - 35.56
s.wire(rs, (xr, rs[1]))
r8 = R('R8', '10k', rot=90); at(r8, 2, (xr, rs[1])); rail('+3V3', r8.p(1), 90)
c9 = C('C9', '100n'); hang(c9, L(rs, -30.48))
for pin, net in ((10, 'SCL'), (11, 'SDA')):
    p = j3.p(pin)
    s.wire(p, L(p, -5.08)); s.label(net, *L(p, -5.08), rot=180)
# right side: each part in its own column, the lowest row nearest the connector
ir, vh, vc2 = j3.p(12), j3.p(13), j3.p(14)
s.wire(ir, L(ir, 5.08)); r9 = R('R9', '910k'); hang(r9, L(ir, 5.08))
s.wire(vh, L(vh, 15.24)); hang(C('C10', '2.2u 16V'), L(vh, 15.24))
s.wire(vc2, L(vc2, 45.72))
hang(C('C11', '4.7u 25V'), L(vc2, 30.48))
hang(C('C12', '100n'), L(vc2, 45.72))
s.nc(j3.p(6))
s.text('VBAT = VDD = 3.3 V (VBAT 3.3-4.2 V with internal DC/DC, N069 DS)\n'
       'R9 910k sets segment current (IREF); RES#: RC power-on reset 10k / 100n', 287.02, 152.4, 1.27)

# ================================================================== rail test pads
s.text('Test pads (bottom side)', 160.02, 109.22, 2, True)
for i, net in enumerate(('VBUS', '+5V', '+3V3', 'GND')):
    ref = 'TP%d' % (i + 1)
    x = 165.1 + i * 17.78
    if net == 'GND':
        tp = s.add('Connector:TestPoint', ref, net, x, 119.38, footprint='TestPoint:TestPoint_Pad_D1.0mm', in_bom=False)
        gnd(tp.p(1))
    else:
        tp = s.add('Connector:TestPoint', ref, net, x, 124.46, rot=180, footprint='TestPoint:TestPoint_Pad_D1.0mm',
                   in_bom=False, fields={'_hide_value': True, '_ref_at': (x + 3.81, 127.0)})
        rail(net, tp.p(1))
    s.accept_missing(ref, 'Datasheet', 'bare test pad')

if __name__ == '__main__':
    col = s.collisions()
    mer = find_merges(s, NETS)
    if col or mer:
        print('COLLISIONS', *col, sep='\n  ')
        print('MERGES', *mer, sep='\n  ')
        sys.exit(1)
    out = os.path.join(HW, 'usbpd.kicad_sch')
    s.write(out)
    print('wrote', out)
