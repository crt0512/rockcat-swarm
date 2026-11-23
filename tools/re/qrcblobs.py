#!/usr/bin/env python3
# pulls the compressed Qt resources out of a Swarm module or the monitor exe without knowing where their tree is.
# qrcfind.py only finds the collections with pictures in them, the config ones (default.dat & co) hold none.
# a compressed resource sits as [u32 length][u32 unpacked size][zlib ...], big endian, so we look for zlib headers
# with a sane length in front, unpack them, and keep what looks like text or a QSettings file
# usage: python3 -I tools/re/qrcblobs.py FILE OUTDIR      (reads only, writes OUTDIR/blob_<offset>.<ini|txt|bin>)
import os, struct, sys, zlib

if len(sys.argv) != 3:
    sys.exit('usage: qrcblobs.py FILE OUTDIR')
data = open(sys.argv[1], 'rb').read()
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
found = 0
for i in range(8, len(data) - 2):
    if data[i] != 0x78 or data[i + 1] not in (0x01, 0x5e, 0x9c, 0xda):
        continue
    length, size = struct.unpack_from('>II', data, i - 8)
    if not (6 < length < 1 << 24) or not (0 < size < 1 << 26) or i - 4 + length > len(data):
        continue
    try:
        blob = zlib.decompress(data[i:i - 4 + length])
    except zlib.error:
        continue
    if len(blob) != size:
        continue
    text = blob[:4096]
    kind = 'ini' if b'@ByteArray' in blob or text.lstrip().startswith(b'[') else 'txt' if all(32 <= c < 127 or c in (9, 10, 13) for c in text) else 'bin'
    open(os.path.join(out, 'blob_%08x.%s' % (i, kind)), 'wb').write(blob)
    found += 1
print('%d blobs' % found)
