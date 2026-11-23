#!/usr/bin/env python3
# turns the original ROCCAT pngs into the webp files the app gets built with, the pngs made the binary thick af (~48 MB of it)
# usage: tools/assets/to-webp.py [-q QUALITY | --lossless] [--from DIR] [--to DIR] [-j JOBS]
#   defaults: q90 from DEV/roccat-assets-original into assets/roccat. run it from the repo root
#   -q 0-100 lossy quality, lower = smaller and uglier. 80 was fine on the keyboards but some mice went crusty, 90 holds up everywhere
#   --lossless  pixel perfect, about 60 % of the png size
# the see-through part always stays lossless whatever -q says, otherwise the glows and soft edges get blocky.
# wipes the target folder first so nothing stale hangs around, everything that isnt a png (view.ini, kb_matrix.xml,
# ATTRIBUTION.md) gets copied over as it is. needs python3-pil with webp support (debian: python3-pil, arch: python-pillow)
import os, sys, time, shutil, argparse
from multiprocessing import Pool
from PIL import Image, features


def convert(job):
    src, dst, quality = job
    im = Image.open(src)
    im.load()
    if im.mode not in ('RGBA', 'RGB'):
        im = im.convert('RGBA')
    if quality is None:
        im.save(dst, 'WEBP', lossless=True, method=6, quality=100)
    else:
        im.save(dst, 'WEBP', quality=quality, method=6, alpha_quality=100)
    return src, os.path.getsize(src), os.path.getsize(dst)


def clock(s):
    s = int(s)
    return '%d:%02d' % (s // 60, s % 60)


def main():
    ap = argparse.ArgumentParser(description='convert the ROCCAT pngs to webp for the build')
    ap.add_argument('-q', '--quality', type=int, default=90, help="lossy quality 0-100 (default 90)")
    ap.add_argument('--lossless', action='store_true', help='lossless webp instead, ignores -q')
    ap.add_argument('--from', dest='src', default='DEV/roccat-assets-original')
    ap.add_argument('--to', dest='dst', default='assets/roccat')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count())
    a = ap.parse_args()
    if not features.check('webp'):
        sys.exit('this pillow cant write webp, install one that can (python3-pil on debian, python-pillow on arch)')
    if not 0 <= a.quality <= 100:
        sys.exit('-q goes from 0 to 100, %d is not a thing' % a.quality)
    if not os.path.isdir(a.src):
        sys.exit('no %s here. run me from the repo root, or tell me where the originals are with --from' % a.src)
    if os.path.abspath(a.src) == os.path.abspath(a.dst):
        sys.exit('--from and --to are the same folder, that would eat the originals')
    quality = None if a.lossless else a.quality

    jobs, copies = [], []
    for root, _, files in os.walk(a.src):
        for f in files:
            src = os.path.join(root, f)
            rel = os.path.relpath(src, a.src)
            if f.endswith('.png'):
                jobs.append((src, os.path.join(a.dst, rel[:-4] + '.webp'), quality))
            else:
                copies.append((src, os.path.join(a.dst, rel)))

    shutil.rmtree(a.dst, ignore_errors=True)
    for _, dst, *_ in jobs + copies:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
    for src, dst in copies:
        shutil.copyfile(src, dst)

    # big ones first, so the end isnt one keyboard body chewing alone on one core
    jobs.sort(key=lambda j: -os.path.getsize(j[0]))
    total = sum(os.path.getsize(j[0]) for j in jobs)
    print('%d pngs (%.1f MB) to %s, %s, %d jobs' % (len(jobs), total / 1e6, a.dst,
                                                     'lossless' if quality is None else 'q%d' % quality, a.jobs), flush=True)
    start, done, before, after = time.time(), 0, 0, 0
    tty = sys.stderr.isatty()
    with Pool(a.jobs) as pool:
        for i, (src, n_png, n_webp) in enumerate(pool.imap_unordered(convert, jobs), 1):
            before += n_png
            after += n_webp
            took = time.time() - start
            # time left by bytes, a 900 kb body takes way longer than a 4 kb light mask
            left = took / before * (total - before) if before else 0
            line = '[%3d%%] %d/%d  %s gone, ~%s left  %s' % (100 * i // len(jobs), i, len(jobs), clock(took), clock(left),
                                                         os.path.relpath(src, a.src))
            if tty:
                sys.stderr.write('\r\033[K' + line)
            elif i % 20 == 0 or i == len(jobs):
                sys.stderr.write(line + '\n')
            sys.stderr.flush()
    if tty:
        sys.stderr.write('\n')
    print('%.1f MB of png became %.1f MB of webp (%d %%), %d other files copied, took %s'
          % (before / 1e6, after / 1e6, round(100 * after / before), len(copies), clock(time.time() - start)))


if __name__ == '__main__':
    main()
