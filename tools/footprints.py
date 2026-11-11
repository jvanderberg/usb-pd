#!/usr/bin/env python3
"""Project footprints that JLC does not supply (hand-soldered parts). Writes hardware/usbpd.pretty/ and registers it.

OLED_0.69in_96x16_FPC14: solder pads for the 14-way folding FPC of a 0.69" 96x16 COG OLED
(Wisevision N069-9616TSWPG02-H14 drawing, docs/datasheets/N069-9616TSWPG02-H14.pdf p.5):
  contact pitch 0.62 mm, 14 contacts (P0.62 x 13 = 8.06 mm), contact width 0.32 mm, contact length ~2 mm,
  FPC 9 mm wide, 10.49 mm from the panel edge to the contact end; panel 26.3 x 8.0 x 1.2 mm.
Pads are 0.38 x 2.6 mm (longer than the contact so an iron can reach), pin 1 at -x. The FPC is soldered flat
first, then folded 180 deg so the panel lies on the board over the pads (double-sided tape). The F.Fab outline
shows where the folded panel lands (fold allowance ~1.5 mm, approximate: check the real part before the case).
"""
import os
import kskills  # noqa: F401  (puts kicad-skills on sys.path, checks the pin)
from kicad_sexpr import Node, LibTable
from kicad_sexpr.board import new_footprint, fp_line, fp_rect
from kicad_sexpr.sexp import Sym, dumps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HW = os.path.join(ROOT, 'hardware')
LIB = os.path.join(HW, 'usbpd.pretty')

PITCH, N, PW, PL = 0.62, 14, 0.38, 2.6
FPC_LEN = 10.49        # panel edge -> contact end
PANEL_L, PANEL_W = 26.3, 8.0
FOLD = 1.5             # length taken by the 180 deg bend (approximate)


def pad(num, x, y, w, h):
    return Node('pad', str(num), Sym('smd'), Sym('rect'), Node('at', round(x, 4), round(y, 4)),
                Node('size', w, h), Node('layers', 'F.Cu', 'F.Mask', 'F.Paste'))


def oled():
    name = 'OLED_0.69in_96x16_FPC14'
    items = []
    x0 = -(N - 1) * PITCH / 2
    for i in range(N):
        items.append(pad(i + 1, x0 + i * PITCH, 0, PW, PL))
    # contact end at y = -1.3 (pad top). FPC runs toward +y, bends, panel comes back over the pads toward -y.
    # simple model: the FPC (plus bend allowance) folds in half -> the panel edge sits at the bend line
    yb = round(-PL / 2 + (FPC_LEN + FOLD) / 2, 3)
    edge = yb
    items.append(fp_rect((-PANEL_W / 2, edge - PANEL_L), (PANEL_W / 2, edge), 'F.Fab', 0.1))
    items.append(fp_line((-4.5, -PL / 2), (-4.5, yb), 'F.Fab', 0.1))
    items.append(fp_line((4.5, -PL / 2), (4.5, yb), 'F.Fab', 0.1))
    # silk: pin-1 mark and the panel outline corners (glass area)
    items.append(fp_line((x0 - 0.6, -PL / 2 - 0.4), (x0 - 0.6, PL / 2 + 0.4), 'F.SilkS', 0.12))
    for sx in (-1, 1):
        items.append(fp_line((sx * (PANEL_W / 2 + 0.2), edge), (sx * (PANEL_W / 2 + 0.2), edge - PANEL_L), 'F.SilkS', 0.12))
    items.append(fp_rect((-PANEL_W / 2 - 0.4, edge - PANEL_L - 0.25), (PANEL_W / 2 + 0.4, max(edge, PL / 2) + 0.25),
                         'F.CrtYd', 0.05))
    fp = new_footprint(name, descr='0.69in 96x16 OLED (SSD1306/SSD1315), 14-pin 0.62 mm folding FPC, hand-soldered',
                       tags='OLED FPC 0.62', attr=('smd', 'exclude_from_bom', 'exclude_from_pos_files'), items=items)
    return name, fp


def main():
    os.makedirs(LIB, exist_ok=True)
    for name, fp in (oled(),):
        p = os.path.join(LIB, name + '.kicad_mod')
        text = dumps(fp)
        open(p, 'w').write(text)
        print('wrote', p)
    t = LibTable.for_project(HW, 'fp')
    if t.add('usbpd', LIB, descr='usbpd project footprints'):
        t.save()
        print('registered usbpd in', t.path)


if __name__ == '__main__':
    main()
