#!/usr/bin/env python3
# the mouse view of the Lua, which has no Swarm module to take one from: ROCCATs top render off the old product page,
# its blue logo cut out as the mask of the one light, and the six buttons placed by hand
# usage: tools/assets/lua-view.py TOP_PNG      (DEV/roccat-shill/Extras/Lua/top.png, the render via the Internet Archive)
# writes assets/roccat/views/2c2e/{top.png, light0.png, view.ini} and the view.ini to assets/free/views/2c2e/. safe to run again
# needs python3-pil. the picture belongs to ROCCAT / Turtle Beach, see assets/roccat/ATTRIBUTION.md
import colorsys, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
HEIGHT, PAD, MARKER = 575, 10, 27

if len(sys.argv) != 2:
    sys.exit('usage: lua-view.py TOP_PNG')
src = Image.open(sys.argv[1]).convert('RGBA')
src = src.crop(src.getbbox())
scale = (HEIGHT - 2 * PAD) / src.height
small = src.resize((round(src.width * scale), HEIGHT - 2 * PAD), Image.LANCZOS)
canvas = Image.new('RGBA', (small.width + 2 * PAD, HEIGHT), (0, 0, 0, 0))
canvas.alpha_composite(small, (PAD, PAD))

# the logo is the only blue on the mouse, and only in the lower third. a pixel counts by how blue and how bright it is
mask = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
px, mp = canvas.load(), mask.load()
for y in range(canvas.height * 2 // 3, canvas.height):
    for x in range(canvas.width):
        r, g, b, a = px[x, y]
        if not a:
            continue
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if 0.47 < h < 0.64 and s > 0.35 and v > 0.3:
            mp[x, y] = (255, 255, 255, round(255 * min(1, v * 1.4) * a / 255))

out = os.path.join(ROOT, 'assets', 'roccat', 'views', '2c2e')
os.makedirs(out, exist_ok=True)
canvas.save(os.path.join(out, 'top.png'))
mask.save(os.path.join(out, 'light0.png'))

# button marker middles, as a share of the cropped render (0..1), in the order lua.cpp numbers them. wheel up and down sit
# above and below the wheel, the middle click on it
spots = [(0.27, 0.21), (0.73, 0.21), (0.50, 0.255), (0.50, 0.185), (0.50, 0.33), (0.50, 0.465)]
lines = ['# written by tools/assets/lua-view.py, dont edit by hand', '[view]', 'picture=top.png',
         f'width={canvas.width}', f'height={canvas.height}', f'marker={MARKER}', '',
         '# button (our number, from 0) = x;y of the middle of its marker on the picture;the number on the marker', '[buttons]']
for i, (fx, fy) in enumerate(spots):
    lines.append(f'{i}={PAD + fx * small.width:.1f};{PAD + fy * small.height:.1f};{i + 1}')
lines += ['', '# led (our number, from 0) = its mask, same size as the picture', '[lights]', '0=light0.png', '',
          '# the logo is painted into the picture, it gets painted dark when the light is off', '[painted]', '0=1', '']
ini = '\n'.join(lines)
open(os.path.join(out, 'view.ini'), 'w').write(ini)
free = os.path.join(ROOT, 'assets', 'free', 'views', '2c2e')
os.makedirs(free, exist_ok=True)
open(os.path.join(free, 'view.ini'), 'w').write(ini)
print(f'wrote {out} ({canvas.width}x{canvas.height}), logo mask {sum(1 for y in range(mask.height) for x in range(mask.width) if mp[x, y][3])} px')
