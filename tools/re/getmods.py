# downloads every module the update server still hands out, usage: getmods.py OUT.json DESTDIR
# OUT.json is what allmods.py wrote. safe to run again, files that are already there with the right size get skipped
import hashlib, json, os, socket, sys, time, urllib.parse, urllib.request

# urlretrieve has no timeout of its own and the storage host sometimes just stops talking mid file
socket.setdefaulttimeout(60)
# some of the files sit on a cdn whose ipv6 address just never answers (from here at least), ipv4 always does
_getaddrinfo = socket.getaddrinfo
socket.getaddrinfo = lambda *a, **k: [x for x in _getaddrinfo(*a, **k) if x[0] == socket.AF_INET] or _getaddrinfo(*a, **k)

reply = json.load(open(sys.argv[1]))['reply'][1]
dest = sys.argv[2]
os.makedirs(dest, exist_ok=True)

# several pids share one module (the three kova colours for example), so group by download link
mods = {}
for pid, m in (reply.get('hardware') or {}).items():
    e = mods.setdefault(m['file'], {'name': m['name'], 'version': m['version'], 'size': m['size'], 'pids': []})
    e['pids'].append(pid)
for key, m in (reply.get('software') or {}).items():
    mods.setdefault(m['file'], {'name': m['name'], 'version': m['version'], 'size': m['size'], 'software_id': key})
if reply.get('swarm'):
    s = reply['swarm']
    mods.setdefault(s['file'], {'name': 'Swarm', 'version': s['version'], 'size': s['size']})

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()

failed = 0
for link, m in sorted(mods.items(), key=lambda kv: kv[1]['name']):
    try:
        # the link doesnt serve the file, it answers with a signed object storage url that expires after a bit
        signed = urllib.request.urlopen(link, timeout=60).read().decode().strip()
        m['file'] = os.path.basename(urllib.parse.urlparse(signed).path)
        path = os.path.join(dest, m['file'])
        if os.path.exists(path) and os.path.getsize(path) == m['size']:
            print('have  ', m['file'])
        else:
            urllib.request.urlretrieve(signed, path + '.part')
            os.replace(path + '.part', path)
            print('got   ', m['file'], '%.1f MB' % (os.path.getsize(path) / 1e6), flush=True)
            time.sleep(1)  # no need to hammer a server thats on life support
        if os.path.getsize(path) != m['size']:
            print('  size is', os.path.getsize(path), 'but the server promised', m['size'])
        m['sha256'] = sha256(path)
    except Exception as e:
        failed += 1
        m['error'] = str(e)
        print('FAILED', m['name'], e, flush=True)
    m['link'] = link

json.dump(sorted(mods.values(), key=lambda m: m['name']), open(os.path.join(dest, 'index.json'), 'w'), indent=1)
print(len(mods) - failed, 'of', len(mods), 'downloaded into', dest)
sys.exit(1 if failed else 0)
