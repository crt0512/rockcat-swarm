#!/usr/bin/env python3
# the side of a mouse whose side buttons are caps you put in yourself (the Nyth): Swarms picture of the side without any
# caps, and one picture per cap and place, all the same size and drawn at the same spot on the side (pieces_at). the gui
# stacks the ones that are in. also writes the [side] part of view.ini: where the pieces go and the box of every slot
# usage: tools/assets/side-view.py SORTED_ASSETS [PID...] [--out DIR]   (no PID = every recipe in views.json with a "side")
#   SORTED_ASSETS  the folder tools/assets/sort-swarm.py wrote (one folder per device). a copy of the Nyth folder from
#                  roccat.512mb.net has the same layout and works too
# writes DIR/views/PID/side*.png (DIR defaults to DEV/roccat-assets-original) and puts the [side] sections into the
# view.ini there and in assets/free/views/PID/. tools/assets/mouse-view.py runs this by itself for recipes with a "side"
# the file names (Swarm -> ours):
#   buttons/Nyth_standard.png                -> side.png          the side, every slot open
#   side-grid/Single-Button-NN.png           -> side-button-NN.png  a button on slot NN
#   side-grid/Single-blanc_NN.png            -> side-blank-NN.png   a blank cap on slot NN
#   side-grid/Double-button_0N.png           -> side-double-N.png   a double over slots 2N-1 and 2N (only those pairs)
#   side-grid/Quad-blanc_0N.png              -> side-quad-N.png     a blank over row N
# needs python3-pil. safe to run again. the artwork belongs to ROCCAT / Turtle Beach, see assets/roccat/ATTRIBUTION.md
import json, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')

args = [a for a in sys.argv[1:]]
out_root = os.path.join(ROOT, 'DEV', 'roccat-assets-original')
if '--out' in args:
    i = args.index('--out')
    out_root = args[i + 1]
    del args[i:i + 2]
if not args:
    sys.exit('usage: side-view.py SORTED_ASSETS [PID...] [--out DIR]')
assets, pids = args[0], args[1:]
recipes = json.load(open(os.path.join(HERE, 'views.json')))


def recipe(pid):
    r = dict(recipes[pid])
    if 'like' in r:
        base = recipe(r['like'])
        side = dict(base.get('side', {}), **r.get('side', {}))
        r = dict(base, **{k: v for k, v in r.items() if k != 'like'})
        if side:
            r['side'] = side
    return r


def put_sections(ini_path, lines):
    # the [side] and [side-slots] sections get replaced, everything else in the file stays as it is
    old = open(ini_path).read().split('\n') if os.path.exists(ini_path) else []
    keep, comments, skipping = [], [], False
    for line in old:
        if line.startswith('#') or not line.strip():
            comments.append(line)  # belongs to whatever comes next
            continue
        if line.startswith('['):
            skipping = line.strip() in ('[side]', '[side-slots]')
        if not skipping:
            keep += comments + [line]
        comments = []
    text = '\n'.join(keep).rstrip('\n')
    open(ini_path, 'w').write(text + '\n\n' + '\n'.join(lines) + '\n')


def build(pid):
    r = recipe(pid)
    sd = r['side']
    dev = os.path.join(assets, r['device'])
    grid = os.path.join(dev, sd.get('pieces', 'buttons/side-grid'))
    out = os.path.join(out_root, 'views', pid)
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        if old.startswith('side'):
            os.remove(os.path.join(out, old))
    names = [(sd['picture'], 'side.png')]
    names += [('Single-Button-%02d.png' % n, 'side-button-%02d.png' % n) for n in range(1, 13)]
    names += [('Single-blanc_%02d.png' % n, 'side-blank-%02d.png' % n) for n in range(1, 13)]
    names += [('Double-button_%02d.png' % n, 'side-double-%d.png' % n) for n in range(1, 7)]
    names += [('Quad-blanc_%02d.png' % n, 'side-quad-%d.png' % n) for n in range(1, 4)]
    for src, dst in names:
        path = os.path.join(dev, src) if src == sd['picture'] else os.path.join(grid, src)
        Image.open(path).convert('RGBA').save(os.path.join(out, dst), optimize=True)
    side = Image.open(os.path.join(out, 'side.png'))
    lines = ['# the side with its caps (side_panel.cpp). the pieces are drawn at x;y on the side picture, all of them the',
             '# same size, so a slot box below is in the coordinates of a piece picture',
             '[side]', 'picture=side.png', 'width=%d' % side.width, 'height=%d' % side.height,
             'pieces=%d;%d' % tuple(sd['pieces_at']), '',
             '# slot (the number Swarm prints on it, row by row from the top front) = left;top;right;bottom of a single cap there',
             '[side-slots]']
    lines += ['%d=%d;%d;%d;%d' % (n + 1, *box) for n, box in enumerate(sd['slots'])]
    put_sections(os.path.join(out, 'view.ini'), lines)
    free = os.path.join(ROOT, 'assets', 'free', 'views', pid)
    if os.path.isdir(free):
        put_sections(os.path.join(free, 'view.ini'), lines)
    print('%s: side %dx%d, %d pieces' % (pid, side.width, side.height, len(names) - 1))


for pid in pids or [p for p in recipes if not p.startswith('_') and 'side' in recipe(p)]:
    if pid not in recipes or 'side' not in recipe(pid):
        sys.exit('no side in the recipe for %s (views.json)' % pid)
    build(pid)
