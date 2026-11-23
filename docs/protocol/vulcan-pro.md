# Vulcan Pro, Vulcan TKL Pro, Vulcan II, II Max, II Mini, Pyro, Magma, Magma Mini

Vulcan Pro (`1e7d:30f7`), Vulcan TKL Pro (`1e7d:311a`, `1e7d:3167`), Vulcan II (`1e7d:2f4e`), Vulcan II Max (`1e7d:2ee2`) and Vulcan II Mini (`1e7d:2f09`). Code: `src/lib/families/vulcan_pro.cpp`, tables in `vulcan_pro_tables.inc` and the `vulcan_*_keys.inc` next to it. Pretend ones: `ROCKCAT_SIM=vulcan-pro`, `vulcan-tkl-pro`, `vulcan-ii`, `vulcan-ii-max`, `vulcan-ii-mini`.

NOBODY HERE HAS ONE. The firmware in these modules is encrypted (`fw_V1.21.bin` starts with noise, `Command_Key.bin` next to it is for `USBCmdLib.dll`), so unlike the AIMO boards ([vulcan.md](vulcan.md)) there is no firmware to check against. Everything comes from:

- `ROCCAT_Swarm_Monitor.exe` 1.9477: `vulcan_pro_device_c` (vtable `0x8cfd40`), `vulcan_tkl_pro_device_c` (`0x8d4e98`), `vulcan2_mk_device_c` (`0x8cfa90`), `vulcan2_max_device_c` (`0x8d0f64`), `vulcan2_mini_device_c` (`0x8d2250`). The II Max and Mini classes use the same report sizes as their modules. In these classes vtable `+0x84` sends a feature report and `+0x88` reads one
- the Swarm modules Turtle Beach still served: `VULCAN_PRO.dll` v1.0013 (it carries the full size and the TKL setup), `VULCAN_TKL_PRO.dll` v1.0016, `VULCAN_II.dll` v1.0002, `VULCAN_II_MAX` v1.0012, `VULCAN_II_MINI` v1.0014. Their default profile (compressed Qt resource) and the uncompressed text resources next to it (`key_pos_mapping.txt`, the `rgb_led_mapping` with the heading "16. Backlight Setting (ID 0x11)", `key_data_mapping.txt` with `[ES_ZONE]`, `[FN_ZONE]`, `[M_ZONE]` and `[REMAP_KEYS]`)

## Conversation

The Ryos way again: a select on `04`, the status on `04`, 16 bit byte sum at the end of every report. Two differences to the AIMO boards:

- the select is four bytes, `[04 profile request 00]`
- the board keeps **five profiles** again. `05` is `[05 04 active count]`, read and written as four bytes (`FUN_004b1e10` reads it, `FUN_004b8e80` writes it back with the new number)

Reading a profile (`FUN_005da6c0`): for each part, select `[04 p request 00]`, then read the report. Writing (`FUN_005d9bf0`): every report that changed gets its checksum and goes out as it is, the profile byte inside, no select.

| id | select | Pro | TKL Pro | II | II Max | II Mini | what |
|---|---|---|---|---|---|---|---|
| `06` | `a1` | 134 | 134 | 134 | 134 | 134 | `[06 86 profile 01]` + one byte per key matrix slot (128, HID usages) + checksum. Swarm calls it `game_mode_default_data` |
| `07` | `a2` | 11 | 11 | 11 | 14 | 11 | "caps", `[07 0b profile 00 00 0b 0a 00 00]` + checksum out of the box. Not understood, kept |
| `0a` | `a3` | 125 | 189 | 189 | 181 | 262 | Fn zone: `[0a size profile]`, one 4 byte entry per key of `[FN_ZONE]`, then the same keys with Fn held |
| `0b` | `a4` | 45 | none | 53 | 53 | none | M zone (game mode keys M1 to M6 on Ins to PgDn, and the two knobs of the Pro). Kept, not shown |
| `0c` | `a5` | 85 | 85 | 89 | 518 | 518 | Easy-Shift zone, one entry per key of `[ES_ZONE]` (20 keys, the II has Space as a 21st). On the II Max and Mini one entry per key matrix slot (128), so every key has an Easy-Shift side |
| `11` | `b0` | 371 | 299 | 377 | 542 | 332 | light, see below |

There is a second 134 byte report per profile on select `a0` (read in `FUN_005d4f10` next to `a1`), never written by Swarm. rockcat leaves it alone. Also read once: a 16 byte `0d` (bytes 2, 3 and 6 get set by Swarm, 6 looks like the brightness step while the pc draws AIMO) and a 128 byte `0f` on select `c1`. Not used.

A report longer than 255 bytes has a two byte size (`[0c 06 02 profile]`), so its profile byte and its first entry sit one byte further.

Keys are numbered the way these modules number them, 1 to 123 (the same scheme as the [Vulcan TKL AIMO](vulcan.md#vulcan-tkl-aimo)), and the slot table is the same too.

## Key entries

`0a`, `0b`, `0c` hold 4 bytes per key: `[a b code kind]`. Out of the box `a` and `b` are always 0. Kinds seen in the defaults: `0c` a key (`code` = HID usage), `03`, `08`, `0b` and `06` functions, and `07` is a macro (`FUN_005da6c0` looks for kind 7 and then fetches a 1044 byte macro).

rockcat shows them with the Ryos names. The AIMO boards have Ryos codes on the same keys out of the box, which gives:

| entry | Ryos | how sure |
|---|---|---|
| `[08 04]` to `[08 07]` (Fn + F1 to F4) | `b3` to `b6`, profile 1 to 4 | same keys |
| `[03 10] [03 09] [03 12] [03 11]` (Fn + F5 to F8 on the Pro) | `c2 c3 c0 c1`, my computer, browser, email, calculator | same keys |
| `[03 02] [03 05] [03 04] [03 03]` (Fn + F9 to F12) | `ce cf cc cd`, previous, stop, play/pause, next | same keys |
| `[03 07] [03 08]` | `d0 d1`, volume up / down | GUESS: the Pro has them on its volume knob |
| `[03 06]` | `d2`, mute | GUESS: fills the gap |

Anything else shows as `kept` with the code and kind in the two parameters, and goes back exactly like that. A key entry whose `a` or `b` isnt 0 is kept as well.

Normal layer: F1 to Pause (and on the TKL boards the nav keys, arrows and right Ctrl, whatever `[FN_ZONE]` lists) are in the first half of `0a`. Every other key in `[REMAP_KEYS]` is one byte in `06`, a key only. Ins to PgDn arent in `[REMAP_KEYS]` on the Pro: they are its game mode keys and live in `0b`, which rockcat doesnt show yet. That `06` is what the board uses all the time and not only in game mode is a GUESS from its place in the key assignment page of Swarm.

## Lights, report `11`

`[11 size16 profile effect speed brightness theme ?]`, then the colours. Effect, speed (1 to 11) and theme like on the AIMO boards, the brightness is the AIMO step (1 to 69) one byte earlier. Where a keys red, green and blue are comes from each module's "Backlight Setting (ID 0x11)" table: the Pro and TKL Pro keep the AIMO blocks of 12 red, 12 green, 12 blue, the Vulcan II has r g b side by side per led (its table lists only red, green and blue follow it).

## II Max and II Mini

- the Max `[ES_ZONE]` is the slot table backwards (entry n = the key in matrix slot n). The Mini module has no `key_data_mapping.txt` at all, so its Easy-Shift list is made the same way, every key of its picture counts as remappable, and its 32 Fn keys come from its own factory `0a`: the first half holds each keys own HID usage, and `06` says which key has that usage. That gives Esc, the number row (F1 to F12 with Fn), Del Home PgUp PgDn and the keys the Mini puts functions on. One entry has usage `00` and stays without a key
- the Mini module says Esc is slot 1 like on the big boards, but its factory `06` has Esc (`29`) in slot 2 (where `` ` `` sits on a big board, the Mini has none) and nothing in slot 1. Every other key agrees with the table, so rockcat uses slot 2 for Esc. GUESS
- the Mini has nothing on Easy-Shift out of the box: `[00 00 00 0c]`, which rockcat shows as Ryos `00`, disabled
- the Fn side of the Max has a kind `0d` with codes 01 to 1a nobody has names for (kept)
- the palm rest of the Max has lights of its own (led numbers 124 to 163 in its table), they arent keys in the picture and arent offered yet. Nor is its 56 byte `16` report (`smart_key_rgb_data`)

## Pyro

`1e7d:314c`, pretend one `ROCKCAT_SIM=pyro`. Monitor class `pyrox_device_c` (vtable `0x8c8920`, it reads `06` 134, `07` 11, `0a` 157, `0c` 85, `11` 365 with the same 4 byte select), module `PYRO.dll` v1.0009. The module keeps its defaults under Swarms own names (`GameModeKeys`, `CapsLockKey`, `FunctionKeys`, `EasyZoneKeys`, `BacklightSettings`), five raw reports per value, the same reports as above: no M keys, Fn zone of 19 keys (F1 to Pause, the arrows), colours as r g b side by side like the Vulcan II.

Its `key_pos_mapping.txt` (Esc = 17) doesnt fit its own factory `06` at all. That `06` is the Vulcan Pro one with a few keys moved (a right Win at slot 15, Fn, Menu and right Ctrl seven slots earlier, some international keys), so rockcat takes the Vulcan Pro slots and moves a key to wherever the Pyros `06` has its HID usage. GUESS, but every key of the picture ended up on a slot that holds its own usage.

## Magma, Magma Mini

`1e7d:3124` and `1e7d:69a0`, pretend ones `ROCKCAT_SIM=magma`, `magma-mini`. Monitor `magma_device_c` (vtable `0x8c84f4`) and `magma_mini_device_c` (`0x8cf29c`), modules `MAGMA` v1.0009 and `MAGMA_MINI` v1.0000, defaults under Swarms own names like the Pyro. The membrane boards of the family:

- `06` is 150 bytes, `[06 96 profile 01]` + 144 slots. Their `key_pos_mapping.txt` counts 16 further than `06` does: Esc, F1, 1, Tab, A, Space, the arrows, Fn and right Ctrl all sit at their number - 16
- the light is 26 bytes: `[11 1a profile effect speed brightness theme ? ?]` (one byte size, so everything one byte earlier than on the Pro), then 5 zones: 5 red, 5 green, 5 blue. Which key belongs to which zone isnt in the module. GUESS: five columns of the same width over the picture, left to right. Painting one key paints its zone
- the Magma: Fn zone of 19 keys (157 bytes), Easy-Shift 20. The Mini: Fn zone of 36 keys (294 bytes, two byte size), Easy-Shift 14 (61 bytes), no F row

## Not done

- light from the pc (the PWM tables in the modules name a second interface, usage page `ff00` usage `01` on the II)
- macros (kind `07`), the M keys (`0b`), the caps report (`07`), shortcuts in an entry (`a` and `b`)
- Vulcan II Mini Air (`3aa9`, `6978`): wireless, a dongle with its own `pre_check_*` handshake in the monitor (`vulcan2_mini_air_base_c`, its sending functions dont decompile cleanly), and a key matrix of its own (the monitors `config/vulcan2_mini_air/key_pos_mapping.txt` starts `1=139 2=94`). Its module defaults have the Mini sizes except a 333 byte light
