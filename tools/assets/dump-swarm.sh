#!/bin/sh
# gets every picture out of a pile of Swarm modules: the Qt resources inside each module dll (thats where the device renders are)
# and the device icon out of its metadata cabinet. one output folder per module
# usage: tools/assets/dump-swarm.sh MODULES_DIR OUT_DIR
#   MODULES_DIR  a folder with one subfolder per module, the way Swarm keeps them (.../ROCCAT/Swarm/data/ or an unpacked download)
# needs python3 and cabextract. only reads MODULES_DIR. running it again overwrites what it wrote before
# what comes out is ROCCATs artwork, fine to look at, think twice before putting it anywhere public
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:?usage: dump-swarm.sh MODULES_DIR OUT_DIR}"
OUT="${2:?usage: dump-swarm.sh MODULES_DIR OUT_DIR}"

for dir in "$SRC"/*/; do
    name="$(basename "$dir")"
    for dll in "$dir"*.dll "$dir"*.DLL; do
        [ -f "$dll" ] || continue
        python3 "$HERE/../re/qrcfind.py" "$dll" "$OUT/$name" | grep -v ' 0 files' || true
    done
    cab="$(ls "$dir"device_meta_data/*.devicemetadata-ms 2>/dev/null | head -1)"
    if [ -n "$cab" ]; then
        tmp="$(mktemp -d)"
        # extract.py writes devices/NAME.png under --out, we want it as device-icon.png next to the rest
        if python3 "$HERE/extract.py" "$dir" icon --out "$tmp" >/dev/null 2>&1 && [ -f "$tmp/devices/icon.png" ]; then
            mkdir -p "$OUT/$name"
            cp "$tmp/devices/icon.png" "$OUT/$name/device-icon.png"
        fi
        rm -rf "$tmp"
    fi
done
