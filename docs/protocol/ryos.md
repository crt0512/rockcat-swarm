# Ryos MK FX

`1e7d:2fda`. The layouts are from roccat-tools 5.9.1 (`ryosmk/libroccatryosmk/` and `ryosmkfx/libroccatryosmkfx/`), every number below was then looked at on a real one with firmware 1.31. The code is in `src/lib/families/ryos.cpp`.

The family is called "ryos" and not "ryos mk fx" because the older boards (MK, MK Glow, MK Pro, TKL, TKL Pro) use the same reports for everything except the lighting. They live in the same file, see [the older boards](#the-older-boards-mk-mk-glow-mk-pro-tkl-tkl-pro) at the end.

It talks like the mice (see [README.md](README.md) for the part all devices share): feature reports, a select on `04` before reading, and after every write `04` gets polled until it says ready. One difference in timing: after a select this keyboard is ready at once. Swarm and roccat-tools wait 150 ms before asking, rockcat waits 2 ms, which was measured on the real keyboard (same data on three profiles, 0.2 s per profile instead of 1.5). After writes the 150 ms stay. The keyboard shows up as three hidraw nodes, the one to talk to is interface 1, the only one that declares these reports.

## Reports

| id | bytes | select | what |
|---|---|---|---|
| `04` | 3 | | control: `[04 profile request]` picks what the next read returns. Reading it gives the status |
| `05` | 3 | | `[05 03 xy]`: y = active profile, x = number of profiles (5). Write it to switch profile |
| `06` | 125 | `a0` | primary keys: one byte per key |
| `07` | 95 | `a1` | F1 to F12, Print, Scroll, Pause: 15 keys x 3 bytes, then the same 15 with Fn held |
| `08` | 35 | `a2` | M1 to M5: 5 keys x 3 bytes, then with Easy-Shift |
| `09` | 23 | `a3` | T1 to T3 (thumb keys): 3 keys x 3 bytes, then with Easy-Shift |
| `0a` | 8 | `a4` | Caps Lock and Fn, one byte each |
| `0b` | 294 | `a5` | 96 keys with Easy-Shift held, 3 bytes each. The only report with a two byte size (`26 01`) |
| `0c` | 6 | `b0` | key mask: which keys the game mode switches off. Passed through untouched |
| `0d` | 32 | `b1` | light settings |
| `0e` | 1026 | | one half of a macro, see below |
| `0f` | 8 | | info: `[0f 08 firmware ...]`, firmware `0x83` = 131 = "1.31" |
| `13` | 8 | | light control: who owns the leds, and the answer to "did you take that frame" |
| `17` | 239 | `b3` | stored lights: the colour of every key |
| `18` | 232 | | custom lights: one frame from the pc |

Every report from `06` to `0d` and `17` starts with id, size and profile (the profile is the fourth byte in `0b`) and ends with a checksum: the sum of all bytes before it, 16 bit, low byte first.

Not looked at: `03` (events, see below), `11` reset, `12`, `14`, `15` device state, `16` talk, `19` light macro, `1b` / `1c` firmware update.

## Keys

Keys are numbered by their place in the key matrix, 0 to 119. That number is the position of the key in report `06`, its bit and nibble in the lighting reports, and the index Swarm uses in its `kb_matrix.xml`, so the key tables in `src/lib/keyboards/` fit without translation. Not every slot has a key. The thumb keys are 121 to 123 in Swarm's numbering and have no led of their own.

What a key does is one byte, or three bytes `type p1 p2`:

- below `0x73` the type is the HID usage of a key: the key simply is that key
- `b8` is a shortcut: p1 = modifiers (bit 0 ctrl, 1 shift, 2 alt, 3 win), p2 = HID usage
- `be` plays the macro stored for that key
- the rest are functions of the keyboard: `aa`-`ae` M1 to M5, `b0` / `b1` profile up / down, `b3`-`b7` profile 1 to 5, `cb`-`d2` media keys, `d3`-`d7` shift+1 to 5, `d8`-`dc` ctrl+1 to 5, `dd` open driver, `f1` Fn, `ff` Easy-Shift. The full list is `rockcat-cli --device 2fda types`

Where the bytes of a key are depends on the key:

| key | normal | with Easy-Shift | with Fn |
|---|---|---|---|
| most keys | 1 byte in `06` | 3 bytes in `0b` | |
| F1 to F12, Print, Scroll, Pause | 3 bytes in `07`, first half | | 3 bytes in `07`, second half |
| M1 to M5 | 3 bytes in `08`, first half | second half | |
| T1 to T3 | 3 bytes in `09`, first half | second half | |
| Caps Lock | byte 3 of `0a` | | |
| Fn | byte 4 of `0a` | | |
| Esc | 1 byte in `06` | | |

rockcat calls these layer 0, 1 and 2. roccat-tools files the second half of `07` under Easy-Shift, but on a real keyboard Fn + F1 mutes and Easy-Shift + F1 is simply F1, so here it is the Fn layer. The function keys have nothing of their own with Easy-Shift.

Which key of `0b` is which matrix slot is a table out of roccat-tools (`ryos_key_relations.c`), it is `ZONE_INPUT` in the code.

Out of the box Caps Lock is the Easy-Shift key (`ff`), M1 to M5 are shift+1 to 5 (ctrl+1 to 5 with Easy-Shift), and F1 to Pause with Fn are what is printed on the key (mute, volume, calculator, ...). Profile 1 of the keyboard here is byte for byte the factory layout roccat-tools ships, that is where `reset-buttons` gets its table from.

The second half of `09` is the Easy-Shift side, checked by hand: Easy-Shift + T1 toggles Caps Lock, which is what the factory layout has there. For `08` (M keys) that is still roccat-tools' word, ctrl+1 to 5 show nothing when pressed.

## Light settings, report `0d`

| offset | bytes | what |
|---|---|---|
| 0 | 2 | `0d 20` |
| 2 | 1 | profile, 0-4 |
| 3 | 1 | brightness, 0-5 |
| 4 | 1 | brightness when idle, 0-5 |
| 5 | 1 | minutes until idle, 0 = never |
| 6 | 1 | mode: 0 = every key the colour at offset 14, 1 = the colours of report `17` |
| 7 | 1 | effect: 0 off, 1 fully lit, 2 blinking, 3 breathing, 4 heartbeat, 10 fade (a key lights up when pressed). 5 to 9 are equalizer, ripple, wave, heatmap and game preset, which Swarm drew from the pc |
| 8 | 1 | unknown, 0 |
| 9 | 1 | effect speed, 1-3 |
| 10 | 1 | unknown, 0 |
| 11 | 1 | macro feedback: 1 = the lights show a macro running |
| 12 | 1 | idle style: 0 dim to the idle brightness, 1 starlit sky, 3 fall asleep |
| 13 | 1 | unknown, `1e` on the profile in use and 0 on the others |
| 14 | 6 | colour for mode 0: red, green, blue, 16 bit each (see below) |
| 20 | 10 | unused |
| 30 | 2 | checksum |

## Colours

The leds do not take 0 to 255. A colour value is sent as a pwm value (0-63) and a brightness step (0-15), and how bright that comes out is pwm x (step + 1). roccat-tools has a table that says which pair to send for each of the 256 values (`COLOUR_TO_HARDWARE` in `ryos_tables.inc`). In report `0d` the pair is a 16 bit number, pwm in the low byte.

Reports `17` and `18` carry the colours of all 120 keys in the same 228 bytes:

| offset | bytes | what |
|---|---|---|
| 0 | 15 | a bit per key: lit or not |
| 15 | 60 | red: a nibble per key, 0 = none, 1-7 = palette entry (low nibble = even key) |
| 75 | 60 | green, same |
| 135 | 60 | blue, same |
| 195 | 7 | red palette, pwm values |
| 202 | 7 | green palette |
| 209 | 7 | blue palette |
| 216 | 4 | red palette, brightness steps, a nibble each |
| 220 | 4 | green |
| 224 | 4 | blue |

Nine of the 120 slots have no key on an ISO or ANSI board: 32, 71, 91, 100, 101, 102, 111, 113, 119 (five of them are keys on japanese, korean or brazilian boards, four are nothing). They must stay dark. Tried on a real ISO keyboard: with those nine lit and everything else black, M3, M5, right Ctrl, Y, C and B came on bright in the colour of slot 71 and more keys glowed dimly. rockcat sends them as black in every frame and every stored profile. The thumb keys T1 to T3 are not among the 120 and did not react to any of this, their light cannot be set. What their owner saw them do: they go dark while Fn is held, and the brightness keys (Fn + F11) leave them alone unless all other lights are off. So the firmware runs them by itself.

So there is no free rgb per key: each channel has 7 values to pick from. Up to 7 different reds, greens and blues fit exactly, a frame with more gets rounded to the 7 values per channel that fit best (k-means on a line, `pick_palette`).

Report `17` is `[17 ef profile]`, those 228 bytes, 6 more bytes (1 unknown, 5 unused) and the checksum. When every key has the same colour, rockcat writes mode 0 with that colour into `0d` and the same picture into `17`.

## Lighting from the pc

- `[13 08 01 00 00 00 00 00]` hands the leds to the pc, `[13 08 00 ...]` gives them back to the profile
- the keyboard stays in pc mode until it is told otherwise. Switching the profile does not end it: the new profile flashes up for a moment and the last frame comes back. So whatever starts pc mode has to end it, and the gui sends the "back to the profile" report every time it reads or writes a profile
- a frame is `[18 e8]`, the 228 bytes from above, checksum
- after a frame the keyboard wants to be asked: read `13`, byte 6 is 1 = taken, 3 = still busy, anything else = refused

On the keyboard here that gives about 37 frames a second (`rockcat-cli direct demo`), every frame confirmed.

## Effects the pc draws

Effect 5 to 9 in report `0d` mean "the pc draws this": equalizer, ripple, wave, heatmap, game preset. The keyboard shows nothing of its own for them and starts reporting key presses (see Events). rockcat's lighting engine (`src/lib/engine.cpp`, `run_keyboard`, the effects themselves in `src/lib/kb_effects.cpp`) draws wave, ripple, heatmap and equalizer under their own numbers, and two effects ROCCAT never had, fire and drip. Those two have no number on the keyboard: it gets told 9 (game preset, which shows nothing and makes it report key presses) and which effect it really is stays in the config file on the pc. The engine watches the active profile, takes the leds when one of these is set and gives them back for anything else. It runs inside the gui (window or tray) or as `rockcat-cli --device 2fda engine`. Each effect has settings (speed, colours, ...) that live on the pc as well, `rockcat_effect_options()` lists them.

## Macros

Same format as the Kova 2016 (see [kova.md](kova.md)): two halves of 1024 bytes in report `0e`, `[0e 01 ...]` and `[0e 02 ...]`. To read one, select `[04 profile+10 slot]` for the first half and `[04 profile+20 slot]` for the second.

Slots: 0-4 M1 to M5, 5-9 the same with Easy-Shift, 10-12 T1 to T3, 13-15 with Easy-Shift, and from 16 on the keys of report `0b` (slot = 16 + position in that report). Plain keys without Easy-Shift and the function keys cannot hold a macro.

## Events

The keyboard reports things on `03`: `[03 subtype type data action]`. Seen on the real keyboard:

| what was pressed | reports |
|---|---|
| Caps Lock (Easy-Shift) down | `03 00 0a ff 00`, then `03 00 fd 01 00` |
| Caps Lock up | `03 00 0a ff 01`, then `03 00 fd 00 00` |
| T2 (open driver) down / up | `03 00 10 01 00` / `03 00 10 00 00` |
| Fn | nothing |
| M1 to M5 (shift+1 to 5) | nothing, they just type |
| any key, while the active profile has effect 5 to 9 | `03 00 fb NN 01` down, `03 00 fb NN 00` up, NN = the key's slot + 16 (Esc `11`, A `4d`, Space `80`) |

So Easy-Shift is reported twice, and the two types say it differently: `0a` in the action byte (0 = down), `fd` in the data byte (1 = on).

That last line is what makes ripple and heatmap possible without reading what gets typed: the keyboard only says which key, only while such an effect is on, and nothing of it is kept.

From roccat-tools and not seen yet: type `01` / `02` = profile (data = which), `03` macro key, `04` live recording, `0b` multimedia, `0c` backlight.

## What was tried on the real keyboard

Read: all five profiles, firmware, active profile (this is the recording in `tests/captures/ryos-mk-fx-readonly.cap`).

Written, on profile 5 (which was empty) and read back: brightness, effect, speed, idle settings, key colours (three different ones), a key in `06`, one in `08`, one in `0b`, a macro on M1. Afterwards profile 5 was restored from a backup and all five profiles were byte for byte what they were before.

Lighting from the pc: three seconds of a rainbow, then back to the profile.

Switching the active profile works. One thing to know: the keyboard forgets which keys are down when it switches. Switch while a key is held (the Enter that sent the command) and its release never arrives, the pc sees the key stuck until the next key press. `rockcat-cli profile N` waits 400 ms before it switches a keyboard for that reason, and with that wait the key no longer sticks (tried on the real keyboard).

Not tried: reset.

## The older boards: MK, MK Glow, MK Pro, TKL, TKL Pro

| board | pid | leds | sim | seen for real |
|---|---|---|---|---|
| Ryos MK | `3138` | none | `ryos-mk` | no |
| Ryos MK Glow | `31ce` | one brightness for all keys | `ryos-mk-glow` | no |
| Ryos MK Pro | `3232` | on / off (and blink) per key, blue | `ryos-mk-pro` | yes, firmware 1.26 |
| Ryos TKL | `3106` | none | `ryos-tkl` | no |
| Ryos TKL Pro | `316a` | on / off per key, blue | `ryos-tkl-pro` | no |

Key reports `06` to `0c`, macros `0e`, info `0f`, profile `05` and the events on `03` are the ones above, same sizes, same selects. The TKL boards have no M keys and roccat-tools never asks them for `08`, so rockcat doesnt either, M1 to M5 answer "no such key" there. Everything else below is roccat-tools (`ryosmk/libroccatryosmk/`, `ryostkl/libroccatryostkl/`), on the MK Pro it was also read, written and sent frames.

On the MK Pro `05` says `[05 03 0y]`, the count half is 0. rockcat reports 5 anyway.

### Light settings, `0d`, 16 bytes

`[0d 10 profile brightness dim minutes ? ? ? ? illumination feedback dimstyle ? sum sum]`. brightness, dim, minutes, feedback and dimstyle sit where they are on the MK FX (0 to 5, minutes, 0 / 1, 0 dim 1 starlit 2 macro 3 fall asleep). illumination: 0 = the automatic set shows, 1 = the manual one. The four unknown bytes are `f4 01 00 00` on every profile of the real MK Pro, 500 as 16 bit, nobody knows of what. They get passed through. On the MK and TKL (no leds) the report is still read and kept, nothing in it can be changed through rockcat.

### MK Pro: two sets of 11 layers, `17`, 1382 bytes

Select `d0` gives the manual set, `c0` the automatic one: `[17 66 05 profile mode]` (size is 16 bit, mode 1 manual, 0 automatic, the real one says exactly that), then 11 layers of 125 bytes: effect (0 nothing, 1 a pressed key fades, 2 ripple), fade time in seconds, then 123 bytes for the primary keys and the three thumb keys, bit 0 lit, bit 1 blinks. Layers are normal, left shift, right shift, left ctrl, right ctrl, left alt, right alt, left win, right win, Easy-Shift, Fn.

rockcat shows and changes layer 0 (normal) of whichever set `0d` picks: the lit bit per key and the effect byte. Blink bits, fade time, the other ten layers and the other set go back untouched. The factory set of the real one lights 116 of 120: 111 keys of the ISO board plus 5 slots only japanese / korean boards have a key on.

### TKL Pro: a report per layer, `17`, 253 bytes

`[17 fd profile mode layer effect delay fade]`, then 120 x 16 bit big endian (bit 15 lit, bits 12-13 key effect off / blink / breathe, 6-8 breathe speed, 2-4 blink speed) and three thumb bytes (bit 7 lit). Select `c0 | layer` for the automatic ones (layers 0, 9, 10 only) and `d0 | layer` for manual (all 11). Same deal as the MK Pro: the normal layer of the chosen mode is what rockcat shows, only bit 15 and the effect byte move. Never seen on a real one.

### Lighting from the pc

`13` takes the leds like on the MK FX, a frame is `[18 14 profile data[15] sum sum]`, a bit per primary key, then the usual wait on `04` (10 ms first, roccat-tools does the same). No fast "took it" byte in `13` on these. A colour counts as lit from half brightness on, the 9 slots without a key stay dark. 48 frames a second on the real MK Pro.

That makes the engine effects work on the MK Pro and TKL Pro too (wave, heatmap, equalizer, fire, drip), squashed into on and off by the engine itself (`one_colour()` in `devices.h`): a key goes on above 55 %, off below 45 %, so edges dont flicker, and the gui shows the engine frame as it is. The engine doesnt dim these frames by the brightness setting the way it does on the MK FX, that would only switch keys off, the keyboards own brightness (`0d` byte 3) is left to do it. Writing that byte works on the real one (read back 5, 2, 5), whether the firmware also dims frames from the pc with it nobody has looked yet. While one of those runs the keyboard has "fully lit" stored (the carrier, like on the Kova), so without the pc it shows the keys lit in the profile. The colour options of those effects are hidden in the gui, only their brightness and shape settings count. The engine swaps modes that only differ in hue: wave always runs as "key colours, brightness waves", rainbow equalizer bars are plain bars, the gui hides those choices.

### Not done

- light macros (`19`, select `e0`) and dimstyle 2 that uses them
- the fade time, the other layers, blink / breathe per key: the bytes survive, nothing in the api can set them
- the MK Glow probably has a whole board level somewhere, roccat-tools only ever changes brightness on it
- select delay: the MK Pro is ready right after a select like the MK FX, 2 ms it is (full backup 1.5 s instead of 10, same bytes three times). the boards nobody has here keep the safe 150 ms

