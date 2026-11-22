#!/bin/sh
# records everything a Kova 2016 (gray, white or black) has stored: info, active profile, settings and buttons of all 5 profiles
# run it as: tools/capture/kova2016.sh out.cap
# read only. the only thing it sends is the "show me profile N" select, your settings stay exactly as they are
set -e
. "$(dirname "$0")/common.sh"

OUT="${1:?usage: kova2016.sh OUT.cap}"
NODE="$(find_node "2cee 2cef 2cf0")"
[ -n "$NODE" ] || { echo "no Kova 2016 found, is it plugged in?"; exit 1; }
rm -f "$OUT"

plain 09 8
plain 05 4
for p in 00 01 02 03 04; do
    fetch "$p" 80 06 28
    fetch "$p" 90 07 75
done
echo "done, $(grep -c '^[GS]' "$OUT") transfers in $OUT"
