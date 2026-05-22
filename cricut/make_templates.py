import math, os
from pathlib import Path
import qrcode

# Write outputs next to this script so it works on any machine.
out = Path(__file__).resolve().parent
out.mkdir(exist_ok=True)

COPPER = '#B87333'
WHITE = '#FFFFFF'
DARK = '#07101A'
GUIDE = '#888888'
BLUE = '#2f80ed'
GREEN = '#2ee58f'

# Coordinate system: 400 x 400 == 4 x 4 inches.
W = H = 400
CX = CY = 200
node_r = 155
nodes = []
# Start at top and go clockwise: LED1 at 12 o'clock.
for i, deg in enumerate([-90, -45, 0, 45, 90, 135, 180, 225], start=1):
    rad = math.radians(deg)
    x = CX + node_r * math.cos(rad)
    y = CY + node_r * math.sin(rad)
    nodes.append((i, x, y))


def donut_path(cx, cy, ro, ri):
    # two circles as subpaths; evenodd fill creates a ring
    return (
        f"M {cx-ro:.3f},{cy:.3f} "
        f"a {ro:.3f},{ro:.3f} 0 1,0 {2*ro:.3f},0 "
        f"a {ro:.3f},{ro:.3f} 0 1,0 {-2*ro:.3f},0 Z "
        f"M {cx-ri:.3f},{cy:.3f} "
        f"a {ri:.3f},{ri:.3f} 0 1,1 {2*ri:.3f},0 "
        f"a {ri:.3f},{ri:.3f} 0 1,1 {-2*ri:.3f},0 Z"
    )


def line(x1,y1,x2,y2, color=COPPER, width=7, opacity=1.0):
    return f"<line x1='{x1:.2f}' y1='{y1:.2f}' x2='{x2:.2f}' y2='{y2:.2f}' stroke='{color}' stroke-width='{width}' stroke-linecap='round' opacity='{opacity}'/>"

# ---------- Front vinyl cut SVG ----------
traces = []
# outer mesh ring
for idx in range(len(nodes)):
    _, x1,y1 = nodes[idx]
    _, x2,y2 = nodes[(idx+1)%len(nodes)]
    traces.append(line(x1,y1,x2,y2, width=6, opacity=0.95))
# inner links that avoid the text center a little
for a,b in [(1,3),(3,5),(5,7),(7,1),(8,4),(2,6)]:
    _, x1,y1 = nodes[a-1]
    _, x2,y2 = nodes[b-1]
    # shorten the line so it does not cover the text as much
    sx = x1 + (x2-x1)*0.18
    sy = y1 + (y2-y1)*0.18
    ex = x1 + (x2-x1)*0.82
    ey = y1 + (y2-y1)*0.82
    traces.append(line(sx,sy,ex,ey, width=4, opacity=0.55))

rings = []
for i,x,y in nodes:
    rings.append(f"<path d='{donut_path(x,y,17,8)}' fill='{COPPER}' fill-rule='evenodd'/>")

front_svg = f"""<?xml version='1.0' encoding='UTF-8'?>
<svg xmlns='http://www.w3.org/2000/svg' width='4in' height='4in' viewBox='0 0 400 400'>
  <title>I Network With AI badge front vinyl cut art</title>
  <desc>Cut white text and copper/gold network traces/node rings. Size is 4 inches square for a 4 inch acrylic circle.</desc>
  <g id='VINYL_COPPER_TRACES_AND_NODE_RINGS'>
    {''.join(traces)}
    {''.join(rings)}
  </g>
  <g id='VINYL_WHITE_TEXT' fill='{WHITE}' font-family='DejaVu Sans Condensed, Arial, sans-serif' font-weight='700' text-anchor='middle'>
    <text x='200' y='183' font-size='43'>I NETWORK</text>
    <text x='200' y='226' font-size='38'>WITH AI</text>
    <text x='200' y='272' font-size='17' letter-spacing='1.5'>BUILD THE MESH</text>
  </g>
</svg>
"""
(out/'front_vinyl_cut_editable.svg').write_text(front_svg)

# ---------- Placement guide ----------
guide_lines = []
for idx in range(len(nodes)):
    _, x1,y1 = nodes[idx]
    _, x2,y2 = nodes[(idx+1)%len(nodes)]
    guide_lines.append(line(x1,y1,x2,y2, color='#555555', width=2, opacity=0.5))

placement = f"""<?xml version='1.0' encoding='UTF-8'?>
<svg xmlns='http://www.w3.org/2000/svg' width='4in' height='4in' viewBox='0 0 400 400'>
  <title>I Network With AI badge LED placement guide</title>
  <rect width='400' height='400' fill='white'/>
  <circle cx='200' cy='200' r='198' fill='none' stroke='black' stroke-width='2'/>
  <circle cx='200' cy='28' r='10' fill='none' stroke='#cc3333' stroke-width='2'/>
  <text x='200' y='18' font-family='Arial, sans-serif' font-size='10' text-anchor='middle' fill='#cc3333'>lanyard hole optional</text>
  <g id='LED_POSITIONS'>
    {''.join(guide_lines)}
"""
for i,x,y in nodes:
    placement += f"    <circle cx='{x:.2f}' cy='{y:.2f}' r='18' fill='none' stroke='{BLUE}' stroke-width='2'/>\n"
    placement += f"    <line x1='{x-6:.2f}' y1='{y:.2f}' x2='{x+6:.2f}' y2='{y:.2f}' stroke='{BLUE}' stroke-width='1'/>\n"
    placement += f"    <line x1='{x:.2f}' y1='{y-6:.2f}' x2='{x:.2f}' y2='{y+6:.2f}' stroke='{BLUE}' stroke-width='1'/>\n"
    placement += f"    <text x='{x:.2f}' y='{y+34:.2f}' font-family='Arial, sans-serif' font-size='13' text-anchor='middle' fill='{BLUE}'>LED{i}</text>\n"
placement += f"""  </g>
  <text x='200' y='184' font-family='Arial, sans-serif' font-size='40' font-weight='700' text-anchor='middle' fill='#222222'>I NETWORK</text>
  <text x='200' y='226' font-family='Arial, sans-serif' font-size='35' font-weight='700' text-anchor='middle' fill='#222222'>WITH AI</text>
  <text x='200' y='276' font-family='Arial, sans-serif' font-size='14' text-anchor='middle' fill='#555555'>BUILD THE MESH</text>
  <text x='200' y='386' font-family='Arial, sans-serif' font-size='12' text-anchor='middle' fill='#555555'>Print this as a placement guide. Do not cut as front vinyl.</text>
</svg>
"""
(out/'led_placement_guide.svg').write_text(placement)

# ---------- Back label with QR ----------
qr = qrcode.QRCode(border=1, box_size=1)
qr.add_data('http://192.168.4.1')
qr.make(fit=True)
mat = qr.get_matrix()
qr_n = len(mat)
qr_size = 82
qr_x = 214
qr_y = 28
cell = qr_size / qr_n
qr_rects = []
for r,row in enumerate(mat):
    for c,val in enumerate(row):
        if val:
            x = qr_x + c*cell
            y = qr_y + r*cell
            qr_rects.append(f"<rect x='{x:.3f}' y='{y:.3f}' width='{cell:.3f}' height='{cell:.3f}' fill='black'/>")

back_label = f"""<?xml version='1.0' encoding='UTF-8'?>
<svg xmlns='http://www.w3.org/2000/svg' width='3.2in' height='1.4in' viewBox='0 0 320 140'>
  <title>AI Network Badge back instruction label</title>
  <rect x='0.5' y='0.5' width='319' height='139' rx='12' fill='white' stroke='black' stroke-width='1'/>
  <text x='18' y='28' font-family='Arial, sans-serif' font-size='17' font-weight='700' fill='black'>I NETWORK WITH AI</text>
  <text x='18' y='54' font-family='Arial, sans-serif' font-size='13' fill='black'>Join Wi-Fi: AI-BADGE</text>
  <text x='18' y='74' font-family='Arial, sans-serif' font-size='13' fill='black'>No password</text>
  <text x='18' y='94' font-family='Arial, sans-serif' font-size='12' fill='black'>Send a packet.</text>
  <text x='18' y='112' font-family='Arial, sans-serif' font-size='12' fill='black'>Build the mesh.</text>
  <g id='QR_LOCAL_BADGE_PAGE'>
    <rect x='{qr_x-4}' y='{qr_y-4}' width='{qr_size+8}' height='{qr_size+8}' fill='white'/>
    {''.join(qr_rects)}
  </g>
  <text x='{qr_x+qr_size/2:.1f}' y='126' font-family='Arial, sans-serif' font-size='8' text-anchor='middle' fill='black'>http://192.168.4.1</text>
</svg>
"""
(out/'back_label_print_then_cut.svg').write_text(back_label)

# ---------- Preview SVG ----------
preview_svg = f"""<?xml version='1.0' encoding='UTF-8'?>
<svg xmlns='http://www.w3.org/2000/svg' width='4in' height='4in' viewBox='0 0 400 400'>
  <rect width='400' height='400' fill='{DARK}'/>
  <circle cx='200' cy='200' r='198' fill='#101B2A' stroke='#36526D' stroke-width='3'/>
  <circle cx='200' cy='28' r='10' fill='none' stroke='#6D839B' stroke-width='2'/>
  <g opacity='0.75'>
    {''.join(traces)}
  </g>
  <g>
    {''.join(rings)}
  </g>
  <g fill='{WHITE}' font-family='DejaVu Sans Condensed, Arial, sans-serif' font-weight='700' text-anchor='middle'>
    <text x='200' y='183' font-size='43'>I NETWORK</text>
    <text x='200' y='226' font-size='38'>WITH AI</text>
    <text x='200' y='272' font-size='17' letter-spacing='1.5'>BUILD THE MESH</text>
  </g>
</svg>
"""
(out/'preview_source.svg').write_text(preview_svg)

# ---------- README ----------
readme = """# Cricut badge templates

These SVGs are sized for a 4 inch round acrylic disk.

## Files

- `front_vinyl_cut.svg` - front Cricut vinyl art with text, network traces, and LED node rings. Text has been converted to paths.
- `front_vinyl_cut_editable.svg` - same front art, but text remains editable SVG text.
- `led_placement_guide.svg` - print this on paper as a placement guide for the 8 LEDs. Do not use as the vinyl cut file.
- `back_label_print_then_cut.svg` - back instruction label with a local badge QR code for `http://192.168.4.1`.
- `badge_preview.png` - visual preview.

## Cricut notes

1. Upload `front_vinyl_cut.svg` to Design Space.
2. Confirm the imported size is 4.0 in x 4.0 in.
3. Use separate vinyl colors if desired:
   - White for the text.
   - Copper/gold/silver for traces and LED node rings.
4. The front SVG does not include the 4 inch circle outline because you already have acrylic circles.
5. Use `led_placement_guide.svg` as a printed paper guide behind the acrylic while placing LEDs.
6. Use `back_label_print_then_cut.svg` as a print-then-cut sticker, not as cut vinyl. QR codes are usually too detailed for vinyl cutting.

## LED order

The guide places LED1 at 12 o'clock, then numbers clockwise around the circle.

```text
        LED1
   LED8      LED2

LED7  TEXT   LED3

   LED6      LED4
        LED5
```

Wire LED DOUT to the next LED DIN in that order.
"""
(out/'README_cricut.md').write_text(readme)

print('wrote templates to', out)
