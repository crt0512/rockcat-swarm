#!/bin/sh
# runs a pile of cli commands against the simulated mouse and the simulated keyboard and compares what they print AND what they send over the wire
# with tests/cli_golden.txt. thats what catches a cli change that quietly sends something different to the device
#   tests/cli_golden.sh /path/to/rockcat-cli            compare, exit 1 on any difference
#   tests/cli_golden.sh /path/to/rockcat-cli --update   rewrite the expected file. look at the diff before committing it!
# idempotent, everything it writes lands in a temp dir that gets removed again
set -u

CLI="${1:?usage: cli_golden.sh /path/to/rockcat-cli [--update]}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

export ROCKCAT_SIM=kone-aimo-remastered
# the cli keeps a few host side settings in the config dir, they must not leak in from (or out to) the real one
export XDG_CONFIG_HOME="$WORK/config"

run() {
    echo "\$ rockcat-cli $*"
    rm -f "$WORK/wire.cap"
    ROCKCAT_CAPTURE="$WORK/wire.cap" "$CLI" "$@" 2>&1
    echo "[exit $?]"
    # timestamps and the header are noise, the transfers are what counts
    [ -f "$WORK/wire.cap" ] && grep '^[GSR] ' "$WORK/wire.cap" | sed 's/ @[0-9]*$//'
    echo
}

all() {
    run list
    run --device 1 info
    run --device 2e2c profile
    run --device 7 info
    run --device 2cee info
    run udev-rules
    run info
    run profile
    run profile 3
    run profile 6
    run show
    run show -p 2
    run set dpi1=800 dpi2=1600x1200 dpi-active=2 polling=500
    run set -p 3 dpi-enabled=1,3,5 angle-snapping=on debounce=4
    run set -p 3 dpi-enabled=1,2,5
    run set effect=fully-lit brightness=200 led=all:#65c3f7 led=wheel:#ff0000
    run set -p 4 led=left:#112233@128 led=2,3:#00ff00 led-enable=0x155 speed=3
    run set effect=aimo theme=3
    run set effect=breathing render=engine
    run set -p 2 effect=snake render=mouse
    run set dpi1=50
    run set polling=333
    run buttons
    run buttons -p 5
    run bind 9 shortcut ctrl+c
    run bind shift+11 volume-up
    run bind -p 2 8 brightness-up
    run bind 13 mute
    run reset-buttons -p 4
    run types
    run macro -b 8
    run macro -b 8 --text "gg wp" --delay 15 --name greeting
    run macro -p 3 -b shift+2 --text "A1!"
    run shift-light
    run shift-light on '#ff0000' pulse
    run shift-light -p 2 on '#00ff00@128' solid
    run tdcu
    run direct '#00ff00'
    run direct off
    run dump
    run dump -p 5
    run backup "$WORK/all.rswb"
    run restore "$WORK/all.rswb"
    run restore "$WORK/nope.rswb"

    # and the same idea for a keyboard, where keys go by name and there is a third layer
    export ROCKCAT_SIM=ryos-mk-fx
    run list
    run info
    run profile 2
    run profile
    run show
    run show -p 3
    run set effect=heartbeat speed=3 brightness=100 idle-minutes=5 idle-style=fall-asleep macro-feedback=on
    run set -p 2 led=all:#0000ff led=w,a,s,d:#ff0000 led=esc:#000000
    run show -p 2
    run set effect=wave
    run set effect=snake
    run set dpi1=800
    run buttons
    run bind esc a
    run bind shift+a ctrl+c
    run bind a ctrl+c
    run bind fn+f1 volume-up
    run bind shift+f1 mute
    run bind fn+a mute
    run bind t1 volume-down
    run bind nosuchkey a
    run buttons -p 1
    run reset-buttons
    run types
    run macro -b m1 --text "hi" --name greeting
    run macro -b m1
    run macro -b f1
    run direct '#00ff00'
    run direct off
    run dump -p 5
    run backup "$WORK/kb.rswb"
    run restore -p 2 "$WORK/kb.rswb"
    run restore "$WORK/all.rswb"

    # a second mouse, the pretend Kova (its profiles are what a real gray one answered)
    export ROCKCAT_SIM=kova-2016
    run list
    run info
    run show
    run show -p 2
    run set effect=breathing speed=2 dpi2=1200 led=1:#ff0000
    run set effect=wave
    run set effect=smooth-flow
    run buttons
    run bind 4 volume-up
    run types
    run macro -b 5 --text "gg" --name kova
    run direct '#00ff00'
    run direct off
    run backup "$WORK/kova.rswb"
    run restore "$WORK/kova.rswb"
    run restore "$WORK/kb.rswb"

    # the Kone line out of roccat-tools: one pretend mouse with one colour per light, one with a fixed dpi table
    for sim in kone-plus kone-xtd kone-pure-optical kone-aimo vulcan-120-aimo; do
        export ROCKCAT_SIM=$sim
        run info
        run show
        run set effect=fully-lit dpi1=1200 polling=500 led=1:#ff0000
        run buttons
        run bind 4 volume-up
        run direct '#00ff00'
        run direct off
    done
    # a device known by name only says so and doesnt get anything sent
    export ROCKCAT_SIM=vulcan-ii-mini-air
    run info
    run show
}

# the temp path shows up in a few messages and is different every run
all | sed "s|$WORK|TMP|g" > "$WORK/got.txt"

if [ "${2:-}" = "--update" ]; then
    cp "$WORK/got.txt" "$HERE/cli_golden.txt"
    echo "rewrote $HERE/cli_golden.txt"
    exit 0
fi
if diff -u "$HERE/cli_golden.txt" "$WORK/got.txt"; then
    exit 0
fi
echo "cli output or traffic changed, see the diff above. if thats on purpose: $0 $CLI --update"
exit 1
