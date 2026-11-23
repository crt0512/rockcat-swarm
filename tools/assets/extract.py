#!/usr/bin/env python3
# pulls the artwork we use out of a Swarm device module: the device icon, and for keyboards the layout pictures + key matrix
# usage: tools/assets/extract.py MODULE_DIR NAME [--out DEV/roccat-assets-original]
#   MODULE_DIR  a module folder as Swarm has them on disk (the one with device_meta_data/ and maybe graphics/ in it)
#   NAME        what to call the device in our tree, its usb product id (2cee), keyboards too (2fda)
# needs cabextract in PATH. safe to run again, it just overwrites what it wrote last time
# the artwork belongs to ROCCAT / Turtle Beach, see assets/roccat/ATTRIBUTION.md before you add more of it
# the pngs land in DEV/roccat-assets-original (the originals, outside of git), tools/assets/to-webp.py makes the webp the build uses out of them
import glob, os, shutil, struct, subprocess, sys, tempfile

if len(sys.argv) < 3:
    sys.exit(__doc__ or 'usage: extract.py MODULE_DIR NAME [--out DIR]')
module, name = sys.argv[1], sys.argv[2]
out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else 'DEV/roccat-assets-original'

def biggest_png(ico_path):
    # an .ico is a little directory of pictures. the 256 px one is a complete png file sitting in there,
    # which is lucky because icotool chokes on these particular files and spits out confetti
    data = open(ico_path, 'rb').read()
    count = struct.unpack_from('<H', data, 4)[0]
    best = None
    for i in range(count):
        size, offset = struct.unpack_from('<II', data, 6 + 16 * i + 8)
        blob = data[offset:offset + size]
        if blob[:8] == b'\x89PNG\r\n\x1a\n' and (best is None or len(blob) > len(best)):
            best = blob
    return best

def icon(cab):
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['cabextract', '-q', '-d', tmp, cab], check=True)
        # its device.ico nearly everywhere, the Kone XP calls it device_xp.ico because why not
        icos = sorted(glob.glob(os.path.join(tmp, 'DeviceInformation', '*.ico')), key=lambda p: os.path.basename(p) != 'device.ico')
        png = biggest_png(icos[0]) if icos else None
        if not png:
            sys.exit('the icon has no png in it (only tiny bitmaps), make one with tools/assets/icon-from.py')
        os.makedirs(os.path.join(out, 'devices'), exist_ok=True)
        target = os.path.join(out, 'devices', name + '.png')
        open(target, 'wb').write(png)
        print('icon     ', target)

cabs = glob.glob(os.path.join(module, 'device_meta_data', '*.devicemetadata-ms'))
if cabs:
    icon(cabs[0])
else:
    # a few modules come without (Burst Core, Kone Air, Vulcan II Mini Air ...), no icon then, the keyboard pictures still work
    print('no device_meta_data/*.devicemetadata-ms in %s, so no icon' % module)

layout = os.path.join(module, 'graphics', 'keyboard_layout')
# the Suoras wrap everything in one more folder (normal/, next to an illumination/ we dont need)
if os.path.isdir(os.path.join(layout, 'normal', 'base')):
    layout = os.path.join(layout, 'normal')

def find(folder, name):
    # Swarm cant decide between _without_keys and _without_Keys, so we dont care about case
    if not os.path.isdir(folder):
        return None
    hits = [f for f in os.listdir(folder) if f.lower() == name.lower()]
    return os.path.join(folder, hits[0]) if hits else None

if os.path.isdir(os.path.join(layout, 'base')):
    kb = os.path.join(out, 'keyboards', name)
    os.makedirs(kb, exist_ok=True)
    base = os.path.join(layout, 'base')
    regions = [r for r in sorted(os.listdir(layout)) if r != 'base' and find(os.path.join(layout, r, '1920x1080'), 'Keycaps_text_small.png')]
    # the gui knows two picture sets: eu (every iso board, DE and friends borrow it) and us. a board that only exists
    # in japan (Vulcan TKL Pro JP) puts its JP set where eu would go, the gui uses that one for every non US region anyway
    eu = 'EU' if 'EU' in regions else next((r for r in regions if r != 'US'), None)
    sets = {'eu': eu, 'us': 'US' if 'US' in regions else None}
    # one size is enough, the gui scales. the 1920x1080 set is the sharpest one Swarm shipped
    # the board: either one for all (base/1920x1080_without_keys.png) or one per set (base/EU/..., base/US/...)
    body = find(base, '1920x1080_without_keys.png') or find(os.path.join(base, eu or ''), '1920x1080_without_keys.png')
    wanted = {}
    if find(base, 'kb_matrix.xml'):
        wanted[find(base, 'kb_matrix.xml')] = 'kb_matrix.xml'
    if body:
        wanted[body] = 'body.png'
    us_body = find(os.path.join(base, 'US'), '1920x1080_without_keys.png')
    # some us boards have their own body (the hole for the enter key sits somewhere else), the gui only loads body.png for now
    if body and us_body and open(us_body, 'rb').read() != open(body, 'rb').read():
        wanted[us_body] = 'body-us.png'
    for tag, region in sets.items():
        if not region:
            continue
        hi = os.path.join(layout, region, '1920x1080')
        if find(hi, 'Keycaps_small.png'):
            wanted[find(hi, 'Keycaps_small.png')] = 'keycaps-%s.png' % tag
        wanted[find(hi, 'Keycaps_text_small.png')] = 'legends-%s.png' % tag
    for src, dst in wanted.items():
        shutil.copyfile(src, os.path.join(kb, dst))
        print('keyboard ', os.path.join(kb, dst))
