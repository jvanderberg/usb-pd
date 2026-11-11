#!/usr/bin/env python3
"""Generate hardware/usbpd.kicad_sch: the USB-PD PPS board (docs/requirements.md, tools/nets.py).
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

s = Sch('usbpd', paper='A3', title='USB-PD PPS board', rev='1', date='2026-11-11')
for n in ('Device', 'power', 'Switch', 'Connector'):
    s.lib(n, '%s/%s.kicad_sym' % (KS, n))
s.lib('usbpd', os.path.join(HW, 'usbpd.kicad_sym'))

RES = {'2.2k': ('0402WGF2201TCE', 'C25879'), '10k': ('0402WGF1002TCE', 'C25744'),
       '910k': ('0402WGF9103TCE', 'C25800'), '5.1k': ('0402WGF5101TCE', 'C25905'),
       '4.7k': ('0402WGF4701TCE', 'C25900'), '27': ('0402WGF270JTCE', 'C25100')}
CAPS = {'10u 50V': ('CL31A106KBHNNNE', 'C13585'), '1u 50V': ('CL21B105KBFNNNE', 'C28323'), '1u': ('CL05A105KA5NQNC', 'C52923'),
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


# ================================================================== USB-C, input shunt, PD sink, output switch
s.text('USB-C input, PD sink (AP33772S, I2C 0x52), output switch', 25.4, 25.4, 2, True)
j1 = s.add('usbpd:USB_C_16P', 'J1', 'TYPE-C-31-M-12', 40.64, 81.28, footprint='JLCImport:TYPE-C-31-M-12',
           lcsc='C165948', fields={'_val_at': (25.4, 110.49)})
u1 = s.add('usbpd:AP33772S', 'U1', 'AP33772S', 0, 0, footprint='usbpd:AP33772S_EPVIA', lcsc='C50341643')
at(u1, 16, (111.76 - 12.7, j1.p('A5')[1]))              # CC rows line up with the connector's
# J1 CC1 -> U1 pin 16 (CC2) and J1 CC2 -> pin 17 (CC1): the sink treats both alike (only the unused FLIP output
# changes sense); on the board this lets both CC traces run straight from J1 to U1
s.wire(j1.p('A5'), u1.p(16))
s.wire(j1.p('B5'), u1.p(17))
# Rd 5.1k on CC1 / CC2 (AP33772S DS 'Cable Attachment': Rd = 5.1 kOhm on both CC pins; as RotoPD R1 / R2)
for ref, net, x in (('R5', 'CC1', 63.5), ('R6', 'CC2', 76.2)):
    r = R(ref, '5.1k')
    at(r, 1, (x, 119.38)); gnd(r.p(2))
    s.wire(r.p(1), L(r.p(1), 0, -5.08)); s.label(net, *L(r.p(1), 0, -5.08), rot=90)
for pin, net in (('A5', 'CC1'), ('B5', 'CC2')):
    s.label(net, *L(j1.p(pin), 2.54))
# USB data pair leaves the block (CH552 bootloader only; AP33772S DP/DN open)
for pin, net in (('A6', 'USB_DP'), ('A7', 'USB_DM')):
    p = j1.p(pin)
    s.wire(p, L(p, 5.08)); s.label(net, *L(p, 5.08))
for pin in ('A8', 'B8'):
    s.nc(j1.p(pin))
g0, sh = j1.p('A1B12'), j1.p('1')
s.wire(g0, L(g0, 0, G)); s.wire(sh, L(sh, 0, G), L(g0, 0, G)); gnd(L(g0, 0, G))
# VBUS up to the top run, through the 5 mOhm input shunt R10; ISENP taps the connector side, VCC the far side
vb0 = j1.p('A4B9')
top = vb0[1] - 30.48
xr = vb0[0] + 5.08
s.wire(vb0, (xr, vb0[1]), (xr, top))
rail('VBUS', (xr, top - 0.0), 0); s.tap((xr, top))
r10 = s.add('Device:R', 'R10', '5m', 0, 0, 90, footprint='NoOutline:RLM12FTCMR005', lcsc='C316225')
at(r10, 1, (xr + 15.24, top))
assert r10.p(2)[0] > r10.p(1)[0]
s.wire((xr, top), r10.p(1))
n1, n2 = L(r10.p(1), -5.08), L(r10.p(2), 5.08)
s.tap(n1)
isenp, vcc = u1.p(1), u1.p(24)
s.wire(n1, (n1[0], isenp[1]), isenp)
s.wire(r10.p(2), n2); s.tap(n2)
s.wire(n2, (n2[0], vcc[1]), vcc)
xq = u1.p(4)[0] + 55.88                                    # Q1 sits right of U1's pull-ups and caps
s.wire(n2, L(n2, 7.62)); s.wire(L(n2, 7.62), (xq - 25.4, top))
s.label('VBUS_S', *L(n2, 12.7)); s.tap(L(n2, 12.7))
rail('PWR_FLAG', L(n2, 17.78)); s.tap(L(n2, 17.78))       # VBUS_S is fed through the shunt, not by a power pin
hang(C('C1', '1u 50V'), L(n2, 7.62))                       # VCC: 1 uF required (DS pin 24)
s.tap(L(n2, 7.62))
# Q1 AON7262E: D = VBUS_S (left), S = OUT (right), G down to PWR_EN (direct, as RotoPD)
# Datasheet overridden: JLC's EasyEDA data links this part to a Winsok WSD3028DN page (design review)
q1 = s.add('usbpd:AON7262E', 'Q1', 'AON7262E', 0, 0, rot=90, footprint='JLCImport:AON7262E', lcsc='C176755',
           fields={'Datasheet': 'https://www.lcsc.com/datasheet/C176755.pdf'})
at(q1, 5, (xq, top))
assert q1.p(1)[0] > q1.p(5)[0] and q1.p(4)[1] > top, 'Q1 orientation: D left, S right, G down'
s.wire((xq - 25.4, top), q1.p(5))
gq = q1.p(4)
s.wire(gq, L(gq, 0, 7.62)); s.label('PWR_EN', *L(gq, 0, 7.62), rot=270)
j2 = s.add('Connector:Screw_Terminal_01x02', 'J2', 'OUT', 0, 0,
           footprint='TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal',
           fields={'Datasheet': 'https://www.phoenixcontact.com/en-us/products/pcb-terminal-block-mkds-15-2-508-1715721'},
           in_bom=False)
at(j2, 1, (xq + 63.5, top))
s.wire(q1.p(1), j2.p(1))
s.label('OUT', *L(q1.p(1), 5.08))
# VOUT monitor: 27R from OUT to the VOUT pin (as RotoPD R12)
r14 = R('R14', '27', rot=180)
at(r14, 2, L(q1.p(1), 17.78)); s.tap(L(q1.p(1), 17.78))
s.wire(r14.p(1), L(r14.p(1), 0, 5.08)); s.label('VOUT_MON', *L(r14.p(1), 0, 5.08), rot=270)
p2 = j2.p(2); s.wire(p2, L(p2, -2.54), L(p2, -2.54, 5.08)); gnd(L(p2, -2.54, 5.08))
# output: 10 uF bulk and a Schottky clamp (as RotoPD C6 / D3): an inductive load switched off by Q1 (or by an OCP trip)
# drags OUT below ground; D2 holds it near -0.4 V, sparing Q1 and the VOUT pin (abs min -0.3 V). Reverse-biased in use.
hang(C('C18', '10u 50V'), L(q1.p(1), 25.4))
d2 = s.add('Device:D_Schottky', 'D2', 'SS34', 0, 0, rot=270, footprint='JLCImport:SS34_C8678', lcsc='C8678',
           fields={'_ref_at': L(q1.p(1), 43.18, 3.81), '_val_at': L(q1.p(1), 43.18, 6.35), '_just': 'left'})
at(d2, 1, L(q1.p(1), 40.64)); s.tap(L(q1.p(1), 40.64))
assert d2.p(2)[1] > d2.p(1)[1], 'D2: cathode on OUT, anode down to GND'
gnd(d2.p(2))
# U1 right side: bus B, PWR_EN / VOUT, then a staircase of parts hanging from OTP / IFB / V5V / V18
sda, scl = u1.p(4), u1.p(5)
s.wire(sda, L(sda, 12.7)); s.label('SDA5', *L(sda, 12.7))
s.wire(scl, L(scl, 25.4)); s.label('SCL5', *L(scl, 25.4))
for ref, p, dx in (('R7', sda, 5.08), ('R8', scl, 17.78)):
    r = R(ref, '4.7k'); at(r, 2, L(p, dx)); s.tap(L(p, dx)); rail('+5V', r.p(1))
for pin, net in ((23, 'PWR_EN'), (22, 'VOUT_MON')):
    p = u1.p(pin); s.wire(p, L(p, 7.62)); s.label(net, *L(p, 7.62))
rt1 = s.add('Device:Thermistor_NTC', 'RT1', '10k B3380', 0, 0, footprint='NoOutline:NCP15XH103F03RC', lcsc='C77131',
            fields={'Datasheet': 'https://www.lcsc.com/datasheet/C77131.pdf'})
for pin, part, dx in ((13, rt1, 40.64), (15, C('C17', '100n'), 30.48), (20, C('C15', '1u'), 20.32),
                      (12, C('C16', '100n'), 10.16)):
    p = u1.p(pin); s.wire(p, L(p, dx)); hang(part, L(p, dx))
for pin in (11, 9, 8, 6, 7, 19, 18, 2):
    s.nc(u1.p(pin))
gp = u1.p(3); s.wire(gp, L(gp, 0, G)); gnd(L(gp, 0, G))
s.text('Output live at 5 V whenever the firmware is not running (blank / bootloader): accepted (requirements s3).\n'
       'Firmware writes SYSTEM = 0x11 (off), clears PDCONFIG.EPR_MODE_EN and CONFIG.DR_EN within 100 ms of attach.\n'
       'One NMOS: body diode blocks VBUS -> OUT when off (DS allows a single NMOS). PWR_EN direct to the gate (RotoPD).\n'
       'R10 5 mOhm (DS OCP section): ISENP at the connector side, VCC after it. VSEL open = 5 V at attach.\n'
       'NTC on OTP (100 uA source); default table = Murata NCP03XH103, same curve. DP/DN open: moisture detection off.',
       25.4, 142.24, 1.27)

# ================================================================== regulators
s.text('Logic supplies', 287.02, 25.4, 2, True)
u2 = s.add('usbpd:SGM2203-5.0', 'U2', 'SGM2203-5.0', 317.5, 53.34, footprint='JLCImport:SGM2203-5_0YK3G_TR',
           lcsc='C4555390')
u3 = s.add('usbpd:XC6206P332MR', 'U3', 'XC6206-3.3', 365.76, 53.34, footprint='JLCImport:XC6206P332MR',
           lcsc='C5446')
y = u2.p(3)[1]
x0 = u2.p(3)[0] - 20.32
s.wire((x0, y), u2.p(3)); s.label('VBUS_S', x0, y, rot=180)
hang(C('C2', '1u 50V'), (x0 + 10.16, y))
s.wire(u2.p(1), u3.p(3))
# SGM2203 needs 1-10 uF EFFECTIVE output capacitance (DS p.10); 4.7 uF 10 V 0402 keeps ~1.5-2 uF at 5 V
hang(C('C3', '4.7u'), L(u2.p(1), 7.62))
rail('+5V', L(u2.p(1), 15.24)); s.tap(L(u2.p(1), 15.24))
x3 = u3.p(2)[0] + 17.78
s.wire(u3.p(2), (x3, y)); rail('+3V3', (x3, y))
hang(C('C4', '1u'), L(u3.p(2), 7.62))
for u, pin in ((u2, 2), (u3, 1)):
    p = u.p(pin); s.wire(p, L(p, 0, G)); gnd(L(p, 0, G))
s.text('SGM2203 from VBUS_S (after the shunt, as RotoPD): its ~20-40 mA reads as output current (1-2 LSB)\n'
       'PPS clamped >= 5.0 V in firmware: below that the SGM2203 drops out', 287.02, 78.74, 1.27)

# ================================================================== MCU, buttons, bootloader entry
s.text('MCU (CH552P) + buttons', 25.4, 160.02, 2, True)
s.text('Hold SEL (CYCLE) while plugging in: D1 + R4 pull D+ up -> CH552 ROM USB bootloader', 25.4, 167.64, 1.27)
u4 = s.add('usbpd:CH552P', 'U4', 'CH552P', 81.28, 213.36, footprint='usbpd:CH552P_EPVIA', lcsc='C968536')
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
s.nc(u4.p(2))                    # P1.5: alternate bootloader-entry pin, left alone
# bus B (5 V, AP33772S): P3.2 = SDA5, P1.0 = SCL5. UP / DN: P3.3 / P3.4 to GND (internal pull-ups)
for pin, net in ((4, 'SDA5'), (8, 'SCL5'), (12, 'BTN_UP'), (13, 'BTN_DN')):
    p = u4.p(pin); s.wire(p, L(p, 5.08)); s.label(net, *L(p, 5.08))
# I2C with pull-ups hanging above
sda, scl = u4.p(5), u4.p(6)
# 150R series: limit current into the OLED's clamps if a 5 V CH552 pin ever drives high (OLED VDD abs max 4 V;
# 9.3 mA total). Low level at the bus 0.26 V worst case (3.3 V +2 %, 2.2k -1 %, CH552 33R), well under the OLED's VIL
for ref, p, end, lab in (('R12', sda, 22.86, 'SDA'), ('R13', scl, 35.56, 'SCL')):
    r = s.add('Device:R', ref, '150', 0, 0, 90, footprint='NoOutline:0402WGF1500TCE', lcsc='C25082')
    at(r, 1, L(p, 2.54)); s.wire(p, r.p(1)); s.wire(r.p(2), L(p, end)); s.label(lab, *L(p, end))
for ref, p, dx in (('R1', sda, 12.7), ('R2', scl, 30.48)):
    r = R(ref, '2.2k'); at(r, 2, L(p, dx)); s.tap(L(p, dx)); rail('+3V3', r.p(1))
# buttons
on, cyc = u4.p(11), u4.p(3)                 # P1.1 ON/OFF, P1.4 SEL (CYCLE)
sw1 = s.add('Switch:SW_Push', 'SW1', 'ON/OFF', 0, 0, footprint='JLCImport:TS-1088-AR02016', lcsc='C720477')
at(sw1, 1, L(on, 55.88, 20.32))                           # ON/OFF below R4's USB_DP label
s.wire(on, L(on, 40.64), L(on, 40.64, 20.32), sw1.p(1))
p = sw1.p(2); s.wire(p, L(p, 5.08)); gnd(L(p, 5.08), 90)
sw2 = s.add('Switch:SW_Push', 'SW2', 'SEL', 0, 0, footprint='JLCImport:TS-1088-AR02016', lcsc='C720477')
at(sw2, 1, L(cyc, 76.2))                                  # CYCLE runs past the I2C labels before R3 / D1 hang
s.wire(cyc, sw2.p(1))
p = sw2.p(2); s.wire(p, L(p, 5.08)); rail('+3V3', L(p, 5.08), 270)
r3 = R('R3', '10k'); hang(r3, L(cyc, 50.8))
d1 = s.add('Device:D_Schottky', 'D1', 'BAT54WS', 0, 0, rot=90, footprint='JLCImport:BAT54WS_C7502694',
           lcsc='C7502694', fields={'_ref_at': L(cyc, 66.04, 3.81), '_val_at': L(cyc, 66.04, 6.35), '_just': 'left'})
at(d1, 2, L(cyc, 63.5)); s.tap(L(cyc, 63.5))
r4 = R('R4', '10k'); at(r4, 1, d1.p(1)); s.wire(d1.p(1), r4.p(1))
p = r4.p(2); s.wire(p, L(p, 0, G)); s.label('USB_DP', *L(p, 0, G), rot=270)
for ref, val, net, x in (('SW3', 'UP', 'BTN_UP', 198.12), ('SW4', 'DN', 'BTN_DN', 198.12)):
    y0 = 226.06 if ref == 'SW3' else 238.76
    sw = s.add('Switch:SW_Push', ref, val, 0, 0, footprint='JLCImport:TS-1088-AR02016', lcsc='C720477')
    at(sw, 1, (x, y0))
    s.wire(sw.p(1), L(sw.p(1), -5.08)); s.label(net, *L(sw.p(1), -5.08), rot=180)
    p = sw.p(2); s.wire(p, L(p, 5.08)); gnd(L(p, 5.08), 90)
s.text('SW1-SW4: TS-1088 4 x 3 mm, top-actuated (printed plungers). Front: [ON]  [SEL]  [UP][DN]\n'
       'R1/R2 2.2k to 3.3 V: CH552 reset pull-up (140 uA) keeps the bus < 3.7 V (OLED VDD abs max 4 V)\n'
       'R12/R13 150R: firmware runs P1.6/P1.7 open drain, no pull-up (P1_MOD_OC=1, P1_DIR_PU=0); the resistors limit a mistake',
       25.4, 264.16, 1.27)

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
r9 = R('R9', '10k', rot=90); at(r9, 2, (xr, rs[1])); rail('+3V3', r9.p(1), 90)
c9 = C('C9', '100n'); hang(c9, L(rs, -30.48))
for pin, net in ((10, 'SCL'), (11, 'SDA')):
    p = j3.p(pin)
    s.wire(p, L(p, -5.08)); s.label(net, *L(p, -5.08), rot=180)
# right side: each part in its own column, the lowest row nearest the connector
ir, vh, vc2 = j3.p(12), j3.p(13), j3.p(14)
s.wire(ir, L(ir, 5.08)); r11 = R('R11', '910k'); hang(r11, L(ir, 5.08))
s.wire(vh, L(vh, 15.24)); hang(C('C10', '2.2u 16V'), L(vh, 15.24))
s.wire(vc2, L(vc2, 45.72))
hang(C('C11', '4.7u 25V'), L(vc2, 30.48))
hang(C('C12', '100n'), L(vc2, 45.72))
s.nc(j3.p(6))
s.text('VBAT = VDD = 3.3 V (VBAT 3.3-4.2 V with internal DC/DC, N069 DS)\n'
       'R11 910k sets segment current (IREF); RES#: RC power-on reset 10k / 100n', 287.02, 152.4, 1.27)

# ================================================================== test pads
s.text('Test pads (bottom side)', 287.02, 93.98, 2, True)
for i, net in enumerate(('VBUS', '+5V', '+3V3', 'GND', 'OUT', 'PWR_EN')):
    ref = ('TP1', 'TP2', 'TP3', 'TP4', 'TP7', 'TP8')[i]
    x = 292.1 + i * 15.24
    if net == 'GND':
        tp = s.add('Connector:TestPoint', ref, net, x, 104.14, footprint='TestPoint:TestPoint_Pad_D1.0mm', in_bom=False)
        gnd(tp.p(1))
    else:
        tp = s.add('Connector:TestPoint', ref, net, x, 109.22, rot=180, footprint='TestPoint:TestPoint_Pad_D1.0mm',
                   in_bom=False, fields={'_hide_value': True, '_ref_at': (x + 3.81, 111.76)})
        if net in ('OUT', 'PWR_EN'):
            s.wire(tp.p(1), L(tp.p(1), 0, -5.08)); s.label(net, *L(tp.p(1), 0, -5.08), rot=90)
        else:
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
