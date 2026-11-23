#!/bin/sh
# takes a picture of the gui without putting a window on anybodys screen: the gui runs on gtks broadway backend (a display
# that only exists as a web page nobody opens) and draws its own window into a png (the hidden --shot option)
# for checking gui changes from a terminal, or by a machine
# usage: tools/dev/gui-shot.sh OUT.png [PAGE] [SIM] [PROFILE] [DEVICE]
#   PAGE     settings (default), buttons, illumination or advanced
#   SIM      which simulated device to show, default kone-aimo-remastered. "none" = whatever is really plugged in
#   PROFILE  1-5, the profile to open instead of the active one. "" = the active one
#   DEVICE   with several real devices: which one, the way rockcat-cli --device takes it (2fda)
#   PRESS="PREVIEW" in the environment presses the button that says that (several with commas) 0.7 s after the start
#   SIZE=1600x1000 in the environment makes the window that big
#   KEY=60 in the environment picks that key on the key assignment page
#   PLUG=kova-2016,ryos-mk-fx in the environment plugs those pretend devices in (with SIM=none), DEBUG=1 shows the debug menu
#   EFFECT=17 in the environment chooses that effect (a rockcat_effect number) on the illumination page first
#   SHOT_DELAY=2500 in the environment waits that many ms before the picture is taken (default 1500), for catching an
#   animation at another moment
# needs broadwayd (comes with gtk3). touches no real config: the gui gets a throwaway config dir
# idempotent, cleans up after itself
set -u

OUT="${1:?usage: gui-shot.sh OUT.png [PAGE] [SIM] [PROFILE] [DEVICE]}"
PAGE="${2:-settings}"
SIM="${3:-kone-aimo-remastered}"
HERE="$(cd "$(dirname "$0")" && pwd)"
GUI="${ROCKCAT_GUI:-$HERE/../../build/rockcat-swarm}"
[ -x "$GUI" ] || { echo "no gui at $GUI, build first (or set ROCKCAT_GUI)" >&2; exit 1; }

WORK="$(mktemp -d)"
N=17
broadwayd ":$N" >/dev/null 2>&1 &
BW=$!
trap 'kill $BW 2>/dev/null; rm -rf "$WORK"' EXIT
sleep 0.5
[ "$SIM" = none ] || export ROCKCAT_SIM="$SIM"
rm -f "$OUT"
GDK_BACKEND=broadway BROADWAY_DISPLAY=":$N" XDG_CONFIG_HOME="$WORK" timeout 30 "$GUI" --page "$PAGE" \
    ${4:+--profile "$4"} ${5:+--device "$5"} ${PRESS:+--press "$PRESS"} ${EFFECT:+--pick-effect "$EFFECT"} ${SIZE:+--shot-size "$SIZE"} ${KEY:+--pick-key "$KEY"} ${PLUG:+--plug "$PLUG"} ${DEBUG:+--debug} --shot "$OUT" --shot-delay "${SHOT_DELAY:-1500}" >"$WORK/log" 2>&1
[ -s "$WORK/log" ] && cat "$WORK/log" >&2
[ -s "$OUT" ]
