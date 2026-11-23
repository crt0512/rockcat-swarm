# Kova 2016

Gray (`1e7d:2cee`), white (`2cef`) and black (`2cf0`). The layouts are from roccat-tools 5.9.1 (`kova2016/libroccatkova2016/`), every number below was then looked at on a real gray one with firmware 1.18. The code is `src/lib/families/kova.cpp`.

It talks exactly like the Kone AIMO (see [README.md](README.md) for the part all devices share), just with smaller reports. Of the two hidraw nodes the mouse shows up as, the one that declares report `06` is the one to use.

## Reports

| id | bytes | what |
|---|---|---|
| `04` | 3 | control: `[04 profile 80]` selects settings, `[04 profile 90]` buttons, `[04 profile+10 button]` / `[04 profile+20 button]` the two halves of a macro. Reading it gives the status |
| `05` | 4 | `[05 04 active count]`, write it to switch profile |
| `06` | 28 | settings of the selected profile |
| `07` | 75 | buttons of the selected profile |
| `08` | 1026 | one half of a macro: `[08 n data...]`, n = 1 or 2 |
| `09` | 8 | info: `[09 08 firmware ...]`, firmware `0x76` = 118 = "1.18" |

## Settings, report `06`

| offset | bytes | what |
|---|---|---|
| 0 | 2 | `06 1c` |
| 2 | 1 | profile, 0-4 |
| 3 | 2 | unknown, `00 06` on every profile seen |
| 5 | 1 | handedness, `ff` right, `fe` left |
| 6 | 1 | dpi levels enabled, bit n = level n |
| 7 | 5 | dpi levels, value x 50 = dpi, 250 to 7000 |
| 12 | 1 | active dpi level, 0-4 |
| 13 | 1 | polling rate, 0-3 = 125 / 250 / 500 / 1000 Hz |
| 14 | 1 | lights: bit 0 light pipe on (the strip at the back), bit 1 wheel on (both checked by eye), bit 4 / 5 = that light uses its own rgb instead of a palette colour |
| 15 | 1 | colour flow, 0 / 1 |
| 16 | 1 | effect, 1 fully lit, 2 blinking, 3 breathing |
| 17 | 1 | effect speed, the vendor tools offer 1-3, a real profile had 0 |
| 18 | 4 | light pipe: palette index, r, g, b |
| 22 | 4 | wheel: palette index, r, g, b |
| 26 | 2 | checksum, sum of bytes 0-25, little endian |

## Buttons, report `07`

`[07 4b profile]` and then 24 entries of 3 bytes: function, parameter 1, parameter 2. First the 12 buttons, then the same 12 again for the Easy-Shift[+] layer. No checksum.

Order: left, right, middle, left Smart Cast, left forward, left backward, right Smart Cast, right forward, right backward, top, wheel up, wheel down.

The function codes are the usual Swarm ones (`ROCKCAT_BT_*` in the header). A shortcut is `05 modifiers key`.

## Macros, report `08`

A macro is 1997 bytes cut into two 1024 byte halves (the second one padded): profile, button (0-23), loop count, 40 bytes set name, 32 bytes macro name, 16 bit event count, then up to 480 events of 4 bytes each (key, 1 = press / 2 = release, 16 bit delay in ms). No header and no checksum, thats the difference to the Kone AIMO. A slot that was never written reads back as all `ff`.

## Not looked at yet

- what the `03` input reports say. Holding Easy-Shift sends nothing on the control node. `03 00 e5 0a 00` showed up once while trying and nobody knows what set it off. Profile and dpi switches were not watched
- reports `0a`, `0c`, `0e`, `0f`, `1a`

## Checked by hand on the gray one

- switching the active profile through report `05` works: lights and dpi change, and change back
- function `26` is volume up (roccat-tools has up and down the other way around, the Kone table was right)

## Lights from the pc, report `10`

ROCCAT's "Talk FX" report: `[10 10 ff ff ff state zone x effect speed r g b r g b]`. The three `ff` mean "leave Easy-Shift and Easy-Aim alone" (the same report can switch those). rockcat sends a colour the way roccat-tools does its "gfx" colours: `01 03 01 01 02` for state, zone, x, effect and speed, the colour twice, and the status on `04` looked at afterwards. Both lights take that one colour, there is no way to set them apart here. State 0 with everything after the `ff` zero gives the lights back to the profile.

Tried on the real gray one: with `x` and speed left at 0 (what rockcat sent at first) green made the lights go dark instead, the roccat-tools bytes show green. A rainbow at 83 colours a second (`rockcat-cli direct demo`) ran smooth, its owner watched it.

rockcat uses it for "Colour flow, smooth": the engine fades through the colours itself, because the mouse's own colour flow goes in visible steps. The mouse has "fully lit" stored meanwhile and which effect the pc draws is remembered on the pc, so without the engine running it just shows its colours.

