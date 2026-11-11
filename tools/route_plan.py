"""Route plan for hardware/usbpd.kicad_pcb (kicad-skills router.py; tools/build_board.sh applies it).

Layer plan (2 layers, JLC-2L-1.6, 1 oz):
  B.Cu  VBUS band along the top edge from J1 to Q1's source vias, plus VBUS under the J1 body (joins the two
        receptacle VBUS pads); everything else GND. Signals that must cross the left cluster ride the bottom
        in short lanes: D+/D- (J1 -> U4), BTN_ON + SW_EN (U4 -> under the display), SDA hop to U1, +5V and +3V3.
  F.Cu  parts and most signals; GND pour; Q1 source copper (VBUS) and the VOUT pour from Q1's drains to J2.
The OLED panel lies on the board over x 124.5-151.9 (J3 courtyard): only copper there, no parts.
Coordinates are absolute board mm (origin 100,100; board 67 x 17).
"""
T, B = 'F.Cu', 'B.Cu'
SIG, THIN, PWR = 0.2, 0.15, 0.3
VS = (0.5, 0.3)          # signal via
VP = (0.6, 0.3)          # power / ground via
VB = (0.8, 0.4)          # VBUS via at the receptacle


def build(r):
    n = r.net
    VBUS, GND = n('J1', 'A4B9'), n('J1', 'A1B12')
    DP, DM = n('J1', 'A6'), n('J1', 'A7')
    SDA, SCL = n('U1', '6'), n('U1', '7')
    P5, P33 = n('U2', '1'), n('U3', '2')
    BTN_ON, BTN_V, SW_EN = n('U4', '11'), n('U4', '3'), n('U4', '8')
    TXD, RXD = n('U4', '9'), n('U4', '10')
    VOUT_SW, VOUT = n('Q1', '5'), n('J2', '1')

    # ------------------------------------------------------------------ zones
    outline = [(100.0, 100.0), (167.0, 100.0), (167.0, 117.0), (100.0, 117.0)]
    r.zone(GND, T, outline, prio=0, name='GND_TOP')
    r.zone(GND, B, outline, prio=0, name='GND_BOT')
    r.zone(VBUS, B, [(108.2, 100.0), (157.6, 100.0), (157.6, 104.4), (151.6, 104.4), (151.6, 103.6),
                     (109.3, 103.6), (109.3, 112.2), (103.8, 112.2), (103.8, 105.0), (108.2, 105.0)],
           prio=1, name='VBUS_BAND')
    r.zone(VBUS, T, [(153.3, 100.0), (157.6, 100.0), (157.6, 106.9), (153.3, 106.9)], prio=2, name='Q1_SOURCE')
    # output path: Q1 drains -> VOUT_SW pour -> R10 (5 mOhm shunt, upper pad) -> R10 lower pad -> VOUT pour -> J2
    r.zone(VOUT_SW, T, [(151.9, 109.9), (157.6, 109.9), (157.6, 112.6), (154.5, 112.6), (154.5, 113.9),
                        (151.9, 113.9)], prio=2, name='VOUT_SW')
    r.zone(VOUT, T, [(158.0, 109.9), (167.0, 109.9), (167.0, 117.0), (152.2, 117.0), (152.2, 114.4), (155.0, 114.4),
                     (155.0, 113.0), (158.0, 113.0)], prio=2, name='VOUT')

    # ------------------------------------------------------------------ J1: shell, VBUS, CC, D+/D-
    r.track(T, 0.5, ('J1', 'A1B12'), (107.0, 104.6), ('J1', '4'), net=GND)
    r.track(T, 0.5, ('J1', 'B1A12'), (107.0, 112.4), ('J1', '1'), net=GND)
    # upper VBUS pad: three vias into the VBUS copper under the receptacle
    r.track(T, 0.6, ('J1', 'A4B9'), (108.75, 106.1), net=VBUS)
    r.track(T, 0.8, (108.75, 106.1), (108.75, 104.2), net=VBUS)
    for y in (104.2, 105.15, 106.1):
        r.via((108.75, y), VBUS, *VB)
    # lower VBUS pad: two vias (the D+/D- vias leave no room for more)
    r.track(T, 0.6, ('J1', 'B4A9'), (108.4, 110.9), (108.7, 111.1), (108.7, 111.8), net=VBUS)
    for y in (111.1, 111.8):
        r.via((108.7, y), VBUS, *VP)
    r.track(T, THIN, ('J1', 'A5'), (109.6, 107.25), (109.6, 106.1), ('U1', '4'))
    r.track(T, THIN, ('J1', 'B5'), (110.55, 110.25), (110.55, 106.6), ('U1', '5'))
    # D+/D-: every receptacle pad drops to the bottom; the two columns of vias join each pair
    for pad, at in (('B7', (109.15, 107.75)), ('A7', (109.15, 108.75)),
                    ('A6', (110.05, 108.25)), ('B6', (110.05, 109.25))):
        r.track(T, THIN, ('J1', pad), at)
        r.via(at, n('J1', pad), *VS)
    r.track(B, THIN, (109.15, 107.75), (109.15, 108.75), (109.55, 109.15), (109.55, 112.0), net=DM)
    r.track(B, THIN, (110.05, 108.25), (110.05, 111.2), net=DP)
    r.via((109.55, 112.0), DM, *VS)
    r.via((110.05, 111.2), DP, *VS)
    r.track(T, THIN, (110.05, 111.2), (110.05, 111.6), (110.4, 111.95), ('U4', '14'), net=DP)
    r.track(T, THIN, (109.55, 112.0), (110.0, 112.45), ('U4', '15'), net=DM)

    # ------------------------------------------------------------------ U1 (HUSB238) supply and ground
    r.track(T, 0.4, ('U1', '1'), ('C1', '1'))
    r.track(T, 0.4, ('C1', '1'), (110.0, 103.25), net=VBUS)
    r.via((110.0, 103.25), VBUS, *VP)
    r.track(T, 0.4, ('C1', '2'), (113.0, 103.4), ('U1', '11'))
    r.track(T, 0.25, ('U1', '8'), ('U1', '11'))
    r.track(T, 0.4, ('U1', '11'), (112.6, 107.45), net=GND)       # the DFN's ends are pin-free: via outside the EP
    r.via((112.6, 107.45), GND, *VP)

    # ------------------------------------------------------------------ I2C
    # SCL: U1 -> lane x 115.45 -> U4; the bus row y 110.05 runs right to R2 and J3.
    # U4's own branch of each line goes through its 150R (R13 SCL, R12 SDA); the pull-ups sit on the bus side
    r.track(T, THIN, ('U1', '7'), (115.45, 106.1), (115.45, 110.05), ('R13', '2'), net=SCL)
    r.track(T, THIN, ('R13', '1'), (115.45, 112.45), ('U4', '6'))
    r.track(T, THIN, (115.45, 110.05), ('R2', '2'), ('J3', '10'), net=SCL)
    # SDA: J3 -> row y 110.67 -> lane x 116.0 (R1) -> U4; U1's branch hops under SCL on the bottom.
    r.track(T, THIN, ('J3', '11'), (116.0, 110.67), ('R12', '2'), net=SDA)     # passes R1.2 (pull-up) on the row
    r.track(T, THIN, ('R12', '1'), (116.0, 112.95), ('U4', '5'))
    r.track(T, THIN, ('U1', '6'), (114.95, 106.6))
    r.via((114.95, 106.6), SDA, *VS)
    r.via((117.0, 110.67), SDA, *VS)
    r.track(B, THIN, (117.0, 110.67), (117.0, 108.25), (115.35, 106.6), (114.95, 106.6), net=SDA)

    # ------------------------------------------------------------------ regulators
    r.track(T, 0.6, ('U2', '3'), ('C2', '1'))
    r.track(T, 0.6, ('C2', '1'), (122.6, 102.7), (122.7, 103.3), net=VBUS)
    for x in (122.3, 123.1):
        r.via((x, 103.3), VBUS, *VP)
    r.track(T, 0.4, (122.3, 103.3), (123.1, 103.3), net=VBUS)
    r.track(T, PWR, ('U2', '1'), ('C3', '1'))      # C3 sits right of U2's output pad
    r.track(T, PWR, ('U2', '1'), (120.0, 105.85), (118.365, 105.85), ('U3', '3'))
    r.track(T, PWR, ('U3', '3'), (119.0, 108.0), (119.5, 108.5), (119.5, 109.4), net=P5)
    r.via((119.5, 109.4), P5, *VP)
    # +5V on the bottom around to U4's VCC pin (the U encloses the D+ branch to R4)
    r.track(B, PWR, (119.5, 109.4), (119.5, 116.3), (109.2, 116.3), (109.2, 114.75), net=P5)
    r.via((109.2, 114.75), P5, *VP)
    r.track(T, PWR, (109.2, 114.75), ('C6', '1'), (111.0, 114.52), ('U4', '16'), net=P5)
    # +3V3 trunk: U3 -> C4 -> C13 -> C14 -> J3 VBAT; spine to J3 VDD and the RES pull-up
    # +3V3 trunk: U3 -> C4 -> J3 VBAT (left end of its pad); the OLED's own caps sit above the panel, fed from the
    # right-hand ends of the VBAT and VDD pads (nothing may sit under the folded panel: J3 courtyard)
    r.track(T, PWR, ('U3', '2'), ('C4', '1'), (123.0, 107.32), (127.0, 107.32), (127.37, 106.95), ('J3', '5'), net=P33)
    r.track(T, PWR, ('J3', '5'), (133.2, 106.95), (133.2, 102.62), ('C13', '1'), ('C14', '1'))
    r.track(T, PWR, ('J3', '8'), (133.2, 108.81), (133.2, 106.95))
    r.track(T, PWR, (123.0, 107.32), (123.0, 108.3), ('R2', '1'), net=P33)
    r.via((123.0, 108.3), P33, *VP)
    # +3V3 under the I2C rows: R1, then along the bottom edge to SW2 (branch up to the RES pull-up R8)
    r.track(B, PWR, (123.0, 108.3), (123.0, 116.3), (149.95, 116.3), net=P33)
    r.via((123.0, 111.6), P33, *VP)
    r.track(T, PWR, (123.0, 111.6), ('R1', '1'), net=P33)
    r.track(B, PWR, (135.3, 116.3), (135.3, 115.6), net=P33)
    r.via((135.3, 115.6), P33, *VP)
    r.track(T, PWR, (135.3, 115.6), ('R8', '1'), net=P33)
    r.via((141.685, 116.3), P33, *VP)
    r.track(T, PWR, (141.685, 116.3), ('SW2', '2'), net=P33)
    # regulator / decoupling grounds
    r.track(T, 0.25, ('C4', '2'), (123.6, 105.6), net=GND)
    r.track(T, 0.25, ('C3', '2'), (123.6, 105.6), net=GND)
    r.via((123.6, 105.6), GND, *VP)
    r.via((121.75, 109.5), GND, *VP)
    # U2's GND tab: the bottom under it is the VBUS band and the BTN_ON / SW_EN lanes, so its vias sit just below it
    for x in (116.7, 117.6, 118.5):
        r.track(T, 0.4, (x, 104.4), (x, 105.15), net=GND)
        r.via((x, 105.15), GND, *VP)
    r.track(T, 0.25, ('U3', '1'), (121.75, 109.5), net=GND)

    # ------------------------------------------------------------------ U4 (CH552) local
    # U4's EP ground leaves through the plated hole built into usbpd:CH552P_EPVIA (pad 17)
    r.track(T, SIG, ('U4', '1'), ('C5', '1'))
    r.via((109.2, 113.4), GND, *VP)
    r.track(T, 0.25, ('C6', '2'), (109.2, 113.4), net=GND)
    r.via((111.0, 115.6), GND, *VP)
    r.track(T, 0.25, ('C5', '2'), (111.0, 115.6), net=GND)
    # TXD / RXD to bottom test pads
    r.track(T, THIN, ('U4', '9'), (113.25, 109.6), net=TXD)
    r.via((113.25, 109.6), TXD, *VS)
    r.track(B, SIG, (113.25, 109.6), ('TP5', '1'), net=TXD)    # TP5 sits between the BTN_ON and SW_EN lanes
    r.track(T, THIN, ('U4', '10'), (112.75, 109.1), (111.85, 108.2), (111.2, 108.2), net=RXD)
    r.via((111.2, 108.2), RXD, *VS)
    r.track(B, SIG, (111.2, 108.2), ('TP6', '1'), net=RXD)

    # ------------------------------------------------------------------ board temperature: NTC divider into P3.2 (AIN3)
    NTC = n('U4', '4')
    r.track(T, THIN, ('U4', '4'), (113.25, 114.2), (116.4, 114.2), (116.4, 113.6), net=NTC)
    r.track(T, THIN, ('RT1', '1'), ('R11', '2'), net=NTC)
    # R11 from +5V (= CH552 VCC, the ADC reference): a via onto the +5V bottom run at x 119.5
    r.via((119.5, 114.0), P5, *VP)
    r.track(T, 0.25, (119.5, 114.0), (118.3, 113.6), ('R11', '1'), net=P5)
    r.track(T, 0.25, ('RT1', '2'), (114.25, 113.55), net=GND)
    r.via((114.25, 113.55), GND, *VS)

    # ------------------------------------------------------------------ CYCLE button, bootloader entry
    r.track(T, THIN, ('U4', '3'), (112.75, 114.45), (113.15, 114.85), net=BTN_V)
    r.track(T, SIG, (113.15, 114.85), ('D1', '2'), net=BTN_V)
    r.track(T, SIG, (113.3, 114.85), (113.3, 116.3), (137.315, 116.3), ('SW2', '1'), net=BTN_V)
    r.track(T, SIG, ('R3', '1'), (120.4, 116.3), net=BTN_V)
    r.track(T, 0.25, ('R3', '2'), (120.4, 114.3), net=GND)
    r.via((120.4, 114.3), GND, *VP)
    r.track(T, SIG, ('D1', '1'), ('R4', '1'))
    r.track(T, SIG, ('R4', '2'), (118.333, 115.6), net=DP)
    r.via((118.333, 115.6), DP, *VS)
    r.track(B, THIN, (110.05, 111.2), (110.75, 111.9), (110.75, 113.6), (112.75, 115.6), (118.333, 115.6), net=DP)

    # ------------------------------------------------------------------ BTN_ON and SW_EN: bottom, then under the display
    r.track(T, THIN, ('U4', '11'), (112.25, 110.0), (111.7, 109.8), net=BTN_ON)
    r.via((111.7, 109.8), BTN_ON, *VS)
    r.track(B, THIN, (111.7, 109.8), (111.7, 104.0), (136.8, 104.0), net=BTN_ON)
    r.via((136.8, 104.0), BTN_ON, *VS)
    r.track(T, SIG, (136.8, 104.0), (137.315, 104.0), ('SW1', '1'), net=BTN_ON)
    r.track(T, THIN, ('U4', '8'), (114.55, 111.45), (114.7, 111.3), (114.7, 110.75), net=SW_EN)
    r.via((114.7, 110.75), SW_EN, *VS)
    r.track(B, THIN, (114.7, 110.75), (114.3, 110.35), (114.3, 104.6), (136.0, 104.6), net=SW_EN)
    r.via((136.0, 104.6), SW_EN, *VS)
    r.track(T, SIG, (136.0, 104.6), (146.733, 104.6), ('R7', '1'), ('Q2', '1'), net=SW_EN)

    # ------------------------------------------------------------------ OLED (J3) escape
    r.track(T, SIG, ('J3', '1'), (128.07, 104.47), ('C7', '2'))
    r.track(T, SIG, ('J3', '2'), (127.23, 105.09), ('C7', '1'))
    r.track(T, SIG, ('J3', '3'), (131.48, 105.71), ('C8', '2'))
    r.track(T, SIG, ('J3', '4'), (132.32, 106.33), ('C8', '1'))
    r.track(T, SIG, ('J3', '7'), (132.0, 108.19), net=GND)
    r.via((132.0, 108.19), GND, *VP)
    r.track(T, SIG, ('J3', '9'), (133.9, 109.43), (133.9, 113.4), ('C9', '1'))
    r.track(T, SIG, (133.9, 113.4), (135.3, 113.4), ('R8', '2'), net=n('J3', '9'))
    r.track(T, 0.25, ('C9', '2'), (133.9, 115.6), net=GND)
    r.via((133.9, 115.6), GND, *VP)
    r.track(T, SIG, ('J3', '12'), (126.8, 111.29), ('R9', '1'))
    r.track(T, SIG, ('J3', '13'), (127.9, 111.91), (127.9, 113.4), ('C10', '1'))
    r.track(T, 0.25, ('J3', '14'), (129.7, 113.0), (130.4, 113.0), ('C11', '1'))
    r.track(T, 0.25, (130.4, 113.0), (132.2, 113.0), ('C12', '1'), net=n('J3', '14'))
    r.track(T, 0.3, ('R9', '2'), (127.3, 115.3), ('C10', '2'), ('C11', '2'), (131.9, 115.3), ('C12', '2'), net=GND)
    for x in (129.4, 131.3):
        r.via((x, 115.3), GND, *VP)

    # ------------------------------------------------------------------ output switch
    r.track(T, SIG, ('Q2', '3'), ('R6', '2'))
    r.track(T, SIG, ('R6', '1'), ('R5', '2'), (152.6, 104.7), ('Q1', '4'))
    r.track(T, 0.4, ('R5', '1'), (153.6, 102.167), net=VBUS)
    r.via_field(VBUS, 154.0, 101.0, 157.0, 103.8, pitch=0.9, size=VP[0], drill=VP[1])
    r.track(T, 0.4, ('Q2', '2'), (148.0, 104.0), net=GND)

    # ------------------------------------------------------------------ INA226 (U5) power monitor
    # I2C: from J3's SCL/SDA pads down to the bottom, two rows under the panel, up into lanes left of U5
    r.track(T, THIN, ('J3', '10'), (131.65, 110.05), net=SCL)
    r.via((131.65, 110.05), SCL, *VS)
    r.track(T, THIN, ('J3', '11'), (132.3, 110.67), net=SDA)
    r.via((132.3, 110.67), SDA, *VS)
    r.track(B, THIN, (131.65, 110.05), (142.9, 110.05), net=SCL)
    r.track(B, THIN, (132.3, 110.67), (132.53, 110.9), (143.45, 110.9), net=SDA)
    r.via((142.9, 110.05), SCL, *VS)
    r.via((143.45, 110.9), SDA, *VS)
    r.track(T, THIN, (142.9, 110.05), (142.9, 115.7), ('U5', '5'), net=SCL)
    r.track(T, THIN, (143.45, 110.9), (143.45, 115.2), ('U5', '4'), net=SDA)
    # Kelvin sense: each line leaves its own shunt pad on the side away from the current and meets no pour
    # (RLM12 DS s6: sense traces from the pads' inner edges)
    r.track(T, THIN, ('U5', '10'), (151.0, 113.7), (151.5, 113.2), (152.6, 113.2), net=VOUT_SW)
    r.track(T, THIN, ('U5', '9'), (151.6, 114.2), (151.6, 115.1), (152.6, 115.1), net=VOUT)
    r.track(T, THIN, ('U5', '9'), (149.75, 114.2), (149.75, 114.7), ('U5', '8'), net=VOUT)   # VBUS sense = IN-
    # supply: C15 beside VS, +3V3 from the bottom lane; GND pins (A0, A1 = GND -> address 0x40) to a via under the body
    r.track(T, 0.25, ('U5', '6'), (150.2, 115.7), ('C15', '1'), net=P33)
    r.track(T, 0.25, ('C15', '1'), (149.95, 116.3), net=P33)
    r.via((149.95, 116.3), P33, *VP)
    r.track(T, 0.25, ('U5', '7'), ('C15', '2'), net=GND)
    r.track(T, 0.25, ('U5', '7'), (147.6, 115.2), (147.0, 114.6), net=GND)
    r.track(T, 0.25, ('U5', '1'), (145.9, 113.7), (145.9, 114.2), ('U5', '2'), net=GND)
    r.track(T, 0.25, (145.9, 114.2), (147.0, 114.6), net=GND)
    r.via((147.0, 114.6), GND, *VP)

    # ------------------------------------------------------------------ ground stitching
    for pt in [(141.0, 104.0), (145.0, 104.0), (148.0, 104.0), (150.0, 104.0),
               (136.0, 108.0), (140.0, 108.0), (144.0, 108.0), (148.0, 108.0),
               (136.0, 111.5), (140.0, 111.5), (146.0, 111.5), (148.0, 111.5),
               (159.0, 101.5), (162.0, 101.5), (165.5, 101.5), (159.0, 108.5), (165.5, 108.5),
               (104.5, 114.8), (107.8, 114.8), (102.5, 116.0),
               (125.5, 108.5), (127.6, 108.5)]:      # GND island between the +3V3 trunk and the I2C rows
        r.via(pt, GND, *VP)
