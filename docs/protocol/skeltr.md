# Skeltr

`1e7d:2fc6`, the one with the phone holder. Out of roccat-tools 5.9.1 (`skeltr/libroccatskeltr/`), never on hardware. Code: `src/lib/families/skeltr.cpp`, tables in `skeltr_tables.inc`, pretend one `skeltr`.

Ryos conversation (select `[04 profile request]`, read, poll `04` after writes), byte sum at the end of every report.

| id | select | bytes | what |
|---|---|---|---|
| `05` | | 4 | `[05 04 active count]` |
| `06` | `40` | 149 | primary: a byte per slot, 144 slots |
| `07` | `50` | 6 | game mode key mask |
| `08` | `60` | 95 | F1 to Pause, 3 bytes each, 15 plain then 15 with Fn |
| `09` | `b0` | 65 | the 20 Easy-Shift keys (1-5, Q-T, A-G, Z-B), 3 bytes each |
| `0a` | `90` | 8 | Caps Lock, Fn |
| `0b` | `d0` | 23 | T1 to T3, then with Easy-Shift |
| `0d` | `a0` | 35 | M1 to M5, then with Easy-Shift |
| `0e` | profile, slot | 2002 | one macro, the Sova layout |
| `0f` | | 8 | firmware in byte 2 |
| `10` | `70` | 36 | light, below |
| `13` | | 8 | `[13 08 mode]`, 1 = the pc owns the light |
| `18` | | 17 | `[18 11 rgb x 5]`, the five zones from the pc |
| `17` | `c0` | | bluetooth, not touched |

Inputs are Swarms key numbers, which are the slots of `06` (roccat-tools default `06` has the usage of every key in the slot Swarm gives it). T1 to T3 are 255 to 257 and Caps Lock 258, like in Swarms table. Esc and Fn cant be set. Macros: M keys 0 to 9, thumb keys 10 to 15, the Easy-Shift side of the zone keys 16 to 35.

Light: `[10 24 profile mode preset speed start sleep wake switch flow (index r g b) x 5 brightness ? minutes sum]`. mode 1 = the zones show their own rgb. preset 0 off, 1 fully lit, 2 blinking, 4 breathing, 5 pulsing (no name of ours, survives untouched), 6 heartbeat, 7 fade fx, 8 heatmap. speed 0 to 2, brightness 0 to 5. The zones are the keys, top left, bottom left, top right, bottom right: five leds, not one per key.

Not sure: how long the keyboard needs after a frame on `18`. roccat-tools waits 160 ms before asking, we ask after 20 so frames dont crawl.

## Isku+ Force FX

`1e7d:3214`, the second model in `skeltr.cpp`, pretend one `isku-plus-force-fx`. NOT in roccat-tools, out of ROCCATs Swarm: the monitor class `isku_plus_force_device_c` (vtable `0x8d5188`) reads a profile in `FUN_004bdc00` with the Skeltr selects (`60`, `90`, `d0`, `b0`, `a0`, `40`), and the module default (`isku+_force_fx.dll`, copy in `DEV/re-notes/`) has the Skeltr report ids (`06`, `08`, `0a`, `0b`, `0d`) and the same key numbers. Differences:

- the Easy-Shift report is `0b` and not `09`: the shared read `FUN_004eaac0` puts `09` in front only for one product type (the Skeltr) and `0b` for every other. That makes two reports with id `0b` on this keyboard, Easy-Shift (select `b0`, 65 bytes) and thumb keys (select `d0`, 23 bytes). Reading goes by the select, writing has only the size to tell them apart. GUESS that the firmware does it that way
- no key mask report is read
- the light is three reports: `10` with 20 bytes on select `70` (in the module default), and two more on `c1` (23 bytes) and `c2` (103 bytes) whose ids are not known. The 20 bytes are read and written back untouched, nothing in them is decoded, the other two are not touched at all. So: no light settings for this one yet

The module default keeps the Easy-Shift report as Swarms own 294 byte buffer, the factory one here is the first 20 entries of it (which are the 20 zone keys in order).

The Isku+ (`isku-plus.md`) has the same key numbers but the Ryos selects and report ids, the Skeltr ones are only shared with the Force FX.
