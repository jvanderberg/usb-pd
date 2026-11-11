"""Route plan for hardware/usbpd.kicad_pcb, the USB-PD PPS board (kicad-skills router.py; tools/build_board.sh applies it).

Coordinates below are written relative to the board's top-left corner (A() adds the 100,100 origin); board 67 x 17.

Layer plan (2 layers, JLC-2L-1.6, 1 oz):
  F.Cu  parts and short signals; GND pour; VBUS from J1's upper pad over the top-left to the shunt R10; VBUS_S from
        R10 to the via field into the bottom band; the Q1 drain copper (VBUS_S) under SW4 and above Q1; the OUT pour
        from Q1's sources to J2. Vertical "risers" cross the panel area from the bottom lanes up to the buttons.
  B.Cu  VBUS under J1 (joins the receptacle's two VBUS pads); the I2C-B pair along the top edge (U1 -> U4, past the
        shunt and its VCC/ISENP lines); the VBUS_S band along the top from x 15 to Q1; then a stack of lanes under the
        OLED panel, top to bottom:
            +3V3 (to SW2), BTN_ON, BTN_UP, BTN_DN, PWR_EN, VOUT_MON, NTC
        each ending in a via under its target; D+/D- from J1 to U4 and BTN_V / +5V along the bottom.
The OLED panel lies over x 25-51.3, y 4.5-12.5 when folded: only copper there, no parts.
"""
T, B = 'F.Cu', 'B.Cu'
SIG, THIN, PWR = 0.2, 0.15, 0.3
VS = (0.5, 0.3)          # signal via
VP = (0.6, 0.3)          # power / ground via
VB = (0.8, 0.4)          # VBUS via at the receptacle


def A(x, y):
    return (round(100.0 + x, 4), round(100.0 + y, 4))


def poly(*pts):
    return [A(x, y) for x, y in pts]


# B.Cu lane heights under the panel (relative y)
L33, LON, LUP, LDN, LEN, LVO, LNTC = 4.3, 4.9, 5.5, 6.1, 6.7, 7.3, 7.9


def build(r):
    n = r.net
    VBUS, VBUS_S, GND, OUT = n('J1', 'A4B9'), n('R10', '2'), n('J1', 'A1B12'), n('J2', '1')
    P5, P33 = n('U2', '1'), n('U3', '2')
    DP, DM = n('J1', 'A6'), n('J1', 'A7')
    CC1, CC2 = n('J1', 'A5'), n('J1', 'B5')
    SCL5, SDA5 = n('U1', '5'), n('U1', '4')
    PWR_EN, VOUT_MON, NTC = n('U1', '23'), n('U1', '22'), n('U1', '13')
    BTN_ON, BTN_UP, BTN_DN, BTN_V = n('U4', '11'), n('U4', '12'), n('U4', '13'), n('U4', '3')
    TXD, RXD = n('U4', '9'), n('U4', '10')
    SDA_M, SCL_M = n('U4', '5'), n('U4', '6')
    SCL, SDA = n('J3', '10'), n('J3', '11')
    RES = n('J3', '9')

    def t(layer, w, *pts, net=None):
        r.track(layer, w, *[p if isinstance(p[0], str) else A(*p) for p in pts], net=net)

    def v(x, y, net, size=VS, **kw):
        r.via(A(x, y), net, *size, **kw)

    # ================================================================== zones
    outline = poly((0, 0), (67, 0), (67, 17), (0, 17))
    r.zone(GND, T, outline, prio=0, name='GND_TOP')
    r.zone(GND, B, outline, prio=0, name='GND_BOT')
    # VBUS, F.Cu: J1's upper VBUS pad up a strip and along the top to R10 pad 1 (left part above the receptacle)
    r.zone(VBUS, T, poly((1.5, 0.4), (14.25, 0.4), (14.25, 2.6), (13.3, 2.6), (13.3, 3.0), (9.15, 3.0), (9.15, 6.5),
                         (8.35, 6.5), (8.35, 3.0), (1.5, 3.0)), prio=2, name='VBUS_TOP')
    # VBUS, B.Cu: under J1 from the lower pad's vias, up between the shell legs, along the top edge to the F.Cu zone
    r.zone(VBUS, B, poly((3.4, 0.3), (11.3, 0.3), (11.3, 2.9), (5.8, 2.9), (5.8, 5.4), (8.95, 5.4), (8.95, 7.35),
                         (8.2, 7.35), (8.2, 10.6), (9.2, 10.6), (9.2, 13.6), (3.4, 13.6)), prio=1, name='VBUS_J1')
    # VBUS_S: R10 pad 2 -> F.Cu top band -> via field -> B.Cu band along the top to Q1's drain copper
    r.zone(VBUS_S, T, poly((15.65, 0.4), (21.4, 0.4), (21.4, 3.3), (15.65, 3.3)), prio=2, name='VBUS_S_TOP')
    r.zone(VBUS_S, B, poly((15.3, 1.5), (23.7, 1.5), (23.7, 0.0), (56.0, 0.0), (56.0, 5.6), (52.2, 5.6),
                           (52.2, 3.6), (15.3, 3.6)), prio=1, name='VBUS_S_BAND')
    # Q1 (rot 90) drain pad on the left, joined to the band by the via field above it; C2 sits on its lower end
    r.zone(VBUS_S, T, poly((52.2, 3.5), (55.15, 3.5), (55.15, 10.2), (53.7, 10.2), (53.7, 8.1), (52.2, 8.1)),
           prio=2, name='Q1_DRAIN')
    # OUT: Q1 sources (right of the drain) -> J2 pin 1, below J2's GND pad and right of the 5 V regulator
    r.zone(OUT, T, poly((55.5, 6.05), (61.8, 6.05), (61.8, 7.65), (67.0, 7.65), (67.0, 17.0), (57.6, 17.0),
                        (57.6, 11.6), (55.5, 11.6)), prio=2, name='OUT')

    # ================================================================== J1: VBUS, shell, CC, D+/D-
    # upper VBUS pad -> F.Cu strip; lower pad -> three vias into the B.Cu VBUS copper; B.Cu <-> F.Cu at the top-left
    t(T, 0.6, ('J1', 'A4B9'), (8.75, 6.1), net=VBUS)
    t(T, 0.6, ('J1', 'B4A9'), (8.65, 10.9), (8.65, 13.1), net=VBUS)
    for y in (11.3, 12.2, 13.1):
        v(8.65, y, VBUS, VP)
    for x in (4.2, 5.1, 6.0, 6.9, 7.8):
        for y in (1.0, 2.0):
            v(x, y, VBUS, VP)
    # receptacle GND pads to the shell legs
    t(T, 0.5, ('J1', 'A1B12'), (7.0, 4.75), ('J1', '4'), net=GND)
    t(T, 0.5, ('J1', 'B1A12'), (7.0, 12.25), ('J1', '1'), net=GND)
    # D+/D-: every receptacle pad drops to the bottom. DM joins on column x 8.6 and leaves over the DP column; DP
    # joins on x 9.3. Both run down past the lower VBUS vias and right to U4 (DM lane 13.0 above DP lane 13.5);
    # they swap sides on F.Cu at U4.
    for pad, x, y in (('B7', 8.6, 7.75), ('A7', 8.6, 8.75), ('A6', 9.3, 8.25), ('B6', 9.3, 9.25)):
        t(T, THIN, ('J1', pad), (x, y))
        v(x, y, n('J1', pad))
    t(B, THIN, (8.6, 8.75), (8.6, 7.75), (10.3, 7.75), (10.3, 13.0), (16.2, 13.0), (16.2, 12.6), net=DM)
    t(B, THIN, (9.3, 8.25), (9.3, 9.25), (9.6, 9.55), (9.6, 13.5), (16.9, 13.5), (16.9, 11.6), net=DP)
    v(16.2, 12.6, DM)
    v(16.9, 11.6, DP)
    t(T, THIN, (16.2, 12.6), (16.55, 12.25), ('U4', '15'), net=DM)
    t(T, THIN, (16.9, 11.6), (17.05, 11.75), ('U4', '14'), net=DP)
    # CC: both run under U1 and rise into pins 16 / 17 from the lower right (CC1 inner, CC2 outer)
    t(T, THIN, ('J1', 'A5'), (9.85, 7.25), (9.85, 12.0), (14.0, 12.0), (14.0, 10.6), (13.05, 9.65), ('U1', '16'), net=CC1)
    t(T, THIN, (9.85, 7.25), ('R5', '1'), net=CC1)
    t(T, THIN, ('J1', 'B5'), (9.0, 10.25), (9.4, 10.65), (9.4, 12.3), (9.7, 12.6), (14.6, 12.6), (14.6, 10.5),
      (13.55, 9.45), ('U1', '17'), net=CC2)
    t(T, THIN, (10.4, 12.6), ('R6', '1'), net=CC2)
    t(T, 0.25, ('R5', '2'), (9.75, 4.25), net=GND)
    v(9.75, 4.25, GND, VP)
    v(10.75, 3.6, GND, VP)          # second stitch for the pour pocket above U1's left pins
    t(T, 0.25, ('R6', '2'), (10.4, 15.15), net=GND)
    v(10.4, 15.15, GND, VP)

    # ================================================================== U1 (AP33772S)
    # exposed pad: four plated holes in the footprint (usbpd:AP33772S_EPVIA) to the bottom ground; pin 3 joins it
    t(T, 0.25, ('U1', '3'), (13.05, 5.6), net=GND)
    # ISENP: from R10 pad 1's inner corner (Kelvin), straight down to pin 1
    t(T, THIN, ('R10', '1'), (14.05, 2.6), ('U1', '1'), net=VBUS)
    # VCC (after the shunt) with its 1 uF C1; C1 joins R10 pad 2 directly
    t(T, PWR, ('U1', '24'), (15.75, 5.0), ('C1', '1'), net=VBUS_S)
    t(T, 0.6, ('C1', '1'), (16.4, 2.8), ('R10', '2'), net=VBUS_S)
    t(T, 0.4, ('C1', '2'), (20.1, 4.3), net=GND)
    v(20.1, 4.3, GND, VP)
    # V18, IFB, V5V caps
    t(T, SIG, ('U1', '12'), (10.6, 8.05), ('C16', '1'))
    t(T, SIG, ('U1', '15'), ('C17', '1'))
    t(T, SIG, ('U1', '20'), (15.75, 7.55), ('C15', '1'))
    t(T, 0.25, ('C16', '2'), (10.6, 11.0), (11.5, 11.0), net=GND)
    t(T, 0.25, ('C17', '2'), (12.4, 11.0), (11.5, 11.0), net=GND)
    v(11.5, 11.0, GND, VP)
    v(13.0, 11.15, GND, VP)         # second stitch for the C16 / C17 pocket
    t(T, 0.25, ('C15', '2'), (16.3, 10.65), net=GND)
    v(16.3, 10.65, GND, VP)
    # OTP (NTC) -> via -> bottom, under U1 to x 15.4, up to its lane
    t(T, SIG, ('U1', '13'), (11.5, 9.9), net=NTC)
    v(11.5, 9.9, NTC)
    t(B, SIG, (11.5, 9.9), (15.4, 9.9), (15.4, LNTC), (51.95, LNTC), (51.95, 10.95), net=NTC)
    # PWR_EN and VOUT_MON: vias right of U1, then their lanes
    t(T, SIG, ('U1', '23'), (15.6, 6.05), (16.0, 5.9), net=PWR_EN)
    v(16.0, 5.9, PWR_EN)
    t(B, SIG, (16.0, 5.9), (16.8, LEN), (57.2, LEN), (57.2, 5.1), net=PWR_EN)
    t(T, SIG, ('U1', '22'), (15.8, 6.55), (16.1, 7.0), net=VOUT_MON)
    v(16.1, 7.0, VOUT_MON)
    t(B, SIG, (16.1, 7.0), (16.4, LVO), (56.0, LVO), (56.0, 9.6), net=VOUT_MON)
    # I2C-B (5 V): U1 pins 4/5 -> vias above U1 -> along the top edge on the bottom -> vias -> down to U4
    t(T, THIN, ('U1', '5'), (11.9, 3.75), net=SCL5)
    t(T, THIN, ('U1', '4'), (12.75, 3.6), net=SDA5)
    v(11.9, 3.75, SCL5)
    v(12.75, 3.6, SDA5)
    t(B, THIN, (11.9, 3.75), (11.9, 0.6), (23.4, 0.6), net=SCL5)
    t(B, THIN, (12.75, 3.6), (12.75, 1.15), (22.45, 1.15), net=SDA5)
    v(21.85, 0.6, SCL5)
    v(23.4, 0.6, SCL5)
    v(22.45, 1.15, SDA5)
    t(T, THIN, (21.85, 0.6), (21.85, 11.25), ('U4', '8'), net=SCL5)
    t(T, THIN, (22.45, 1.15), (22.45, 13.6), ('U4', '4'), net=SDA5)
    # pull-ups to +5V: R8 (SCL5) off the lane's end, R7 (SDA5) off the vertical
    t(T, THIN, (23.4, 0.6), ('R8', '2'), net=SCL5)
    t(T, THIN, (22.45, 4.33), ('R7', '2'), net=SDA5)
    t(T, PWR, ('R8', '1'), ('R7', '1'))
    t(T, PWR, ('R8', '1'), (24.1, 2.6), (24.1, 16.2), net=P5)

    # ================================================================== shunt R10 and the VBUS_S path
    for x in (17.9, 18.7, 19.5, 20.3, 21.0):
        for y in (2.0, 2.9):
            v(x, y, VBUS_S, VP)

    # ================================================================== U4 (CH552) local
    # EP ground leaves through the plated hole in usbpd:CH552P_EPVIA (pad 17)
    t(T, SIG, ('U4', '1'), (18.75, 14.0), ('C5', '1'))
    t(T, 0.25, ('U4', '16'), (17.3, 12.75), (16.95, 13.1), ('C6', '1'))
    t(T, 0.25, ('C6', '2'), (16.3, 14.95), net=GND)
    v(16.3, 14.95, GND, VP)
    t(T, 0.25, ('C5', '2'), (18.95, 15.55), net=GND)
    v(18.95, 15.55, GND, VP)
    # +5V: bottom lane from the regulator to C6 (U4 VCC), with TP2 on it and the riser to the I2C-B pull-ups
    t(B, PWR, (49.5, 16.2), (18.0, 16.2), (17.6, 15.9), net=P5)
    v(24.1, 16.2, P5, VP)
    v(17.6, 15.9, P5, VP)
    t(T, PWR, (17.6, 15.9), (17.6, 14.05), (17.25, 13.7), ('C6', '1'), net=P5)
    # TXD / RXD to the bottom test pads
    t(T, THIN, ('U4', '9'), (20.25, 9.95), (20.5, 9.7), net=TXD)
    v(20.5, 9.7, TXD)
    t(B, SIG, (20.5, 9.7), ('TP5', '1'), net=TXD)
    t(T, THIN, ('U4', '10'), (19.75, 9.6), net=RXD)
    v(19.75, 9.6, RXD)
    t(B, SIG, (19.75, 9.6), (19.15, 9.0), ('TP6', '1'), net=RXD)
    v(20.05, 8.55, GND, VP)
    # buttons: risers from U4 up to their lane vias
    t(T, THIN, ('U4', '12'), (18.75, LUP), net=BTN_UP)
    v(18.75, LUP, BTN_UP)
    t(T, THIN, ('U4', '11'), (19.25, 6.2), (20.55, LON), (21.0, LON), net=BTN_ON)
    v(21.0, LON, BTN_ON)
    t(T, THIN, ('U4', '13'), (17.6, 11.25), (17.0, 10.65), (17.0, LDN), net=BTN_DN)
    v(17.0, LDN, BTN_DN)
    # BTN_V (SEL / bootloader entry): U4 pin 3 -> via -> bottom lane y 14.4 right to SW2's riser; on F.Cu along the
    # bottom edge left to D1 (anode) and the pull-down R3. D1 -> R4 -> D+ (lane via under R4).
    t(T, THIN, ('U4', '3'), (19.75, 14.0), (19.6, 14.15), (19.6, 16.4), (19.45, 16.55), (10.56, 16.55), ('D1', '2'),
      net=BTN_V)
    v(19.6, 14.4, BTN_V)
    t(T, SIG, ('D1', '2'), ('R3', '1'), net=BTN_V)
    t(B, SIG, (19.6, 14.4), (38.0, 14.4), (38.415, 13.985), (38.415, 12.25), net=BTN_V)
    v(38.415, 12.25, BTN_V)
    t(T, SIG, ('D1', '1'), ('R4', '1'))
    t(T, SIG, ('R4', '2'), (12.0, 13.5), net=DP)
    v(12.0, 13.5, DP)
    t(T, 0.25, ('R3', '2'), (7.9, 16.0), net=GND)
    v(7.9, 16.0, GND, VP)

    # ================================================================== bus A (OLED, 3.3 V)
    # series resistors and pull-ups in the band below the FPC bend: R13/R2 (SCL) left, R12/R1 (SDA) right;
    # MCU side on the bottom layer under them, OLED side up to the pads' left ends
    t(T, THIN, ('U4', '6'), (21.85, 12.05), net=SCL_M)
    t(T, THIN, ('U4', '5'), (21.85, 12.95), net=SDA_M)
    v(21.85, 12.05, SCL_M)
    v(21.85, 12.95, SDA_M)
    t(B, THIN, (21.85, 12.05), (24.25, 12.05), (24.85, 12.65), (24.85, 13.45), net=SCL_M)
    v(24.85, 13.45, SCL_M)
    t(T, THIN, (24.85, 13.45), ('R13', '1'), net=SCL_M)
    t(B, THIN, (21.85, 12.95), (22.85, 13.95), (30.0, 13.95), (30.5, 13.45), net=SDA_M)
    v(30.5, 13.45, SDA_M)
    t(T, THIN, (30.5, 13.45), ('R12', '1'), net=SDA_M)
    t(T, THIN, ('R2', '2'), ('R13', '2'), (26.43, 10.6), (26.98, 10.05), ('J3', '10'), net=SCL)
    t(T, THIN, ('R1', '2'), ('R12', '2'), (28.0, 13.45), (28.0, 11.07), (28.4, 10.67), ('J3', '11'), net=SDA)

    # ================================================================== +3V3
    # U3 output -> C4; riser at x 44.2 to SW2 and the lane L33 to the OLED supply pins; bottom lane y 15.4 to the
    # pull-ups R2 / R1, the RES pull-up R9 and TP3
    t(T, PWR, ('U3', '2'), ('C4', '1'), net=P33)
    t(T, PWR, ('U3', '2'), (45.6, 13.4), (44.2, 13.4), (44.2, L33), (42.785, 3.3), ('SW2', '2'), net=P33)
    v(44.2, L33, P33, VP)
    v(44.2, 13.4, P33, VP)
    t(B, PWR, (44.2, 13.4), (42.2, 15.4), (25.57, 15.4), net=P33)
    for x, ref in ((39.7, 'R9'), (29.73, 'R1'), (25.57, 'R2')):
        v(x, 15.4, P33, VP)
        t(T, PWR, (x, 15.4), (ref, '1'), net=P33)
    t(B, PWR, (24.9, L33), (44.2, L33), net=P33)
    v(24.9, L33, P33, VP)
    t(T, PWR, (24.9, L33), (24.9, 8.81), ('J3', '8'), net=P33)
    t(T, PWR, (24.9, 6.95), ('J3', '5'), net=P33)
    # right-hand spine: J3 pin 5 -> C13 / C14 above the panel
    # C13 / C14 along the top edge: C13 from the left riser, C14 from the right spine, grounds joined to SW1's
    t(T, PWR, ('J3', '5'), (31.4, 6.95), (31.4, 3.6), (30.0, 3.6), (29.3, 2.9), (29.3, 1.6), ('C14', '1'), net=P33)
    t(T, PWR, (24.9, L33), (24.9, 1.6), (25.3, 1.2), ('C13', '1'), net=P33)
    t(T, 0.25, ('C13', '2'), ('C14', '2'), (27.98, 0.6), (31.22, 0.6), ('SW1', '2'), net=GND)

    # ================================================================== OLED (J3) escape
    # charge-pump caps above the panel, from the pads' left ends (pins 1-4 nest around each other)
    t(T, SIG, ('J3', '1'), (28.07, 4.47), ('C7', '2'))
    t(T, SIG, ('J3', '2'), (27.23, 5.09), ('C7', '1'))
    t(T, SIG, ('J3', '3'), (26.42, 5.71), ('C8', '2'))
    t(T, SIG, ('J3', '4'), (25.58, 6.33), ('C8', '1'))
    # VSS (pin 7) into the pour right of the pads; RES, IREF, VCOMH, VCC to the caps below, from the right-hand ends
    t(T, SIG, ('J3', '7'), (31.6, 8.19), net=GND)
    t(T, SIG, ('J3', '14'), (31.8, 12.53), ('C11', '1'))
    t(T, SIG, (31.8, 12.53), (33.25, 12.53), ('C12', '1'), net=n('J3', '14'))
    t(T, SIG, ('J3', '13'), (34.7, 11.91), ('C10', '1'))
    t(T, SIG, ('J3', '12'), (36.3, 11.29), ('R11', '1'))
    t(T, SIG, ('J3', '9'), (37.6, 9.43), ('C9', '1'))
    t(T, SIG, ('C9', '1'), ('R9', '2'), net=RES)

    # ================================================================== 5 V regulator (U2 below Q1) and 3.3 V LDO
    # VBUS_S from C2 on the drain copper down to U2 pin 3; tab on the top pour, vias above/below it; +5V along the bottom edge
    t(T, 0.4, ('C2', '1'), (54.75, 12.0), (55.6, 12.85), ('U2', '3'), net=VBUS_S)
    for x, y in ((51.4, 12.95), (52.3, 12.95), (51.4, 15.65), (52.3, 15.65), (53.2, 15.65), (54.1, 15.65)):
        v(x, y, GND, VP)      # beside the tab (mask web between), not in it
    t(T, PWR, ('U2', '1'), (55.9, 16.45), (50.3, 16.45), ('C3', '1'), net=P5)
    t(T, PWR, (50.3, 16.45), (49.75, 16.45), (49.5, 16.2), net=P5)
    v(49.5, 16.2, P5, VP)
    t(T, PWR, ('U3', '3'), (43.76, 16.2), net=P5)
    v(43.76, 16.2, P5, VP)

    # ================================================================== buttons: lane ends -> risers
    t(B, SIG, (21.0, LON), (35.585, LON), net=BTN_ON)
    v(35.585, LON, BTN_ON)
    t(T, SIG, (35.585, LON), ('SW1', '1'), net=BTN_ON)
    t(T, SIG, (38.415, 12.25), ('SW2', '1'), net=BTN_V)
    t(B, SIG, (18.75, LUP), (46.015, LUP), net=BTN_UP)
    v(46.015, LUP, BTN_UP)
    t(T, SIG, (46.015, LUP), ('SW3', '1'), net=BTN_UP)
    t(B, SIG, (17.0, LDN), (51.815, LDN), net=BTN_DN)
    v(51.815, LDN, BTN_DN)
    t(T, SIG, (51.815, LDN), ('SW4', '1'), net=BTN_DN)

    # ================================================================== Q1, NTC, VOUT sense, test pads
    for x in (52.7, 53.5, 54.3):
        for y in (3.95, 4.75):
            v(x, y, VBUS_S, VP)
    v(57.2, 5.1, PWR_EN)
    t(T, SIG, (57.2, 5.1), (56.7, 5.62), ('Q1', '4'), net=PWR_EN)
    t(B, SIG, (57.2, LEN), ('TP8', '1'), net=PWR_EN)
    v(51.95, 10.95, NTC)
    t(T, SIG, (51.95, 10.95), ('RT1', '1'), net=NTC)
    t(T, 0.25, ('RT1', '2'), (53.63, 13.35), net=GND)
    v(56.0, 9.6, VOUT_MON)
    t(T, SIG, (56.0, 9.6), ('R14', '1'), net=VOUT_MON)
    # D2 (output clamp) above J2: cathode down into the OUT pour, anode straight to J2's GND pin (short flyback loop)
    t(T, 1.0, ('D2', '1'), (59.0, 6.6), net=OUT)
    t(T, 1.0, ('D2', '2'), ('J2', '2'), net=GND)
    for y in (1.2, 3.4):
        v(65.6, y, GND, VP)          # stitch the corner pour that D2's cathode track cuts off
    # C18 (output bulk) below J2: pin 1 on the OUT pour, pin 2 out to two ground vias
    t(T, 0.6, ('C18', '2'), (64.6, 15.4), net=GND)
    for y in (15.0, 15.9):
        v(64.6, y, GND, VP)
    v(61.6, 14.5, OUT, VP)
    t(B, 0.4, (61.6, 14.5), ('TP7', '1'), net=OUT)
