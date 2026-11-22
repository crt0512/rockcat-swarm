#!/bin/sh
# records everything a Ryos MK FX has stored: info, active profile and per profile all key layers, key mask, light and stored lights
# run it as: tools/capture/ryos-mk-fx.sh out.cap
# read only. the only thing it sends is the "show me X of profile N" select, your settings stay exactly as they are
# macros are left out on purpose, those are your keystrokes and a recording ends up in a public repo
set -e
. "$(dirname "$0")/common.sh"

OUT="${1:?usage: ryos-mk-fx.sh OUT.cap}"
NODE="$(find_node "2fda")"
[ -n "$NODE" ] || { echo "no Ryos MK FX found, is it plugged in?"; exit 1; }
rm -f "$OUT"

plain 0f 8
plain 05 3
for p in 00 01 02 03 04; do
    fetch "$p" a0 06 125   # keys primary
    fetch "$p" a1 07 95    # keys function
    fetch "$p" a2 08 35    # keys macro
    fetch "$p" a3 09 23    # keys thumbster
    fetch "$p" a4 0a 8     # keys extra
    fetch "$p" a5 0b 294   # keys easyzone
    fetch "$p" b0 0c 6     # key mask
    fetch "$p" b1 0d 32    # light
    fetch "$p" b3 17 239   # stored lights
done
echo "done, $(grep -c '^[GS]' "$OUT") transfers in $OUT"
