#!/usr/bin/env python3
# draws a stand in for every picture in the ROCCAT originals (DEV/roccat-assets-original) that assets/free doesnt have yet, same name and same size
# usage: tools/assets/placeholders.py        (add --force to redraw the ones that exist)
# for now they are all empty: transparent, same size, nothing on them. nothing in there came from ROCCAT
# needs python3-pil
import os, sys
from PIL import Image

SRC, DST = 'DEV/roccat-assets-original', 'assets/free'
force = '--force' in sys.argv

made = 0
for root, _, files in os.walk(SRC):
    for f in files:
        if not f.endswith('.png'):
            continue
        src = os.path.join(root, f)
        dst = os.path.join(DST, os.path.relpath(src, SRC))
        if os.path.exists(dst) and not force:
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        size = Image.open(src).size
        # empty, see-through, nothing else for now. until somebody knows how we make our own pictures the gui just shows
        # no artwork with this set (the keys and markers it draws by itself are still there)
        img = Image.new('RGBA', size, (0, 0, 0, 0))
        img.save(dst, optimize=True)
        print('drew', dst)
        made += 1
print(made, 'new placeholder(s)')
