#!/usr/bin/env python3
# a device icon out of one of Swarms own pictures of it, for the few modules that come without a usable icon
# (no metadata at all, or one with only 48 px bitmaps in its .ico, like the Burst Pro Air and the Kone XP)
# usage: tools/assets/icon-from.py PICTURE NAME [--out assets/roccat]
#   PICTURE  a png out of the sorted Swarm artwork (sort-swarm.py), the device seen from above or at an angle.
#            a+b+c stacks pictures of the same size first (a keyboard: board, keycaps, prints)
#   NAME     the usb product id, the icon lands in devices/NAME.png
# it only trims the empty border and fits the rest into 256x256 like the real icons, nothing gets drawn.
# needs python3-pil. safe to run again
# the artwork belongs to ROCCAT / Turtle Beach, see assets/roccat/ATTRIBUTION.md before you add more of it
import os, sys
from PIL import Image

if len(sys.argv) < 3:
    sys.exit('usage: icon-from.py PICTURE NAME [--out DIR]')
picture, name = sys.argv[1], sys.argv[2]
out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else 'assets/roccat'
SIZE = 256

parts = [Image.open(p).convert('RGBA') for p in picture.split('+')]
im = parts[0]
for part in parts[1:]:
    im.alpha_composite(part.resize(im.size, Image.LANCZOS))
# anything thats barely there (soft shadows at the edge) doesnt count for the trim, or a mouse ends up tiny in its icon
box = im.getchannel('A').point(lambda a: 255 if a > 24 else 0).getbbox()
if box:
    im = im.crop(box)
k = (SIZE - 8) / max(im.width, im.height)
im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
icon = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
icon.alpha_composite(im, ((SIZE - im.width) // 2, (SIZE - im.height) // 2))
os.makedirs(os.path.join(out, 'devices'), exist_ok=True)
target = os.path.join(out, 'devices', name + '.png')
icon.save(target, optimize=True)
print('icon     ', target)
