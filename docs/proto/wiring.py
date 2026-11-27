#!/usr/bin/env python3
"""Wiring drawing for the first BusCraft prototype: Raspberry Pi Pico W wired straight to the IEEE-488 connector on a
2.54 mm prototyping board (no bus driver chips). Top view, positions in board holes.

    python3 docs/proto/wiring.py docs/proto/pico-direct-wiring.svg
"""
import sys

U = 22            # px per hole
OX, OY = 40, 120   # board origin in the drawing

# Connector solder tails: two rows of 12 along the top edge, starting at column 1.
# CHECK: which tail is GPIB pin 1. Verify against the numbers moulded on the connector body or with a continuity test.
J1_COL0, J1_ROW_A, J1_ROW_B = 1, 1, 2
def j1_pos(pin):
    """Position of GPIB pin 1..24: row A = pins 1-12, row B = pins 13-24, pin 1 and pin 13 at the left."""
    if pin <= 12:
        return J1_COL0 + pin - 1, J1_ROW_A
    return J1_COL0 + pin - 13, J1_ROW_B

# Pico W, USB to the left. Left header (pins 1-20, GP0..GP15) is the lower row, right header (pins 40-21) the upper row.
PICO_COL0, PICO_ROW_TOP, PICO_ROW_BOT = 2, 12, 19
def pico_pos(pin):
    if pin <= 20:
        return PICO_COL0 + pin - 1, PICO_ROW_BOT
    return PICO_COL0 + 40 - pin, PICO_ROW_TOP

PICO_NAMES = {1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5", 8: "GND", 9: "GP6", 10: "GP7",
              11: "GP8", 12: "GP9", 13: "GND", 14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13", 18: "GND", 19: "GP14",
              20: "GP15", 40: "VBUS", 39: "VSYS", 38: "GND", 36: "3V3"}

# (GPIB pin, signal, Pico pin, colour group)
WIRES = [
    (1, "DIO1", 1, "dio"), (2, "DIO2", 2, "dio"), (3, "DIO3", 4, "dio"), (4, "DIO4", 5, "dio"),
    (13, "DIO5", 6, "dio"), (14, "DIO6", 7, "dio"), (15, "DIO7", 9, "dio"), (16, "DIO8", 10, "dio"),
    (6, "DAV", 11, "hs"), (7, "NRFD", 12, "hs"), (8, "NDAC", 14, "hs"),
    (5, "EOI", 15, "mg"), (11, "ATN", 16, "mg"), (9, "IFC", 17, "mg"), (10, "SRQ", 19, "mg"), (17, "REN", 20, "mg"),
    (24, "GND", 3, "gnd"), (18, "GND", 8, "gnd"),
]
GND_BRIDGE = list(range(18, 25))   # GPIB pins 18-24 are all ground returns: bridge them together on the board

COLOURS = {"dio": "#2f6fdf", "hs": "#e07b10", "mg": "#1a9a5a", "gnd": "#555"}
GROUP_NAMES = {"dio": "Data DIO1-8", "hs": "Handshake DAV, NRFD, NDAC", "mg": "Management EOI, ATN, IFC, SRQ, REN",
               "gnd": "Ground"}


def xy(col, row):
    return OX + col * U + U / 2, OY + row * U + U / 2


def svg():
    cols, rows = 36, 27
    w = OX + cols * U + 360
    h = OY + rows * U + 60
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
         'font-family="ui-sans-serif, system-ui, -apple-system, sans-serif">',
         '<style>.t{font-size:12px;fill:#222}.s{font-size:10px;fill:#333}.h{font-size:18px;font-weight:650;fill:#111}'
         '.n{font-size:9px;fill:#fff;font-weight:600}</style>',
         f'<rect width="{w}" height="{h}" fill="#fff"/>',
         f'<text class="h" x="{OX}" y="30">BusCraft prototype: Pico W straight to the IEEE-488 connector (top view)</text>',
         f'<text class="t" x="{OX}" y="50">No bus driver chips. Wires run on the underside; the drawing shows pad to pad.</text>']
    # board and holes
    o.append(f'<rect x="{OX - 8}" y="{OY - 8}" width="{cols * U + 16}" height="{rows * U + 16}" rx="14" fill="#1b1b1b"/>')
    for c in range(cols):
        for r in range(rows):
            x, y = xy(c, r)
            o.append(f'<circle cx="{x}" cy="{y}" r="5" fill="#b8963e" opacity="0.55"/>')
    # connector body (overhangs the top edge)
    x0, y0 = xy(J1_COL0, J1_ROW_A)
    x1, y1 = xy(J1_COL0 + 11, J1_ROW_B)
    o.append(f'<rect x="{x0 - 26}" y="{OY - 46}" width="{x1 - x0 + 52}" height="{y1 - OY + 58}" rx="6" '
             'fill="#333" opacity="0.55" stroke="#000"/>')
    o.append(f'<text class="t" x="{x1 + 34}" y="{OY - 22}" style="fill:#111">J1 IEEE-488 plug (5552221-1), overhangs the edge</text>')
    # Pico outline
    px0, py0 = xy(PICO_COL0, PICO_ROW_TOP)
    px1, py1 = xy(PICO_COL0 + 19, PICO_ROW_BOT)
    o.append(f'<rect x="{px0 - 18}" y="{py0 - 16}" width="{px1 - px0 + 36}" height="{py1 - py0 + 32}" rx="6" '
             'fill="#1f8a4c" opacity="0.45" stroke="#0d5e31"/>')
    o.append(f'<rect x="{px0 - 46}" y="{(py0 + py1) / 2 - 16}" width="34" height="32" rx="3" fill="#bbb" stroke="#666"/>')
    o.append(f'<text class="s" x="{px0 - 44}" y="{(py0 + py1) / 2 + 4}">USB</text>')
    o.append(f'<text class="t" x="{px0 + 60}" y="{(py0 + py1) / 2 + 4}" style="fill:#fff">'
             'Pico W (KA-0076), USB to the left</text>')
    # GND bridge on J1
    for a, b in zip(GND_BRIDGE, GND_BRIDGE[1:]):
        xa, ya = xy(*j1_pos(a))
        xb, yb = xy(*j1_pos(b))
        o.append(f'<line x1="{xa}" y1="{ya}" x2="{xb}" y2="{yb}" stroke="#bbb" stroke-width="5"/>')
    # wires
    for gpib, name, pico, grp in WIRES:
        xa, ya = xy(*j1_pos(gpib))
        xb, yb = xy(*pico_pos(pico))
        cy = (ya + yb) / 2
        o.append(f'<path d="M{xa},{ya} C{xa},{cy} {xb},{cy} {xb},{yb}" fill="none" stroke="{COLOURS[grp]}" '
                 'stroke-width="2.4" opacity="0.9"/>')
    # pads with numbers
    for pin in range(1, 25):
        x, y = xy(*j1_pos(pin))
        o.append(f'<circle cx="{x}" cy="{y}" r="8.5" fill="#d9a520" stroke="#000"/>')
        o.append(f'<text class="n" x="{x}" y="{y + 3}" text-anchor="middle" style="fill:#000">{pin}</text>')
    for pin in list(range(1, 21)) + list(range(21, 41)):
        x, y = xy(*pico_pos(pin))
        o.append(f'<circle cx="{x}" cy="{y}" r="7" fill="#e8e8e8" stroke="#000"/>')
        o.append(f'<text class="n" x="{x}" y="{y + 3}" text-anchor="middle" style="fill:#000">{pin}</text>')
        if pin <= 20:
            dy = 21 if pin % 2 else 33   # alternate rows so neighbouring names do not overlap
            o.append(f'<text class="s" x="{x}" y="{y + dy}" text-anchor="middle" style="fill:#fff">'
                     f'{PICO_NAMES.get(pin, "")}</text>')
    # table
    tx = OX + cols * U + 30
    ty = OY + 4
    o.append(f'<text class="h" x="{tx}" y="{ty}">Connections</text>')
    ty += 24
    o.append(f'<text class="t" x="{tx}" y="{ty}" font-weight="650">GPIB pin   signal    Pico pin</text>')
    for gpib, name, pico, grp in sorted(WIRES, key=lambda w: w[2]):
        ty += 19
        o.append(f'<rect x="{tx}" y="{ty - 10}" width="10" height="10" fill="{COLOURS[grp]}"/>')
        o.append(f'<text class="t" x="{tx + 18}" y="{ty}">{gpib:>2}</text>')
        o.append(f'<text class="t" x="{tx + 62}" y="{ty}">{name}</text>')
        o.append(f'<text class="t" x="{tx + 130}" y="{ty}">{pico} ({PICO_NAMES[pico]})</text>')
    ty += 26
    o.append(f'<text class="t" x="{tx}" y="{ty}">GPIB 18-24 bridged together (ground).</text>')
    ty += 17
    o.append(f'<text class="t" x="{tx}" y="{ty}">GPIB 12 (shield): leave open or to GND.</text>')
    ty += 30
    for grp, label in GROUP_NAMES.items():
        o.append(f'<rect x="{tx}" y="{ty - 10}" width="22" height="4" fill="{COLOURS[grp]}"/>')
        o.append(f'<text class="s" x="{tx + 30}" y="{ty - 4}">{label}</text>')
        ty += 17
    ty += 16
    o.append(f'<rect x="{tx - 6}" y="{ty - 16}" width="320" height="92" rx="6" fill="#fff4d6" stroke="#d08a00"/>')
    for i, line in enumerate(["CHECK BEFORE WIRING:",
                              "J1 pin numbering is drawn as row A = 1-12,",
                              "row B = 13-24, pins 1 and 13 at the left.",
                              "Confirm pin 1 from the connector body or",
                              "by continuity with a GPIB cable."]):
        bold = ' font-weight="650"' if i == 0 else ''
        o.append(f'<text class="t" x="{tx}" y="{ty + i * 16}"{bold}>{line}</text>')
    o.append('</svg>')
    return "\n".join(o)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "pico-direct-wiring.svg"
    with open(out, "w") as f:
        f.write(svg())
    print(out)
