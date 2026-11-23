#!/usr/bin/env python3
# builds the picture the gui shows of a mouse: the top view out of Swarm on a canvas big enough for everything around it,
# one mask per light (same size as the canvas, so the gui only has to tint and stack them) and a view.ini that says
# where the marker of every button sits
# usage: tools/assets/mouse-view.py SORTED_ASSETS [PID...]      (no PID = every recipe in tools/assets/views.json)
#   SORTED_ASSETS  the folder tools/assets/sort-swarm.py wrote (it has manifest.json and one folder per device)
# writes DEV/roccat-assets-original/views/PID/{top.png, lightN.png, view.ini} and copies view.ini to assets/free/views/PID/
# (the numbers in it are ours to give away, the pictures are not: run tools/assets/placeholders.py for stand ins)
# needs python3-pil. safe to run again, it overwrites what it wrote last time
# the artwork belongs to ROCCAT / Turtle Beach, see assets/roccat/ATTRIBUTION.md before you add more of it
# the pngs land in DEV/roccat-assets-original (the originals, outside of git), tools/assets/to-webp.py makes the webp the build uses out of them
import json, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
PAD = 10

if len(sys.argv) < 2:
    sys.exit('usage: mouse-view.py SORTED_ASSETS [PID...]')
assets = sys.argv[1]
recipes = json.load(open(os.path.join(HERE, 'views.json')))
manifest = json.load(open(os.path.join(assets, 'manifest.json')))
devices = {d['folder']: d for d in (manifest if isinstance(manifest, list) else manifest['devices'])}

def recipe(pid):
    r = dict(recipes[pid])
    if 'like' in r:
        r = dict(recipe(r['like']), **{k: v for k, v in r.items() if k != 'like'})
    return r

def hue_mask(im, hue, box=None):
    # everything in the picture with a colour between the two angles becomes a mask: alpha says where the light is,
    # the grey in it how bright it was painted there, so a dim rim stays a dim rim in any colour
    h, s, v = im.convert('RGB').convert('HSV').split()
    a = im.getchannel('A')
    ramp = lambda x, lo, wide: max(0.0, min(1.0, (x - lo) / wide))
    inside = lambda deg: hue[0] <= deg <= hue[1] if hue[0] <= hue[1] else deg >= hue[0] or deg <= hue[1]
    alpha = Image.new('L', im.size)
    alpha.putdata([int(255 * ramp(ss, 45, 50) * ramp(vv, 12, 40)) if aa > 40 and inside(hh * 360 / 255) else 0
                   for hh, ss, vv, aa in zip(h.getdata(), s.getdata(), v.getdata(), a.getdata())])
    if box:
        # two lights in the same colour (or a print that happens to be in it too): only take the one in the box
        keep = Image.new('L', im.size, 0)
        keep.paste(255, tuple(box))
        alpha = Image.composite(alpha, keep, keep)
    grey = v.point(lambda x: min(255, int(x * 1.3)))
    return Image.merge('RGBA', (grey, grey, grey, alpha))

def load(rel, scale=1, rotate=0):
    im = Image.open(os.path.join(assets, rel)).convert('RGBA')
    if rotate:
        im = im.rotate(-rotate, expand=True, resample=Image.BICUBIC)
    if scale != 1:
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    return im

def layer_mask(light):
    # Swarm has two kinds of light layer: alpha = the light is a shape on see-through (most of them),
    # luma = grey on black, meant to be added onto the picture. both end up white with the light in the alpha
    layer = load(light['from'], light.get('scale', 1), light.get('rotate', 0))
    a = layer.convert('L') if light.get('kind') == 'luma' else layer.getchannel('A')
    white = Image.new('L', layer.size, 255)
    return Image.merge('RGBA', (white, white, white, a))

def view_marks(b, view, nudge, at=(0, 0), scale=1):
    ox, oy = view['origin']
    mw, mh = b['marker']
    marks = {}
    for i in view.get('markers', range(len(b['positions']))):
        (x, y), n = b['positions'][i], b['numbers'][i]
        marks[n] = ((x - ox + mw / 2) * scale + at[0] + nudge[0], (y - oy + mh / 2) * scale + at[1] + nudge[1])
    return marks

def build(pid):
    r = recipe(pid)
    dev = devices[r['device']]
    b = dev.get('buttons')
    # a picture path starting with a device folder is taken from there (Swarm shows the Kone Pure SE as an Owl-Eye), else its in the device folder
    rel = r['picture'] if r['picture'].split('/')[0] in devices else os.path.join(r['device'], r['picture'])
    picture = load(rel, r.get('scale', 1), r.get('rotate', 0))
    mw, mh = b['marker'] if b else (27, 27)
    # where the picture got dragged to in the review is good to a pixel, nudge is for the rest
    nudge = r.get('nudge', [0, 0])
    # everything in the coordinates of the picture first: markers (Swarm gives them where its window has them), light layers
    marks = {}
    if b and r.get('inputs'):
        marks.update(view_marks(b, b['views'][r.get('view', 0)], nudge))
    # more pictures next to the main one: a second view with its own markers (the side of the Leadr), or the side of a
    # mouse whose lights are only drawn from there (the back of the Nyth)
    extras = []
    for e in r.get('extras', []):
        im = load(e['picture'], e.get('scale', 1), e.get('rotate', 0))
        extras.append((im, tuple(e['at'])))
        if 'view' in e and b:
            for n, xy in view_marks(b, b['views'][e['view']], nudge, e['at'], e.get('scale', 1)).items():
                marks.setdefault(n, xy)
    layers, painted = [], []
    for light in r.get('lights', []):
        if 'hue' in light:
            layers.append((light['led'], hue_mask(picture, light['hue'], light.get('box')), (0, 0)))
            # the same light painted on the extra pictures too (the Leadr shows its logo and wheel from the side as well)
            if light.get('extras'):
                for im, at in extras:
                    layers.append((light['led'], hue_mask(im, light['hue']), at))
            painted.append(light['led'])
        else:
            layers.append((light['led'], layer_mask(light), tuple(light.get('at', [0, 0]))))
            # a layer over a part of the picture that is already lit up in ROCCATs colour: it has to cover it when off too
            if light.get('painted'):
                painted.append(light['led'])
    # a layer is mostly empty, only what is in it counts for the size of the canvas (the Pure SEL grew 40 px of nothing otherwise)
    trimmed = []
    for led, layer, at in layers:
        box = layer.getchannel('A').getbbox() or (0, 0, 1, 1)
        trimmed.append((led, layer.crop(box), (at[0] + box[0], at[1] + box[1])))
    layers = trimmed
    wanted = [n for n in r.get('inputs', []) if n]
    missing = [n for n in wanted if n not in marks]
    if missing:
        sys.exit('%s: Swarm has no marker for %s' % (pid, missing))
    used = [marks[n] for n in wanted]
    # the canvas: picture, extras, every layer and every marker, and a little air
    boxes = [(0, 0, picture.width, picture.height)] + [(at[0], at[1], at[0] + im.width, at[1] + im.height) for im, at in extras] + \
            [(at[0], at[1], at[0] + l.width, at[1] + l.height) for _, l, at in layers] + \
            [(x - mw / 2, y - mh / 2, x + mw / 2, y + mh / 2) for x, y in used]
    x0 = int(min(bx[0] for bx in boxes) - PAD)
    y0 = int(min(bx[1] for bx in boxes) - PAD)
    x1 = int(max(bx[2] for bx in boxes) + PAD + .5)
    y1 = int(max(bx[3] for bx in boxes) + PAD + .5)
    size = (x1 - x0, y1 - y0)
    out = os.path.join(ROOT, 'DEV', 'roccat-assets-original', 'views', pid)
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        os.remove(os.path.join(out, old))
    top = Image.new('RGBA', size, (0, 0, 0, 0))
    top.alpha_composite(picture, (-x0, -y0))
    for im, at in extras:
        top.alpha_composite(im, (at[0] - x0, at[1] - y0))
    top.save(os.path.join(out, 'top.png'), optimize=True)
    ini = ['# written by tools/assets/mouse-view.py, dont edit by hand. recipe: tools/assets/views.json',
           '[view]', 'picture=top.png', 'width=%d' % size[0], 'height=%d' % size[1], 'marker=%d' % mw, '',
           '# button (our number, from 0) = x;y of the middle of its marker on the picture;the number Swarm prints on it',
           '[buttons]']
    for i, n in enumerate(r.get('inputs', [])):
        if n:
            x, y = marks[n]
            ini.append('%d=%.1f;%.1f;%d' % (i, x - x0, y - y0, n))
    ini += ['', '# led (our number, from 0) = its mask, same size as the picture', '[lights]']
    masks = {}
    for led, layer, at in layers:
        full = masks.setdefault(led, Image.new('RGBA', size, (0, 0, 0, 0)))
        full.alpha_composite(layer, (at[0] - x0, at[1] - y0))
    for led, full in masks.items():
        full.save(os.path.join(out, 'light%d.png' % led), optimize=True)
        ini.append('%d=light%d.png' % (led, led))
    ini += ['', '# leds whose light is painted into the picture itself. those get painted over dark when they are off,', '# a glow next to the device just isnt drawn then', '[painted]']
    ini += ['%d=1' % led for led in sorted(set(painted))]
    text = '\n'.join(ini) + '\n'
    open(os.path.join(out, 'view.ini'), 'w').write(text)
    free = os.path.join(ROOT, 'assets', 'free', 'views', pid)
    os.makedirs(free, exist_ok=True)
    open(os.path.join(free, 'view.ini'), 'w').write(text)
    print('%s: %s, %dx%d, %d buttons, %d lights' % (pid, r['device'], size[0], size[1], len(wanted), len(masks)))

for pid in sys.argv[2:] or [p for p in recipes if not p.startswith('_')]:
    if pid not in recipes:
        sys.exit('no recipe for %s in views.json' % pid)
    build(pid)
