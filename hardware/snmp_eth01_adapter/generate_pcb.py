#!/usr/bin/env python3
"""
KiCad 7 PCB Generator — SNMP ETH01 Adapter Board
Board: 80 × 60 mm, 2-layer FR4 1.6 mm

Schematics:
  WT32-ETH01 → IO32 (DS18B20 1-Wire)
               IO33 (Gidrolock WSP via 2N3904)
               IO14 (Door sensor 1, PULLUP)
               IO15 (Door sensor 2, PULLUP)
               IO2  (LED Network, output)
               IO4  (LED Alarm, output)
"""

import os

# ─── NET IDs ──────────────────────────────────────────────
GND = 3;  V5  = 1;  V33 = 2
DQ  = 4   # /DATA_1WIRE   IO32
LEAK= 5   # /LEAK_IO      IO33  (collector of Q1)
H2O = 6   # /H2O_SIGNAL   Gidrolock sensor output (5V side)
BASE= 7   # /TR_BASE      Q1 base node
DR1 = 8   # /DOOR1_IO     IO14
DR2 = 9   # /DOOR2_IO     IO15
NL  = 10  # /NET_LED_IO   IO2
AL  = 11  # /ALR_LED_IO   IO4
PLA = 12  # /PWR_LED_A    anode of D1 (after R4)
NLA = 13  # /NET_LED_A    anode of D2 (after R5)
ALA = 14  # /ALR_LED_A    anode of D3 (after R6)

NET_NAMES = {
    0: "",      1: "+5V",       2: "+3V3",      3: "GND",
    4: "/DATA_1WIRE",           5: "/LEAK_IO",
    6: "/H2O_SIGNAL",           7: "/TR_BASE",
    8: "/DOOR1_IO",             9: "/DOOR2_IO",
   10: "/NET_LED_IO",          11: "/ALR_LED_IO",
   12: "/PWR_LED_A",           13: "/NET_LED_A", 14: "/ALR_LED_A",
}

W, H = 80.0, 60.0   # board dimensions [mm]
out  = []

# ─── LOW-LEVEL HELPERS ────────────────────────────────────

def w(s=""): out.append(s)

def net_str(net_id):
    return f'(net {net_id} "{NET_NAMES[net_id]}")' if net_id else ""

def thru_pad(num, x, y, net, size=1.8, drill=1.0, shape="circle"):
    ns = net_str(net)
    w(f'    (pad "{num}" thru_hole {shape}'
      f' (at {x:.3f} {y:.3f})'
      f' (size {size} {size}) (drill {drill})'
      f' (layers "F.Cu" "B.Cu" "F.Mask" "B.Mask") {ns})')

def fp_open(ref, val, cx, cy):
    w(f'  (footprint "{ref}" (layer "F.Cu") (at {cx:.3f} {cy:.3f})')
    w(f'    (fp_text reference "{ref}" (at 0 -3) (layer "F.SilkS")'
      f' (effects (font (size 1 1) (thickness 0.15))))')
    w(f'    (fp_text value "{val}" (at 0 3) (layer "F.Fab")'
      f' (effects (font (size 1 1) (thickness 0.15))))')

def fp_close(): w("  )")

def seg(x1, y1, x2, y2, net, layer="F.Cu", width=0.25):
    if abs(x1-x2) < 1e-4 and abs(y1-y2) < 1e-4: return
    w(f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f})'
      f' (width {width:.2f}) (layer "{layer}") (net {net}))')

def route(pts, net, layer="F.Cu", width=0.25):
    for i in range(len(pts) - 1):
        seg(*pts[i], *pts[i+1], net, layer, width)

def via(x, y, net):
    w(f'  (via (at {x:.3f} {y:.3f}) (size 1.0) (drill 0.6)'
      f' (layers "F.Cu" "B.Cu") (net {net}))')

def edge_line(x1, y1, x2, y2):
    w(f'  (gr_line (start {x1} {y1}) (end {x2} {y2})'
      f' (layer "Edge.Cuts") (width 0.05))')

def silk_text(txt, x, y, size=1.2):
    w(f'  (gr_text "{txt}" (at {x:.1f} {y:.1f})'
      f' (layer "F.SilkS") (effects (font (size {size} {size}) (thickness 0.15))))')

# ─── FOOTPRINT BUILDERS ───────────────────────────────────

def header_1x13(ref, x0, y0, nets):
    """1×13 female header, 2.54 mm pitch, horizontal (pads go right)."""
    fp_open(ref, "PinSocket_1x13_2.54mm", x0 + 15.24, y0)
    for i, net in enumerate(nets):
        thru_pad(str(i+1), x0 + i*2.54 - 15.24, 0, net, size=1.7, drill=1.0, shape="oval")
    fp_close()
    # return list of absolute pad positions
    return [(x0 + i*2.54, y0) for i in range(13)]

def terminal_2pin(ref, val, x0, y0, nets, pitch=5.0):
    """2-pin screw terminal."""
    fp_open(ref, val, x0, y0)
    thru_pad("1", -pitch/2, 0, nets[0], size=2.0, drill=1.2)
    thru_pad("2",  pitch/2, 0, nets[1], size=2.0, drill=1.2)
    fp_close()
    return [(x0 - pitch/2, y0), (x0 + pitch/2, y0)]

def terminal_3pin(ref, val, x0, y0, nets, pitch=2.54):
    """3-pin connector."""
    fp_open(ref, val, x0, y0)
    thru_pad("1", -pitch, 0, nets[0], size=1.8, drill=1.0)
    thru_pad("2",  0,     0, nets[1], size=1.8, drill=1.0)
    thru_pad("3",  pitch, 0, nets[2], size=1.8, drill=1.0)
    fp_close()
    return [(x0 - pitch, y0), (x0, y0), (x0 + pitch, y0)]

def resistor_horiz(ref, val, cx, cy, net1, net2, pitch=10.16):
    """Axial resistor, horizontal, 10.16 mm pitch."""
    fp_open(ref, val, cx, cy)
    thru_pad("1", -pitch/2, 0, net1)
    thru_pad("2",  pitch/2, 0, net2)
    fp_close()
    return (cx - pitch/2, cy), (cx + pitch/2, cy)

def resistor_vert(ref, val, cx, cy, net1, net2, pitch=10.16):
    """Axial resistor, vertical placement (rotated 90°), pitch along Y."""
    fp_open(ref, val, cx, cy)
    thru_pad("1", 0, -pitch/2, net1)
    thru_pad("2", 0,  pitch/2, net2)
    fp_close()
    return (cx, cy - pitch/2), (cx, cy + pitch/2)

def to92(ref, val, cx, cy, nets):
    """TO-92 Inline: pads 1(E), 2(B), 3(C) at -1.27, 0, +1.27 along X."""
    fp_open(ref, val, cx, cy)
    thru_pad("1", -1.27, 0, nets[0], size=1.6, drill=0.9)  # E
    thru_pad("2",  0.00, 0, nets[1], size=1.6, drill=0.9)  # B
    thru_pad("3",  1.27, 0, nets[2], size=1.6, drill=0.9)  # C
    fp_close()
    return (cx-1.27, cy), (cx, cy), (cx+1.27, cy)

def led_5mm(ref, val, cx, cy, net_k, net_a):
    """LED 5 mm THT: pad K at -1.27, pad A at +1.27."""
    fp_open(ref, val, cx, cy)
    thru_pad("K", -1.27, 0, net_k, size=1.6, drill=0.9)
    thru_pad("A",  1.27, 0, net_a, size=1.6, drill=0.9)
    fp_close()
    return (cx-1.27, cy), (cx+1.27, cy)

# ─── MAIN GENERATOR ───────────────────────────────────────

def generate():
    global out
    out = []

    # ── File header ──────────────────────────────────────
    w("(kicad_pcb (version 20221018) (generator pcbnew)")
    w("  (general (thickness 1.6))")
    w('  (paper "A4")')
    w("  (layers")
    layers = [
        (0,  "F.Cu",     "signal"),
        (31, "B.Cu",     "signal"),
        (36, "B.SilkS",  "user",  "B.Silkscreen"),
        (37, "F.SilkS",  "user",  "F.Silkscreen"),
        (38, "B.Mask",   "user"),
        (39, "F.Mask",   "user"),
        (44, "Edge.Cuts","user"),
        (46, "B.CrtYd",  "user",  "B.Courtyard"),
        (47, "F.CrtYd",  "user",  "F.Courtyard"),
        (48, "B.Fab",    "user",  "B.Fab"),
        (49, "F.Fab",    "user",  "F.Fab"),
    ]
    for l in layers:
        if len(l) == 3:
            w(f'    ({l[0]} "{l[1]}" {l[2]})')
        else:
            w(f'    ({l[0]} "{l[1]}" {l[2]} "{l[3]}")')
    w("  )")
    w('  (setup (pad_to_mask_clearance 0.1))')
    w()

    # ── Nets ─────────────────────────────────────────────
    for nid, name in NET_NAMES.items():
        w(f'  (net {nid} "{name}")')
    w()

    # ─────────────────────────────────────────────────────
    # COMPONENT PLACEMENT
    # ─────────────────────────────────────────────────────

    # J1 — WT32-ETH01 top row (GND,3V3,EN,TX,RX,IO5,IO17,IO18,IO19,IO21,IO22,IO23,NC)
    j1_nets = [GND, V33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    j1 = header_1x13("J1", 10.0, 12.0, j1_nets)
    silk_text("WT32-ETH01 ROW1", 25, 7)

    # J2 — WT32-ETH01 bottom row (5V,GND,IO39,IO36,IO35,IO34,IO14,IO15,IO2,IO4,IO0,IO32,IO33)
    j2_nets = [V5, GND, 0, 0, 0, 0, DR1, DR2, NL, AL, 0, DQ, LEAK]
    j2 = header_1x13("J2", 10.0, 30.0, j2_nets)
    silk_text("WT32-ETH01 ROW2", 25, 25)

    # ── Module outline (silkscreen rectangle) ──
    w('  (gr_rect (start 8 8.5) (end 44 33.5) (layer "F.SilkS") (width 0.12))')

    # J3 — Power input 5V (2-pin, 5mm pitch)
    j3 = terminal_2pin("J3", "PWR_IN_5V", 7.5, 4.0, [V5, GND])
    silk_text("+5V IN", 7.5, 1.5, size=0.8)

    # J4 — Gidrolock WSP (2-pin, 5mm pitch)
    j4 = terminal_2pin("J4", "GIDROLOCK_WSP", 7.5, 53.0, [V5, H2O])
    silk_text("LEAK SENSOR", 7.5, 56.5, size=0.8)

    # J5 — Door sensor 1 (2-pin, 5mm pitch)
    j5 = terminal_2pin("J5", "DOOR_1", 20.0, 53.0, [DR1, GND])
    silk_text("DOOR 1", 20.0, 56.5, size=0.8)

    # J6 — Door sensor 2 (2-pin, 5mm pitch)
    j6 = terminal_2pin("J6", "DOOR_2", 32.0, 53.0, [DR2, GND])
    silk_text("DOOR 2", 32.0, 56.5, size=0.8)

    # J7 — DS18B20 connector (3-pin, 2.54mm pitch: GND, DATA, 3V3)
    j7 = terminal_3pin("J7", "DS18B20", 59.54, 4.0, [GND, DQ, V33])
    silk_text("DS18B20", 59.54, 1.5, size=0.8)

    # R1 — 4.7 kΩ DS18B20 pull-up (pad1=3V3, pad2=DATA_1WIRE)
    r1p1, r1p2 = resistor_horiz("R1", "4.7k", 66.0, 19.0, V33, DQ)

    # R2 — 10 kΩ base current limiter (pad1=H2O_SIGNAL, pad2=TR_BASE)
    r2p1, r2p2 = resistor_horiz("R2", "10k", 50.0, 42.0, H2O, BASE)

    # R3 — 10 kΩ base pull-down (pad1=TR_BASE, pad2=GND)
    r3p1, r3p2 = resistor_horiz("R3", "10k", 68.0, 46.0, BASE, GND)

    # R4 — 330 Ω Power LED resistor (pad1=3V3 top, pad2=PWR_LED_A bot)
    r4p1, r4p2 = resistor_vert("R4", "330R", 5.0, 38.96, V33, PLA)

    # R5 — 330 Ω Net LED resistor (pad1=NET_IO top, pad2=NET_LED_A bot)
    r5p1, r5p2 = resistor_vert("R5", "330R", 12.0, 38.96, NL, NLA)

    # R6 — 330 Ω Alarm LED resistor (pad1=ALR_IO top, pad2=ALR_LED_A bot)
    r6p1, r6p2 = resistor_vert("R6", "330R", 19.0, 38.96, AL, ALA)

    # Q1 — 2N3904 NPN (E→GND, B→TR_BASE, C→LEAK_IO)
    q1_e, q1_b, q1_c = to92("Q1", "2N3904", 60.0, 42.0, [GND, BASE, LEAK])
    silk_text("2N3904", 60.0, 38.5, size=0.8)

    # D1 — Power LED (green)
    d1k, d1a = led_5mm("D1", "LED_PWR", 5.0, 46.0, GND, PLA)
    silk_text("PWR", 5.0, 49.5, size=0.8)

    # D2 — Network LED (green)
    d2k, d2a = led_5mm("D2", "LED_NET", 12.0, 46.0, GND, NLA)
    silk_text("NET", 12.0, 49.5, size=0.8)

    # D3 — Alarm LED (red)
    d3k, d3a = led_5mm("D3", "LED_ALR", 19.0, 46.0, GND, ALA)
    silk_text("ALR", 19.0, 49.5, size=0.8)

    # ─────────────────────────────────────────────────────
    # ROUTING
    # ─────────────────────────────────────────────────────
    # Trace widths: 0.5mm power, 0.25mm signal
    PW = 0.5    # power
    SW = 0.25   # signal

    # ── +5V ──────────────────────────────────────────────
    # J3.1(5,4) → J2.1(10,30): left edge power rail
    route([(5.0,4.0),(5.0,30.0),(j2[0][0],j2[0][1])], V5, "F.Cu", PW)
    # +5V → J4.1 (Gidrolock supply)
    route([(5.0,4.0),(5.0,53.0)], V5, "F.Cu", PW)

    # ── +3V3 ─────────────────────────────────────────────
    # J1.2(12.54,12) → R1.1(60.92,19): route below J2 via B.Cu
    via(12.54, 12.0, V33)
    route([(12.54,12.0),(12.54,35.0)], V33, "F.Cu", PW)
    via(12.54, 35.0, V33)
    route([(12.54,35.0),(60.92,35.0),(60.92,19.0)], V33, "B.Cu", PW)
    via(60.92, 19.0, V33)
    # 3V3 → J7.3
    route([(60.92,19.0),(60.92,15.0),(62.08,15.0),(62.08,4.0)], V33, "F.Cu", PW)
    # 3V3 → R4.1
    route([(12.54,35.0),(5.0,35.0),(r4p1[0],r4p1[1])], V33, "B.Cu", PW)
    via(r4p1[0], r4p1[1], V33)

    # ── GND ──────────────────────────────────────────────
    # GND plane on B.Cu — connect via vias
    # J3.2(10,4) GND
    via(10.0, 4.0, GND)
    # J1.1(10,12) GND
    via(10.0, 12.0, GND)
    # J2.2(12.54,30) GND
    via(12.54, 30.0, GND)
    # J4.2(10,53) GND
    via(10.0, 53.0, GND)
    # J5.2(23,53) GND
    via(23.0, 53.0, GND)
    # J6.2(35,53) GND
    via(35.0, 53.0, GND)
    # J7.1(57,4) GND
    via(57.0, 4.0, GND)
    # Q1.E GND
    via(q1_e[0], q1_e[1], GND)
    # R3.2 GND
    via(r3p2[0], r3p2[1], GND)
    # D1.K, D2.K, D3.K GND
    via(d1k[0], d1k[1], GND)
    via(d2k[0], d2k[1], GND)
    via(d3k[0], d3k[1], GND)
    # Connect all GND vias with a B.Cu power net fill stub
    # Horizontal GND bus on B.Cu at y=57
    route([(3.5,57.0),(75.0,57.0)], GND, "B.Cu", PW)
    # Vertical drops to GND bus
    for gx, gy in [(10.0,4.0),(10.0,12.0),(12.54,30.0),(10.0,53.0),(23.0,53.0),
                   (35.0,53.0),(57.0,4.0),(q1_e[0],q1_e[1]),(r3p2[0],r3p2[1]),
                   (d1k[0],d1k[1]),(d2k[0],d2k[1]),(d3k[0],d3k[1])]:
        route([(gx,gy),(gx,57.0)], GND, "B.Cu", PW)

    # ── /DATA_1WIRE  IO32 ─────────────────────────────────
    # J2.12(37.94,30) → R1.2(71.08,19) via B.Cu
    via(37.94, 30.0, DQ)
    route([(37.94,30.0),(37.94,36.0),(71.08,36.0),(71.08,19.0)], DQ, "B.Cu", SW)
    via(71.08, 19.0, DQ)
    # J7.2(59.54,4) → same net, route on F.Cu
    route([(59.54,4.0),(59.54,15.0),(71.08,15.0),(71.08,19.0)], DQ, "F.Cu", SW)

    # ── /LEAK_IO  IO33 ────────────────────────────────────
    # Q1.C(61.27,42) → J2.13(40.48,30)
    route([(q1_c[0],q1_c[1]),(61.27,34.0),(40.48,34.0),(40.48,30.0)], LEAK, "F.Cu", SW)

    # ── /H2O_SIGNAL ──────────────────────────────────────
    # J4.2(10,53) → R2.1(44.92,42) via B.Cu
    via(10.0, 53.0, H2O)   # already a GND via at (10,53)? No J4.2 is H2O not GND
    # Correction: J4 pads: J4[0]=(5,53)=+5V, J4[1]=(10,53)=H2O
    route([(10.0,53.0),(10.0,42.0),(44.92,42.0)], H2O, "F.Cu", SW)

    # ── /TR_BASE ─────────────────────────────────────────
    # R2.2(55.08,42) → Q1.B(60,42) → R3.1(62.92,46)
    route([(r2p2[0],r2p2[1]),(q1_b[0],q1_b[1])], BASE, "F.Cu", SW)
    route([(q1_b[0],q1_b[1]),(q1_b[0],q1_b[1]+2.0),(r3p1[0],r3p1[1]+2.0),(r3p1[0],r3p1[1])], BASE, "F.Cu", SW)

    # ── /DOOR1_IO  IO14 ──────────────────────────────────
    # J2.7(25.24,30) → J5.1(18,53) via B.Cu
    via(25.24, 30.0, DR1)
    route([(25.24,30.0),(25.24,36.0),(18.0,36.0),(18.0,53.0)], DR1, "B.Cu", SW)
    via(18.0, 53.0, DR1)

    # ── /DOOR2_IO  IO15 ──────────────────────────────────
    # J2.8(27.78,30) → J6.1(30,53)
    via(27.78, 30.0, DR2)
    route([(27.78,30.0),(27.78,38.0),(30.0,38.0),(30.0,53.0)], DR2, "B.Cu", SW)
    via(30.0, 53.0, DR2)

    # ── /NET_LED_IO  IO2 ─────────────────────────────────
    # J2.9(30.32,30) → R5.1(12,33.88)
    route([(30.32,30.0),(30.32,33.88),(12.0,33.88)], NL, "F.Cu", SW)

    # ── /ALR_LED_IO  IO4 ─────────────────────────────────
    # J2.10(32.86,30) → R6.1(19,33.88)
    route([(32.86,30.0),(32.86,32.5),(19.0,32.5),(19.0,33.88)], AL, "F.Cu", SW)

    # ── /PWR_LED_A ───────────────────────────────────────
    route([(r4p2[0],r4p2[1]),(d1a[0],d1a[1])], PLA, "F.Cu", SW)

    # ── /NET_LED_A ────────────────────────────────────────
    route([(r5p2[0],r5p2[1]),(d2a[0],d2a[1])], NLA, "F.Cu", SW)

    # ── /ALR_LED_A ────────────────────────────────────────
    route([(r6p2[0],r6p2[1]),(d3a[0],d3a[1])], ALA, "F.Cu", SW)

    # ─────────────────────────────────────────────────────
    # BOARD OUTLINE  (Edge.Cuts)
    # ─────────────────────────────────────────────────────
    edge_line(0, 0, W, 0)
    edge_line(W, 0, W, H)
    edge_line(W, H, 0, H)
    edge_line(0, H, 0, 0)

    # ── Mounting holes ────────────────────────────────────
    for mhx, mhy in [(3.2, 3.2), (76.8, 3.2), (3.2, 56.8), (76.8, 56.8)]:
        w(f'  (footprint "MountingHole:MountingHole_3.2mm_M3" (layer "F.Cu") (at {mhx} {mhy})')
        w(f'    (fp_text reference "MH" (at 0 -3) (layer "F.SilkS")'
          f' (effects (font (size 0.8 0.8) (thickness 0.12))))')
        w(f'    (fp_text value "M3" (at 0 3) (layer "F.Fab")'
          f' (effects (font (size 0.8 0.8) (thickness 0.12))))')
        w(f'    (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask"))')
        w(f'  )')

    # Board title
    silk_text("SNMP-ETH01 ADAPTER v1.0", 40, 58, size=1.0)

    # ── File footer ──────────────────────────────────────
    w(")")  # close kicad_pcb

    return "\n".join(out)


def write_project_file(path):
    content = """{
  "meta": { "filename": "snmp_eth01_adapter.kicad_pro", "version": 1 },
  "board": { "design_settings": { "defaults": { "board_outline_line_width": 0.05 } } },
  "schematic": { "legacy_lib_dir": "", "legacy_lib_list": [] }
}"""
    with open(path, "w") as f:
        f.write(content)


if __name__ == "__main__":
    out_dir = os.path.dirname(os.path.abspath(__file__))

    # Generate .kicad_pcb
    pcb_path = os.path.join(out_dir, "snmp_eth01_adapter.kicad_pcb")
    pcb_text = generate()
    with open(pcb_path, "w") as f:
        f.write(pcb_text)
    print(f"PCB written → {pcb_path}")

    # Generate .kicad_pro
    pro_path = os.path.join(out_dir, "snmp_eth01_adapter.kicad_pro")
    write_project_file(pro_path)
    print(f"Project  → {pro_path}")

    # Print BOM summary
    print("\n── Bill of Materials ──────────────────────────────")
    bom = [
        ("J1, J2", "2×", "1×13 female header 2.54 mm",     "WT32-ETH01 sockets"),
        ("J3, J4", "2×", "2-pin screw terminal 5 mm",       "Power 5V, Gidrolock WSP"),
        ("J5, J6", "2×", "2-pin screw terminal 5 mm",       "Door sensors (reed switches)"),
        ("J7",     "1×", "3-pin header 2.54 mm",            "DS18B20 connector"),
        ("R1",     "1×", "4.7 kΩ 0.25W axial",             "DS18B20 data pull-up"),
        ("R2, R3", "2×", "10 kΩ 0.25W axial",              "2N3904 base bias / pull-down"),
        ("R4,R5,R6","3×","330 Ω 0.25W axial",              "LED current limiting"),
        ("Q1",     "1×", "2N3904 TO-92",                    "Gidrolock WSP NPN interface"),
        ("D1",     "1×", "LED 5mm green",                   "Power indicator"),
        ("D2",     "1×", "LED 5mm green",                   "Network indicator"),
        ("D3",     "1×", "LED 5mm red",                     "Alarm indicator"),
    ]
    for ref, qty, part, note in bom:
        print(f"  {ref:<12} {qty}  {part:<30}  # {note}")
    print("───────────────────────────────────────────────────")
    print("Board: 80 × 60 mm, FR4 1.6 mm, 2-layer, HASL")
    print("Mounting holes: M3 (×4 corners)")
