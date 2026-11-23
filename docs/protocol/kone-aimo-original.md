# Kone AIMO (the original one)

`1e7d:2e27`, Swarm product type 26 (`0x1a`), module `KONE_AIMO`. Code: `src/lib/families/kone_aimo_original.cpp`, pretend one `ROCKCAT_SIM=kone-aimo`. Nobody here has one, none of this was ever sent to a real mouse.

It is the Kone AIMO Remastered ([kone-aimo.md](kone-aimo.md)) with a shorter settings report. Everything else is shared, and the code borrows it from the Remastered family as it is: buttons (`07`, 75 bytes, same 24 slots, same factory layout), macros (`08`), active profile (`05`), info (`09`), direct lighting (`0d` + `0e`), events.

How we know:

- `ROCCAT_Swarm_Monitor.exe` (Swarm 1.9477) drives both mice with one class, `kone_aimo_base_device_c`. Its settings read (`FUN_0051ea60`) does `[04 profile 80]` and then reads `0x69` bytes when the product type is `0x1a` and `0x7e` otherwise. The buttons (`0x4b`) and the direct colours (`0x2e`) have one size for both
- `KONE_AIMO.dll` v1.0135 (`DEV/Roccat-Swarm/Modules/KONE_AIMO/kone_aimo.dll`) has every settings setter twice, once for each layout: it writes `0x7e * profile + x` for the Remastered and `0x69 * profile + y` for this one. Those pairs give the table below
- the module caps the dpi spin box at 12000 for this mouse and 16000 for the Remastered (`QSpinBox::setMaximum(12000)` next to `setMaximum(16000)`)

## Settings, report `06`, 105 bytes

Select `[04 profile 80]`, then read 105 bytes. Ends in the usual 16 bit byte sum.

| offset | Remastered | bytes | what | where it was seen |
|---|---|---|---|---|
| 0 | 0 | 2 | `06 69` | |
| 2 | 2 | 1 | profile | |
| 3 | 3 | 1 | unknown | |
| 4 | 4 | 1 | dpi levels enabled, bit n = level n, factory `1f` | `FUN_6a4133c0` writes `1f` to `+4` in both layouts |
| 5 | 6 / 0x10 | 5 | dpi per level, one byte, x 50 (100 to 12000). The Remastered has x and y as words here | `FUN_6a412ec0`: `param_3 / 0x32` into `+5+n`, the other branch writes words to `+6` and `+0x10` |
| 10 | 5 | 1 | active dpi level | `FUN_6a412d40`: `+10` here, `+5` on the Remastered |
| 0x0b | 0x1a | 1 | polling rate 0-3 | `FUN_6a41e890`, reset value 3 in `FUN_6a41f3a0` |
| 0x0c | 0x1b | 1 | angle snapping | `FUN_6a41f3a0` (reset to 0) |
| 0x0d | 0x1c | 1 | unknown | |
| 0x0e | 0x1d | 1 | debounce ms, factory 8 | `FUN_6a41f3a0` |
| 0x0f | 0x1e | 2 | led enable mask, 11 bits | NOT seen as a pair, follows from the bytes around it (GUESS) |
| 0x11 | 0x20 | 1 | unknown | |
| 0x12 | 0x21 | 1 | effect | `FUN_6a450fe0`, `FUN_6a451410`, ... (9 = AIMO, written by the AIMO button) |
| 0x13 | 0x22 | 1 | effect speed | `FUN_6a450e50`, `FUN_6a45a040` |
| 0x14 | 0x23 | 1 | brightness | `FUN_6a4515a0`, `FUN_6a45b660` |
| 0x15 | 0x24 | 25 | unknown | |
| 0x2e | 0x3d | 55 | 11 leds x `[a r g b amber]` | `param_3 * 5 + 0x2e + param_2 * 0x69` in the colour setter |
| 0x65 | 0x74 | 1 | AIMO colour theme, bit 7 a flag | `FUN_6a45a390` |
| 0x66 | 0x75 | 1 | unknown | |
| 0x67 | 0x7c | 2 | checksum | |

The Remastered has 6 more unknown bytes at `0x76` that this one doesnt.

## What is a guess

- the led enable mask position (see the table)
- which effects the firmware draws by itself: taken over from what was measured on a Remastered (4, 7, 8 and 9 are drawn by the pc)
- that the 11 leds sit where they sit on the Remastered (same shell, same Swarm pictures `Led_N.png`)
- that the firmware Easy-Shift colour and the amber channel behave like on the Remastered

## Try first

`rockcat-cli --device 2e27 backup FILE`, then `show` on every profile and compare with what Swarm showed. If the dpi values look like garbage the dpi bytes are elsewhere, nothing gets written by `show`.
