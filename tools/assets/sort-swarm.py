#!/usr/bin/env python3
# takes the big pile tools/assets/dump-swarm.sh made and sorts out, per device, the pictures a settings gui actually wants:
# the device from its different angles, the lighting layers (one mask per led zone), the button view, and for keyboards the key layout
# usage: tools/assets/sort-swarm.py OUT_DIR ARTWORK_DIR:MODULES_DIR [ARTWORK_DIR:MODULES_DIR ...] [--names INDEX.json]
#   ARTWORK_DIR  one folder per module, as dump-swarm.sh / qrcfind.py wrote them
#   MODULES_DIR  the module folders those came from. either one folder per module, or the unpacked downloads (ARCHIVE/data/MODULE)
#   --names      index.json from tools/re/getmods.py, gives devices their real name ("Kone Pro" and not "2C88")
# when the same module is in several sources the first one on the command line wins, so put the one you trust first
# everything gets COPIED, the sources are left alone. running it again rebuilds OUT_DIR from scratch
# its all ROCCATs artwork. fine for looking at and for working out layouts, not something to publish
import hashlib, html, json, os, re, shutil, struct, sys

args = [a for a in sys.argv[1:] if not a.startswith('--')]
names_file = sys.argv[sys.argv.index('--names') + 1] if '--names' in sys.argv else None
if names_file in args:
    args.remove(names_file)
if len(args) < 2:
    sys.exit('usage: sort-swarm.py OUT_DIR ARTWORK_DIR:MODULES_DIR [...] [--names INDEX.json]')
out, sources = args[0], [a.split(':', 1) for a in args[1:]]

# ui furniture every module carries around, none of it shows a device
CHROME = {'check_box', 'radio_btn', 'dropdowns', 'page_tab', 'control_box', 'color-dropdown', 'color_dropdown', 'dpi-active_box', 'functions',
          'icons', 'icon', 'progress', 'profile', 'trash_can', 'push_btn', 'info', 'switch', 'colorbox', 'palette', 'calibration',
          'mouse-rotation', 'button_preset-custom-mode', 'left', 'right', 'tab', 'slider', 'scrollbar', 'battery', 'settings', 'config',
          'help', 'deactivate', 'delete_icon', 'button-set-presets', 'add-button', 'down', 'over', 'blance', 'single_blancs', 'quad_blancs'}
LIGHT = re.compile(r'led|light|mask|illum|glow|aimo|rgb|zone|logo|branding', re.I)
# loading spinners and the like that are big enough to slip through
NOT_A_DEVICE = re.compile(r'icon|analysis|loading|spinner|checkmark|background|_bg[_.]|_\d{5}\.png$|pop-up|boarder', re.I)
BUTTON = re.compile(r'btn|button|top[_-]?view|sidegrip', re.I)

# what a person corrected by eye, see the file
overrides = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overrides.json')))
fixes = overrides.get('devices', {})

def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    return struct.unpack('>II', head[16:24]) if head[:8] == b'\x89PNG\r\n\x1a\n' else (0, 0)

def digest(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()

def string_reader(data):
    # returns a function that says which text an address in a 32 bit dll points at, or None.
    # Qt keeps its texts as a little header (refcount -1, length, 0, offset 16) followed by utf-16, plain C strings are taken too
    try:
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        count, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
        if data[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', data, pe + 24)[0] != 0x10b:
            return lambda va: None
        base = struct.unpack_from('<I', data, pe + 52)[0]
        sections = [struct.unpack_from('<IIII', data, pe + 24 + opt + 40 * i + 8) for i in range(count)]
    except struct.error:
        return lambda va: None
    def text_at(va):
        for vsize, start, rsize, raw in sections:
            if start <= va - base < start + rsize:
                o = raw + va - base - start
                if o + 16 <= len(data):
                    ref, n, _, off = struct.unpack_from('<iiIi', data, o)
                    if ref == -1 and 0 < n < 80 and off == 16:
                        try:
                            return data[o + 16:o + 16 + 2 * n].decode('utf-16-le')
                        except UnicodeDecodeError:
                            return None
                end = data.find(b'\0', o, o + 60)
                if 2 <= end - o and all(32 <= c < 127 for c in data[o:end]):
                    return data[o:end].decode()
        return None
    return text_at

def button_rects(dll, size):
    # Swarm has no file that says where the button markers go, the numbers are baked into the module as code:
    # "put these four values next to each other on the stack" (a QRect: left, top, right, bottom) and then hand them to setGeometry.
    # so we look for exactly that instruction pattern and keep every rect that has the size of the marker picture.
    # returns [(x, y, number, widget)]. number is what Swarm prints on the marker, None when that could not be found
    data = open(dll, 'rb').read()
    ev = []
    for m in re.finditer(rb'\xc7\x45(.)(....)|\xc7\x44\x24(.)(....)|\xc7\x85(....)(....)|\xc7\x84\x24(....)(....)', data, re.S):
        if m.group(1):
            ev.append((m.start(), 'ebp', struct.unpack('b', m.group(1))[0], struct.unpack('<i', m.group(2))[0]))
        elif m.group(3):
            ev.append((m.start(), 'esp', m.group(3)[0], struct.unpack('<i', m.group(4))[0]))
        elif m.group(5):
            ev.append((m.start(), 'ebp', struct.unpack('<i', m.group(5))[0], struct.unpack('<i', m.group(6))[0]))
        else:
            # the same thing with a 32 bit offset, what the compiler does once a function has a lot of locals. the newer modules are full of it
            ev.append((m.start(), 'esp', struct.unpack('<i', m.group(7))[0], struct.unpack('<i', m.group(8))[0]))
    found = []
    for i in range(len(ev) - 3):
        four = ev[i:i + 4]
        if len(set(e[1] for e in four)) != 1 or four[3][0] - four[0][0] > 80:
            continue
        by = {e[2]: e[3] for e in four}
        d = sorted(by)
        if len(d) != 4 or any(d[k + 1] - d[k] != 4 for k in range(3)):
            continue
        x1, y1, x2, y2 = (by[k] for k in d)
        if (x2 - x1 + 1, y2 - y1 + 1) == size and (x1 or y1) and 0 <= x1 < 3000 and 0 <= y1 < 3000 and (x1, y1) not in [f[1:] for f in found]:
            found.append((four[0][0], x1, y1))
    # the ui code was generated (Qt uic), so every marker is made the same way: name the widget ("wg_btn_7"), place it,
    # then make the label on it ("lb_btn_7") and give that its text ("7"). the name sits right in front of the rect and the text
    # right behind it. the text is what counts: on the Pure SEL the widget names and the numbers shown do not agree
    text_at = string_reader(data)
    out = []
    for n, (at, x, y) in enumerate(found):
        lo = max(at - 260, found[n - 1][0] + 40 if n else 0)
        hi = min(at + 420, found[n + 1][0] - 40 if n + 1 < len(found) else len(data) - 4)
        before = [t for t in (text_at(struct.unpack_from('<I', data, k)[0]) for k in range(lo, at)) if t]
        after = [t for t in (text_at(struct.unpack_from('<I', data, k)[0]) for k in range(at, hi)) if t]
        widget = before[-1] if before and re.fullmatch(r'\w+', before[-1]) else None
        shown = next((int(t) for t in after if t.isdigit()), None)
        out.append((x, y, shown, widget))
    numbers = [o[2] for o in out]
    if None in numbers or len(set(numbers)) != len(numbers):
        return [(x, y, None, widget) for x, y, _, widget in out]
    return sorted(out, key=lambda o: o[2])

PAGE_WIDTH = 541

def write_positions(dest, dll):
    marker = os.path.join(dest, 'buttons', 'Button_Highlight_normal.png')
    if not os.path.exists(marker):
        return 0
    size = png_size(marker)
    spots = button_rects(dll, size)
    if not spots:
        return 0
    # the picture the markers sit on: the biggest one in buttons/. Swarm centres it (background-position: center) on a page
    # that works out to 541 px wide on the two devices this was checked on by eye (Kova, Kone XP), top edge at 0.
    # thats an educated GUESS for everything else, look at button-positions-preview.png before trusting it
    pics = sorted(((png_size(os.path.join(dest, 'buttons', f)), f) for f in os.listdir(os.path.join(dest, 'buttons'))
                   if f.endswith('.png') and not f.startswith('Button_Highlight')), reverse=True)
    image, (iw, ih) = (pics[0][1], pics[0][0]) if pics else (None, (0, 0))
    origin = [round((PAGE_WIDTH - iw) / 2), 0] if image else [0, 0]
    numbered = all(s[2] is not None for s in spots)
    json.dump({'marker_size': list(size), 'image': image, 'image_origin_guess': origin,
               'order': 'n is the number Swarm prints on the marker, read out of the module next to the position' if numbered else
                        'the order the markers appear in the module code. NOT button numbers, those could not be found in this module',
               'positions': [{'n': shown or n + 1, 'widget': widget, 'x': x, 'y': y, 'x_on_image': x - origin[0], 'y_on_image': y - origin[1]}
                             for n, (x, y, shown, widget) in enumerate(spots)]},
              open(os.path.join(dest, 'button-positions.json'), 'w'), indent=1)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return len(spots)
    w = max(max(s[0] for s in spots) + size[0] + 20, origin[0] + iw + 20)
    h = max(max(s[1] for s in spots) + size[1] + 20, ih + 20)
    canvas = Image.new('RGBA', (int(w), int(h)), (23, 24, 28, 255))
    if image:
        pic = Image.open(os.path.join(dest, 'buttons', image)).convert('RGBA')
        canvas.alpha_composite(pic, (max(0, origin[0]), 0))
    draw = ImageDraw.Draw(canvas)
    for n, (x, y, shown, _) in enumerate(spots):
        draw.ellipse([x, y, x + size[0], y + size[1]], outline=(101, 195, 247, 255), width=2)
        draw.text((x + size[0] / 2 - 3 * len(str(shown or n + 1)), y + size[1] / 2 - 5), str(shown or n + 1), fill=(255, 255, 255, 255))
    canvas.save(os.path.join(dest, 'button-positions-preview.png'))
    return len(spots)

def module_dlls(folder):
    # the dlls right in the module folder (the alien_fx ones sit a level down). the newer modules also carry a USBCmdLib.dll
    # that is bigger than the module itself and has nothing to do with the ui, so that one goes to the back of the queue
    dlls = [os.path.join(folder, f) for f in os.listdir(folder) if f.lower().endswith('.dll')] if folder else []
    return sorted(dlls, key=lambda p: ('usbcmdlib' in p.lower(), -os.path.getsize(p)))

def classify(rel):
    # returns the subfolder a picture belongs in, or None for furniture
    parts = [p.lower() for p in rel.split(os.sep)]
    name = parts[-1]
    if name == 'device-icon.png':
        return ''
    folder = '/'.join(parts[:-1])
    if 'button_highlight' in folder:
        return 'buttons'
    # the Nyth has 12 swappable side buttons, Swarm draws them from one picture per slot. kept together, they are not views
    # (single buttons, double buttons and the blank caps for slots nobody wants a button in)
    if re.search(r'(single|double)-buttons/(normal|blance)|quad_blancs', folder):
        return 'buttons/side-grid'
    if any(p in CHROME for p in parts[:-1]) or not name.endswith('.png'):
        return None
    w, h = png_size(rel_to_abs[rel])
    if min(w, h) < 100 or NOT_A_DEVICE.search(name):
        return None
    if 'keyillumination' in folder or 'key_illumination' in folder:
        return 'lighting/effect-previews'
    if 'illumination' in folder or LIGHT.search(name):
        return 'lighting'
    if 'button_assignment' in folder or BUTTON.search(name):
        return 'buttons'
    return 'views'

# real names for modules that are only known by an alias or a product id
friendly = {}
if names_file:
    for m in json.load(open(names_file)):
        if m.get('file'):
            friendly[m['file'][:-3] if m['file'].endswith('.7z') else m['file']] = m['name']

# one entry per module alias: where its artwork is, where its module folder is, which device it belongs to
modules = {}
for art, mods in sources:
    for alias in sorted(os.listdir(art)):
        if not os.path.isdir(os.path.join(art, alias)) or alias in modules:
            continue
        # plain layout MODULES/ALIAS, or the unpacked downloads MODULES/ARCHIVE/data/ALIAS
        folder, device = os.path.join(mods, alias), None
        if not os.path.isdir(folder):
            for archive in os.listdir(mods):
                cand = os.path.join(mods, archive, 'data', alias)
                if os.path.isdir(cand):
                    folder, device = cand, friendly.get(archive)
                    break
        modules[alias] = {'art': os.path.join(art, alias), 'folder': folder if os.path.isdir(folder) else None, 'device': device}
# a module that came from the trusted source still gets its real name from wherever the same alias was downloaded
for alias, m in modules.items():
    if m['device']:
        continue
    for art, mods in sources:
        for archive in os.listdir(mods):
            if os.path.isdir(os.path.join(mods, archive, 'data', alias)) and archive in friendly:
                m['device'] = friendly[archive]
    m['device'] = m['device'] or alias.replace('_', ' ').title()

devices = {}
for alias, m in modules.items():
    devices.setdefault(m['device'], []).append(alias)

# rebuild the device folders from scratch, but leave loose files in OUT alone: thats where review notes get saved
os.makedirs(out, exist_ok=True)
for entry in os.listdir(out):
    if os.path.isdir(os.path.join(out, entry)):
        shutil.rmtree(os.path.join(out, entry))
summary = []
for device in sorted(devices):
    dest = os.path.join(out, re.sub(r'[^A-Za-z0-9+._-]+', '_', device).strip('_'))
    seen, counts, notes = {}, {}, []
    for alias in sorted(devices[device]):
        m = modules[alias]
        rel_to_abs = {}
        for dirpath, _, files in os.walk(m['art']):
            for f in files:
                p = os.path.join(dirpath, f)
                rel_to_abs[os.path.relpath(p, m['art'])] = p
        for rel in sorted(rel_to_abs):
            sub = classify(rel)
            if sub is None:
                continue
            # a module sometimes carries pictures of another model. the review sorted those out, they are kept but out of the way
            if os.path.basename(rel) in fixes.get(os.path.basename(dest), {}).get('exclude', []):
                sub = 'unused'
            src = rel_to_abs[rel]
            name = 'icon.png' if os.path.basename(rel) == 'device-icon.png' else os.path.basename(rel)
            target = os.path.join(dest, sub, name)
            h = digest(src)
            # the colour variants of a device ship the same pictures over and over, one copy is plenty
            if seen.get(target) == h:
                continue
            if target in seen:
                root, ext = os.path.splitext(name)
                target = os.path.join(dest, sub, '%s.%s%s' % (root, alias, ext))
                if target in seen:
                    continue
            seen[target] = h
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(src, target)
            counts[sub or 'icon'] = counts.get(sub or 'icon', 0) + 1
        # keyboards keep their layout as loose files next to the dll: pictures per region plus the key matrix
        layout = os.path.join(m['folder'], 'graphics', 'keyboard_layout') if m['folder'] else ''
        if os.path.isdir(layout) and not os.path.isdir(os.path.join(dest, 'keyboard')):
            shutil.copytree(layout, os.path.join(dest, 'keyboard'))
            counts['keyboard'] = sum(len(f) for _, _, f in os.walk(os.path.join(dest, 'keyboard')))
            notes.append('`keyboard/base/kb_matrix.xml` is the file that says where every key is: name, led index, x/y/w/h for two picture sizes. One `<REGION>` per layout.')
    if not counts:
        continue
    spots = 0
    for alias in sorted(devices[device]):
        for dll in module_dlls(modules[alias]['folder']):
            spots = spots or write_positions(dest, dll)
    if spots:
        counts['button positions'] = spots
        notes.append('Button positions: Swarm has no file for them, the numbers are compiled into the module dll. `button-positions.json` has the %d that could be dug out of the code, in the coordinates of the page they sit on, plus a guess where the picture sits on that page. `button-positions-preview.png` draws them onto the picture so you can see if the guess holds.' % spots)
    elif 'buttons' in counts and 'keyboard' not in counts:
        notes.append('Button positions: could not be dug out of this module automatically.')
    masks = sorted(f for f in os.listdir(os.path.join(dest, 'lighting')) if f.endswith('.png')) if os.path.isdir(os.path.join(dest, 'lighting')) else []
    if masks:
        notes.append('Lighting layers are all the same size as the base picture and meant to be stacked on it, one per led zone. Tint a layer with the zone colour and you have the preview.')
    with open(os.path.join(dest, 'README.md'), 'w') as f:
        f.write('# %s\n\nSwarm modules: %s\n\n' % (device, ', '.join('`%s`' % a for a in sorted(devices[device]))))
        f.write(''.join('- `%s/`: %d files%s\n' % (k, v, ' (in the module, but pictures of another model)' if k == 'unused' else '') for k, v in sorted(counts.items()) if k != 'icon'))
        f.write('\n' + '\n\n'.join(notes) + '\n' if notes else '')
        f.write('\nAll of this is ROCCAT / Turtle Beach artwork, copied out of Swarm unchanged.\n')
    summary.append((device, os.path.basename(dest), counts, sorted(devices[device])))

# ---- the page: every device put back together from its pieces -------------------------------------------------------
try:
    from PIL import Image
except ImportError:
    Image = None

ZONE = re.compile(r'led|mask|light|glow|branding', re.I)
# a logo or shading layer is not a zone and not a base either, it gets drawn on top the way it is
OVERLAY = re.compile(r'overlay|shadow|(?<!with)_logo\.png$', re.I)

def layer_kind(path):
    # a zone is either a shape on transparent ("alpha") or grey on solid black ("luma"),
    # the page has to treat those two differently and only looking at the pixels tells them apart
    if OVERLAY.search(os.path.basename(path)):
        return 'overlay'
    if Image:
        lo, hi = Image.open(path).convert('RGBA').getchannel('A').getextrema()
        if lo == 255:
            return 'luma'
    return 'alpha'

def tokens(text):
    # "KoneAimo-16K_BLK.png" -> kone, aimo, 16k, blk. file names glue words together, device names have dots in them
    text = re.sub(r'\.png$', '', os.path.basename(text))
    text = re.sub(r'([a-z])([A-Z])', r'\1 \2', text)
    return set(t for t in re.split(r'[^a-z0-9]+', text.lower()) if t)

def lighting_groups(dest, device, aliases):
    # layers only make sense on top of a picture of the same size, so group by size and look for a base in each group:
    # something in lighting/ that isnt a zone or an overlay, else a view, else the button page picture
    folder = os.path.join(dest, 'lighting')
    if not os.path.isdir(folder):
        return []
    by_size = {}
    for f in sorted(os.listdir(folder)):
        if f.endswith('.png'):
            by_size.setdefault(png_size(os.path.join(folder, f)), []).append(f)
    groups = []
    for (w, h), files in sorted(by_size.items(), key=lambda kv: -len(kv[1])):
        zones = [f for f in files if (ZONE.search(f) or OVERLAY.search(f)) and 'with_logo' not in f.lower()]
        # some devices ship a zone twice, as a glow picture and as a NAME_MASK. one of them is enough, the glow looks nicer.
        # the other one still goes onto the page, switched off, so it can be swapped in by hand if the guess was wrong
        spare = [f for f in zones if f.lower().endswith('_mask.png') and f[:-9] + '.png' in files]
        bases = [os.path.join('lighting', f) for f in files if f not in zones]
        for sub in ('views', 'buttons'):
            d = os.path.join(dest, sub)
            if os.path.isdir(d):
                bases += [os.path.join(sub, f) for f in sorted(os.listdir(d)) if f.endswith('.png') and png_size(os.path.join(d, f)) == (w, h)]
        if not zones or not bases:
            continue
        # one module often carries the pictures of several devices (Elo Air / Elo USB / Khan, Syn Max / Syn Pro).
        # the words that tell the base pictures apart say which device a file is for: pick the base that shares most of
        # them with this devices name, and switch off every layer that carries a word of one of the others
        words = [tokens(b) for b in bases]
        common = set.intersection(*words) if words else set()
        telling = [wd - common for wd in words]
        name = tokens(device).union(*[tokens(a) for a in aliases])
        # the words only ONE base has ("air", "usb", "khan") are what marks a layer as belonging to that base
        # ("with" and "logo" dont count, CM_300_with_logo.png is the same mouse as CM_300.png and not another device)
        own = [wd - set().union(*(o for j, o in enumerate(words) if j != i)) - {'with', 'logo', 'base', 'graphic'} for i, wd in enumerate(words)]
        def others(f, i):
            t = tokens(f)
            return bool(t & set().union(*(o for j, o in enumerate(own) if j != i))) and not (t & own[i])
        def lit(i):
            # does this variant have a zone picture of its own, or one that belongs to nobody in particular
            return any(not others(f, i) for f in zones if not OVERLAY.search(f))
        def rank(i):
            b = os.path.basename(bases[i]).lower()
            # a variant with nothing to light up loses (the Elo USB has no lighting pictures, the review used the Elo Air ones for it),
            # then the name match, then pictures from lighting/ before borrowed ones, "base" in the name, plain names before "(2)" / "with_logo"
            return (not lit(i), -len(telling[i] & name), 'with' in b or '(' in b, not bases[i].startswith('lighting'), 'base' not in b, len(b), b)
        order = sorted(range(len(bases)), key=rank)
        first = order[0]
        layers = []
        for f in zones:
            auto = layer_kind(os.path.join(folder, f))
            other = others(f, first)
            layers.append({'src': os.path.join('lighting', f), 'auto': auto, 'kind': 'off' if f in spare or other else auto})
        bases = [bases[i] for i in order]
        # pictures that were taken for a base can be told to be a zone on the page too, same reason
        for b in bases[1:]:
            if b.startswith('lighting' + os.sep):
                layers.append({'src': b, 'auto': layer_kind(os.path.join(dest, b)), 'kind': 'off'})
        if any(l['kind'] in ('alpha', 'luma') for l in layers):
            groups.append({'w': w, 'h': h, 'base': bases[0], 'bases': bases, 'layers': layers})
    return groups

def keyboard_scene(dest):
    # Swarm lays every keyboard out the same way: base/kb_matrix.xml with the keys, base/[SET/]1920x1080_without_keys.png for the board,
    # and per picture set (EU, US, sometimes CH, FR, UK ...) the keycaps and the print on them. a few boards wrap all that in one more folder
    matrix = None
    for dirpath, _, files in os.walk(os.path.join(dest, 'keyboard')):
        if 'kb_matrix.xml' in files and os.path.basename(dirpath) == 'base':
            matrix = os.path.join(dirpath, 'kb_matrix.xml')
            break
    if not matrix:
        return None
    base = os.path.dirname(matrix)
    kb = os.path.dirname(base)
    rel = lambda p: os.path.relpath(p, out) if p else None
    def pick(folder, pattern):
        if not os.path.isdir(folder):
            return None
        hits = [os.path.join(folder, f) for f in os.listdir(folder) if re.search(pattern, f, re.I)]
        return max(hits, key=lambda p: png_size(p)[0], default=None)
    sets, small = {}, None
    for sub in sorted(os.listdir(kb)):
        hi = os.path.join(kb, sub, '1920x1080')
        if sub == 'base' or not os.path.isdir(hi):
            continue
        body = pick(os.path.join(base, sub), r'1920x1080_without_keys') or pick(base, r'1920x1080_without_keys')
        sets[sub] = {'body': rel(body), 'caps': rel(pick(hi, r'^keycaps_small')), 'legends': rel(pick(hi, r'^keycaps_text_small'))}
        # x / y / w / h (and the polygon points) in the xml are pixels on the 1024x768 pictures. the t_x / t_y next to them
        # look like a bigger version of the same but are only the spot for the print, every key is 61x61 there
        low = pick(os.path.join(kb, sub, '1024x768'), r'^keycaps_small') or pick(os.path.join(base, sub), r'1024x768_without_keys') or pick(base, r'1024x768_without_keys')
        small = small or (png_size(low) if low else None)
    bodies = [v['body'] for v in sets.values() if v['body']]
    if not sets or not bodies or not small:
        return None
    # only EU and US come with a picture of the board itself. CH, FR, UK and friends are ISO boards like EU and just bring other keycaps
    for v in sets.values():
        v['body'] = v['body'] or (sets.get('EU') or {}).get('body') or bodies[0]
    w, h = png_size(os.path.join(out, bodies[0]))
    xml = re.sub(r'<!--\s*<POLYGON.*?-->', '', open(matrix, encoding='utf-8-sig').read(), flags=re.S)
    regions = {}
    for m in re.finditer(r'<REGION value="(\d+)"[^>]*>\s*(?:<!--(\w+)-->)?(.*?)</REGION>', xml, re.S):
        keys = []
        for k in re.finditer(r'<KEY name="([^"]*)" index="(\d+)"[^>]*?\sx="(-?\d+)" y="(-?\d+)" w="(\d+)" h="(\d+)"[^>]*?(?:/>|>(.*?)</KEY>)', m.group(3), re.S):
            key = [html.unescape(k.group(1)), int(k.group(2)), int(k.group(3)), int(k.group(4)), int(k.group(5)), int(k.group(6))]
            # keys that are not a rectangle (ISO enter, the thumb keys) come with their outline as a list of corners
            poly = re.search(r'<POLYGON([^>]*)/?>', k.group(7) or '')
            if poly:
                pts = dict((p, int(v)) for p, v in re.findall(r'\b([xy]\d+)="(-?\d+)"', poly.group(1)))
                n = len(pts) // 2
                if n >= 3 and all('x%d' % i in pts and 'y%d' % i in pts for i in range(1, n + 1)):
                    key.append([c for i in range(1, n + 1) for c in (pts['x%d' % i], pts['y%d' % i])])
            keys.append(key)
        if not keys:
            continue
        # most regions carry their name as a comment. the few that dont are numbered the same way on every board that was checked
        label = (m.group(2) or {'0': 'US', '1': 'DE', '2': 'UK', '3': 'FR', '4': 'RU'}.get(m.group(1), 'region' + m.group(1))).upper()
        # a layout with its own pictures uses them (CH, FR, UK ...). DE is what Swarm calls EU, and everything else borrows EU
        which = label if label in sets else 'EU' if 'EU' in sets else next(iter(sets))
        regions[label] = {'set': which, 'keys': keys}
    if not regions:
        return None
    return {'w': w, 'h': h, 'sx': w / small[0], 'sy': h / small[1], 'sets': sets, 'regions': regions}

def guess_origin(w, h):
    # Swarm centres the picture on a 541 px wide page, but the page itself sits a bit up and to the left of where the markers
    # are counted from. the averages below are what a human dragged the pictures to in the first review round
    if (w, h) == (340, 518):
        return [78, -13]
    if (w, h) == (457, 518):
        return [20, -21]
    return [round((PAGE_WIDTH - w) / 2) - 12, -6]

def button_scene(folder, entry):
    dest, fix = os.path.join(out, folder), fixes.get(folder, {})
    source = fix.get('buttons_from', folder)
    spots = os.path.join(out, source, 'button-positions.json')
    if not os.path.exists(spots):
        return None
    b = json.load(open(spots))
    n = len(b['positions'])
    # the corrections file talks about buttons by the number on the marker, the page wants places in the list
    numbers = [q['n'] for q in b['positions']]
    # two numbers the review said sit on each others button. the module says otherwise, the person looking at it wins
    for one, other in fix.get('swap_numbers', []):
        if one in numbers and other in numbers:
            i, j = numbers.index(one), numbers.index(other)
            numbers[i], numbers[j] = other, one
    index = lambda numbers_wanted: [numbers.index(m) for m in numbers_wanted if m in numbers]
    def hides(image):
        # pictures of the same device with parts taken off (the Kiro without its side buttons): which markers have nothing under them there
        known = fix.get('picture_hides', {})
        return dict((a, index(known[os.path.basename(a)])) for a in same_size(image) if os.path.basename(a) in known)
    def pieces(spec):
        # buttons that are separate pictures to be laid over the view, all the size of the area they go into (the Nyth side grid).
        # one list per set, each piece with the box its button fills, so the page can tell which one the mouse is on
        sets = {}
        folder_abs = os.path.join(dest, spec['folder'])
        for label, prefix in spec['sets'].items():
            one = []
            for f in sorted(os.listdir(folder_abs)) if os.path.isdir(folder_abs) else []:
                if f.startswith(prefix) and f.endswith('.png'):
                    box = Image.open(os.path.join(folder_abs, f)).convert('RGBA').getchannel('A').point(lambda a: 255 if a > 40 else 0).getbbox() if Image else None
                    number = re.search(r'(\d+)\.png$', f)
                    one.append({'src': os.path.join(folder, spec['folder'], f), 'box': list(box) if box else None, 'n': int(number.group(1)) if number else len(one) + 1})
            if one:
                sets[label] = one
        return {'at': spec['at'], 'sets': sets} if sets else None
    def find(name):
        # a picture named in the corrections file: anywhere in this devices folder
        for sub in ('buttons', 'views', 'lighting'):
            if os.path.exists(os.path.join(dest, sub, name)):
                return os.path.join(folder, sub, name)
        return None
    def same_size(image):
        # other pictures that could stand in for this one (the Kiro and its side grips, colour variants)
        owner = image.split(os.sep)[0]
        size, alts = png_size(os.path.join(out, image)), []
        for sub in ('buttons', 'views', 'lighting'):
            d = os.path.join(out, owner, sub)
            if os.path.isdir(d):
                alts += [os.path.join(owner, sub, f) for f in sorted(os.listdir(d)) if f.endswith('.png') and not f.startswith('Button_Highlight')
                         and not ZONE.search(f) and not OVERLAY.search(f) and png_size(os.path.join(d, f)) == size]
        return [image] + [a for a in alts if a != image]
    views = []
    if fix.get('views'):
        claimed = set(m for v in fix['views'] for m in v.get('markers', []))
        for v in fix['views']:
            image = find(v['image'])
            if not image:
                continue
            w, h = png_size(os.path.join(out, image))
            views.append({'image': image, 'w': w, 'h': h, 'origin': v.get('origin') or guess_origin(w, h), 'images': same_size(image), 'hides': hides(image),
                          'trust': 'placed by hand in the review' if v.get('origin') else 'picture position is a guess',
                          'markers': index(v['markers']) if 'markers' in v else [i for i in range(n) if numbers[i] not in claimed]})
            if v.get('pieces'):
                views[-1]['pieces'] = pieces(v['pieces'])
    else:
        image, origin = None, fix.get('picture_origin')
        if fix.get('button_image') == 'lighting-base':
            image = entry['lighting'][0]['base'] if entry['lighting'] else None
        elif fix.get('button_image'):
            image = fix['button_image']
        elif source != folder:
            # borrowed positions: our own picture if we have one of the same size as theirs, else theirs
            theirs = os.path.join(source, 'buttons', b['image']) if b.get('image') else None
            size = png_size(os.path.join(out, theirs)) if theirs else None
            mine = [os.path.join(folder, sub, f) for sub in ('buttons', 'views', 'lighting') if os.path.isdir(os.path.join(dest, sub))
                    for f in sorted(os.listdir(os.path.join(dest, sub))) if f.endswith('.png') and not f.startswith('Button_Highlight')
                    and not ZONE.search(f) and not OVERLAY.search(f) and (not size or png_size(os.path.join(dest, sub, f)) == size)]
            image = mine[0] if mine else theirs
            origin = origin or fixes.get(source, {}).get('picture_origin')
        elif b.get('image') and os.path.exists(os.path.join(dest, 'buttons', b['image'])):
            image = os.path.join(folder, 'buttons', b['image'])
        if not image:
            # some modules have no picture of their own for the button page and reuse the one from the lighting page
            image = entry['lighting'][0]['base'] if entry['lighting'] else entry['views'][0] if entry['views'] else None
        if not image:
            return None
        w, h = png_size(os.path.join(out, image))
        views.append({'image': image, 'w': w, 'h': h, 'origin': origin or guess_origin(w, h), 'images': same_size(image), 'hides': hides(image), 'markers': list(range(n))})
    if not views:
        return None
    placed = fix.get('picture_origin') or any(v.get('origin') for v in fix.get('views', [])) or (source != folder and fixes.get(source, {}).get('picture_origin'))
    # what the buttons are called, by the number on the marker. only known where somebody sat in front of the device
    names = fix.get('button_names') or fixes.get(fix.get('names_from', source), {}).get('button_names') or {}
    return {'marker': b['marker_size'], 'positions': [[q['x'], q['y']] for q in b['positions']], 'numbers': numbers,
            'numbered': 'NOT' not in b.get('order', ''), 'names': names, 'views': views,
            'trust': fix.get('unverified') or ('placed by hand in the review' if placed else 'picture position is a guess')}

manifest = []
for device, folder, counts, aliases in summary:
    if folder in overrides.get('skip', []):
        continue
    dest = os.path.join(out, folder)
    rel = lambda *p: os.path.join(folder, *p)
    entry = {'name': device, 'folder': folder, 'aliases': aliases, 'icon': rel('icon.png') if os.path.exists(os.path.join(dest, 'icon.png')) else None,
             'lighting': [dict(g, base=rel(g['base']), bases=[rel(b) for b in g['bases']], layers=[dict(l, src=rel(l['src'])) for l in g['layers']])
                          for g in lighting_groups(dest, device, aliases)],
             'buttons': None, 'keyboard': keyboard_scene(dest), 'note': fixes.get(folder, {}).get('note'),
             'looks': dict((rel(sub, f), look) for f, look in fixes.get(folder, {}).get('view_looks', {}).items()
                           for sub in ('views', 'lighting') if os.path.exists(os.path.join(dest, sub, f))),
             'views': ([rel('views', f) for f in sorted(os.listdir(os.path.join(dest, 'views'))) if f.endswith('.png')][:12] if os.path.isdir(os.path.join(dest, 'views')) else [])
                      + [rel(v) for v in fixes.get(folder, {}).get('extra_views', []) if os.path.exists(os.path.join(dest, v))]}
    manifest.append(entry)
# second pass, because a device may borrow its button page from one further down the list
for entry in manifest:
    entry['buttons'] = button_scene(entry['folder'], entry)
# the same list as a file of its own, for whoever wants to build something else from the sorted folder
json.dump(manifest, open(os.path.join(out, 'manifest.json'), 'w'))
template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'viewer.html')).read()
open(os.path.join(out, 'index.html'), 'w').write(template.replace('/*MANIFEST*/[]', json.dumps(manifest)))
open(os.path.join(out, 'README.md'), 'w').write("""# ROCCAT device artwork, sorted

One folder per device, made by `tools/assets/sort-swarm.py` (rerunning it rebuilds this whole folder, so dont edit in here). Open `index.html`: it puts every device back together from these files, lights its led zones up in a rainbow, shows the button markers (hover them, or switch them off at the top) and lets you pick the keyboard layout for all keyboards at once. On mice you can drag the picture until it sits right under the markers, the button at the top then shows the numbers to report back.

In each device folder:

- `icon.png`: the 256 px device icon
- `views/`: the device from its different angles
- `buttons/`: the picture Swarm uses on its button assignment page, plus the highlight marker it puts on each button
- `button-positions.json` + `button-positions-preview.png`: where those markers go and which number Swarm prints on each. Dug out of the module code, see the README in the device folder for how much to trust it
- `unused/`: pictures the module carries that show another model
- `lighting/`: the base picture and one layer per led zone, all the same size, made to be stacked. `lighting/effect-previews/` on keyboards is the little thumbnails of the lighting presets
- `keyboard/`: keyboards only. The board without keys, the keycaps and legends per region, and `base/kb_matrix.xml`, which lists every key with its position and led index
- `README.md`: which Swarm modules the device came from and what was found

Whats NOT in here: the thousands of buttons, checkboxes and sliders of the Swarm ui itself. Those are still in `../Artwork/`.

All of it is ROCCAT / Turtle Beach artwork.
""")
for device, folder, counts, aliases in summary:
    print('%-28s %s' % (folder, ' '.join('%s=%d' % kv for kv in sorted(counts.items()))))
print('%d devices in %s, open %s' % (len(summary), out, os.path.join(out, 'index.html')))
