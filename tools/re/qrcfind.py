# finds the Qt resources (pictures, mostly) baked into a Swarm exe or module dll and writes them out under their real names
# usage: qrcfind.py FILE OUTDIR
# unlike qrc.py this needs no addresses out of a disassembler, it goes looking by itself:
#   1. names in a Qt resource carry a hash of themselves, so a name table can be recognised for certain
#   2. a resource tree starts with a root folder node that looks the same everywhere
#   3. with the tree and the names known, the data block is wherever the .png entries land on png files
# a file can hold several resource collections, all of them get dumped. prints what it found
import os, re, struct, sys, zlib
from collections import Counter

blob = open(sys.argv[1], 'rb').read()
out = sys.argv[2]

def be16(o): return struct.unpack_from('>H', blob, o)[0]
def be32(o): return struct.unpack_from('>I', blob, o)[0]

def qt_hash(s):
    h = 0
    for ch in s:
        h = ((h << 4) + ord(ch)) & 0xffffffff
        g = h & 0xf0000000
        h ^= g >> 23
        h &= ~g & 0xffffffff
    return h

def name_at(o):
    # [16 bit length][32 bit hash][utf-16-be text]. None if thats not what is sitting at o
    if o < 0 or o + 6 > len(blob):
        return None
    n = be16(o)
    if not 0 < n <= 200 or o + 6 + 2 * n > len(blob):
        return None
    try:
        s = blob[o + 6:o + 6 + 2 * n].decode('utf-16-be')
    except UnicodeDecodeError:
        return None
    return s if qt_hash(s) == be32(o + 2) else None

# every name entry that ends in a picture extension, found from the back: extension first, then try lengths until the hash fits
entries = set()
for m in re.finditer(b'\x00\\.\x00(?:p\x00n\x00g|j\x00p\x00g|s\x00v\x00g|i\x00c\x00o|b\x00m\x00p)', blob):
    end = m.end()
    for n in range(5, 120):
        o = end - 2 * n - 6
        if o >= 0 and be16(o) == n and name_at(o):
            entries.add(o)
            break
pngs = set(m.start() for m in re.finditer(b'\x89PNG\r\n\x1a\n', blob))

def read_tree(t, node):
    # returns [(name offset, flags, a, b)] for a tree at t with the given node size, or None if it falls apart
    nodes, want = [], 1
    i = 0
    while i < want:
        o = t + i * node
        if o + node > len(blob):
            return None
        flags = be16(o + 4)
        if flags & ~3:
            return None
        if flags & 2:
            count, first = be32(o + 6), be32(o + 10)
            if count > 20000 or first > 200000 or first <= i:
                return None
            want = max(want, first + count)
        nodes.append((be32(o), flags, be32(o + 6), be32(o + 10)))
        i += 1
        if want > 200000:
            return None
    return nodes

found = 0
seen_trees = set()
for m in re.finditer(b'\x00\x00\x00\x00\x00\x02\x00\x00..\x00\x00\x00\x01', blob, re.S):
    for node in (14, 22):
        t = m.start()
        nodes = read_tree(t, node)
        if not nodes or len(nodes) < 2 or (t, node) in seen_trees:
            continue
        files = [n for n in nodes[1:] if not n[1] & 2]
        if not files:
            continue
        # names: the base that puts the most file nodes onto name entries we know are real
        votes = Counter(e - f[0] for f in files[:400] for e in entries if 0 <= e - f[0])
        if not votes:
            continue
        names, hits = votes.most_common(1)[0]
        if hits < max(1, len([f for f in files[:400]]) // 4) or any(name_at(names + n[0]) is None for n in nodes[1:]):
            continue
        # data: same game, .png nodes have to land 4 bytes in front of a png
        png_nodes = [f for f in files if name_at(names + f[0]).lower().endswith('.png') and not f[1] & 1]
        votes = Counter(p - 4 - f[3] for f in png_nodes[:60] for p in pngs if p - 4 - f[3] >= 0)
        if not votes:
            continue
        data, hits = votes.most_common(1)[0]
        if hits < max(1, len(png_nodes[:60]) // 2):
            continue
        seen_trees.add((t, node))

        def walk(i, path):
            global found
            off, flags, a, b = nodes[i]
            p = os.path.join(path, name_at(names + off)) if i else path
            if flags & 2:
                for c in range(b, b + a):
                    walk(c, p)
                return
            o = data + b
            payload = blob[o + 4:o + 4 + be32(o)]
            if flags & 1:
                try:
                    payload = zlib.decompress(payload[4:])
                except zlib.error:
                    return
            os.makedirs(os.path.dirname(p), exist_ok=True)
            # two collections with the same path in one file do happen, keep both
            while os.path.exists(p):
                root, ext = os.path.splitext(p)
                p = root + '_' + ext
            open(p, 'wb').write(payload)
            found += 1
        walk(0, out)
        break
print('%-28s %4d files in %d collection(s)' % (os.path.basename(sys.argv[1]), found, len(seen_trees)))
