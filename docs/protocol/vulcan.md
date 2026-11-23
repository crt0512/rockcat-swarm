# Vulcan 100 / 120 AIMO

Vulcan 100 AIMO (`1e7d:307a`) and Vulcan 120/121/122 AIMO (`1e7d:3098`, the three only differ in the palm rest). Code: `src/lib/families/vulcan.cpp`, tables in `vulcan_tables.inc` and `vulcan_keys.inc`, the pretend one in `src/lib/sim_vulcan.h` (`ROCKCAT_SIM=vulcan-120-aimo`).

NOBODY HERE HAS ONE. roccat-tools never heard of it either. Everything below comes out of three places and none of it was ever sent to a real keyboard:

- the keyboard firmware that ships in the Swarm module, `VULCAN_AIMO/firmware/VULCAN_AIMO_v1.36.bin` (plain ARM Cortex-M image, loads at `0x2000`, the first 8 KiB are the bootloader). Ghidra: `analyzeHeadless ... -loader BinaryLoader -processor ARM:LE:32:Cortex -loader-baseAddr 0x2000 -postScript FwFix.java`. The switch tables (`__ARM_common_switch8`) dont decompile, they had to be read off the disassembly
- `ROCCAT_Swarm_Monitor.exe` from Swarm 1.9477 (class `vulcan_aimo_device_c` / `vulcan_base_device_c`, all the usb traffic) and its Qt resources under `config/vulcan/` (`VULCAN_AIMO` is a default profile as a QSettings file, every value is a list of five raw reports, one per profile)
- the module `VULCAN_AIMO/VULCAN.dll` v1.0029 (the gui half) and its Qt resources under `vulcan_aimo_config/` (`key_pos_mapping.txt`, `rgb_led_mapping.ini`, `key_data_mapping.txt`, `default.dat`)

`tools/re/qrcfind.py` only finds resource collections with png files in them, the `config/` ones have none. A copy that chains the data offsets instead of looking for png signatures got them out (not in the repo, the trick is: file payloads sit back to back as `[u32 length][bytes]`, so the sorted data offsets of a tree have to chain up).

It is the Ryos generation of the protocol (feature reports, select on `04`, status poll on `04`, 16 bit byte sum at the end of every report, see [README.md](README.md)), with one big difference:

## One setup, not five

The firmware keeps exactly one copy of every report, each in its own 4 KiB page of an external spi flash (`0x75000` light, `0x78000` keys, `0x79000` F keys, `0x7a000` Caps/Fn, `0x7b000` Easy-Shift, `0x71000` report `09`, `0x7d000` the profile record). There are no per profile addresses anywhere. The profile byte in every report is just carried along. Swarm had its five profiles on the pc and wrote the whole set whenever the profile changed.

So rockcat shows this keyboard with ONE profile. `profile_count` is 1, the active profile is always 0, and the number the keyboard has in report `05` (`[05 04 active count]`, Swarms default has `05 04 00 04`) is ignored. Switching profiles on the pc side is a gui feature for later (keep five setups in files, write one).

## Reports

| id | bytes | select | what |
|---|---|---|---|
| `04` | 3 | | `[04 profile request]` selects what the next read returns. Reading it gives the status like on every other device |
| `05` | 4 | | `[05 04 active count]`. Writing it makes the firmware reload everything from flash (`FUN_0000a94c`) |
| `06` | 133 | `a0` | keys: `[06 85 profile]`, then one byte per firmware matrix slot (128), checksum |
| `07` | 95 | `a1` | F1 to Pause, 15 x 3 bytes, then the same 15 with Fn held |
| `09` | 43 | `a3` | `[09 2b profile]` + 12 x 3 bytes + 2 unknown + checksum. Swarm calls the matching things "extend keys" (Ins, Home, PgUp, Del, End, PgDn, FX, volume). Read and written back untouched, not understood |
| `0a` | 8 | `a4` | `[0a 08 profile caps fn]`: what Caps Lock and Fn are, one byte each |
| `0b` | 65 | `a5` | Easy-Shift: `[0b 41 profile]` + 20 keys x 3 bytes + checksum. See below about 294 |
| `0d` | 443 | `b1` | lights, see below |
| `13` | 8 | | light control, Swarm sends it when its own AIMO engine takes over or lets go, see the last part. Not used |

The select values are the ones the firmware's SET_REPORT handler accepts for `04` (`FUN_0000acfc`, case `04`: `a0 a1 a3 a4 a5 b1` and `f0`). After a select it checks the checksum of what it has stored for that request and answers status `4` when the stored copy is broken (we treat that like ready). What a GET_REPORT returns was NOT traced in the firmware, that it hands out the stored report is a guess from how the Ryos does it. Swarm itself only ever reads `0d` back (`FUN_0056f450(0xb1, profile)` in the monitor), everything else it writes from its own copy.

The sizes are the ones the firmware checks on SET_REPORT (`FUN_0000acfc` calls the checksum check `FUN_00007380` with `0x85`, `0x5f`, `0x2b`, `8`, `0x41`, `0x1bb`, and copies that many bytes). `08` (Ryos: macro keys) and `0e` (Ryos: macros) have no handler at all, so this keyboard has no macros in hardware. Swarm did them from the pc.

Report `0b` is odd. The firmware takes 65 bytes (and its own factory default, `FUN_00008130`, builds `[0b 41 00]` + 20 entries). Swarms default profile has a 294 byte one instead, `[0b 26 01 profile]` + 96 entries like the Ryos, with only the first 20 used. Could be a newer firmware, could be Swarm sending more than needed. rockcat reads with room for 294 and uses whatever the keyboard answers with (byte 1 `41` = short, `26 01` = long), and writes it back in the same size.

## Keys

Keys are numbered the way Swarm numbers them in `kb_matrix.xml` (1 to 113, Esc = 1, F1 = 2, ... FX = 112, volume wheel = 113). That is the input number in rockcat and the led number too, same idea as the Ryos. The key tables (`vulcan_keys.inc`) are generated from `DEV/Roccat-Swarm/Assets/Vulcan_120_121_122_AIMO/keyboard/base/kb_matrix.xml` with `tools/assets/kbmatrix.py` (US and DE regions, picture space 680x370). The Vulcan 100 has the same xml byte for byte.

Where the bytes of a key are:

| key | normal | Easy-Shift | Fn |
|---|---|---|---|
| most keys | 1 byte in `06`, slot from `key_pos_mapping.txt` | | |
| F1 to Pause (2 to 16) | 3 bytes in `07`, first half | | 3 bytes in `07`, second half |
| Caps Lock ("Left+", 62) | byte 3 of `0a` | | |
| Fn (104) | byte 4 of `0a` | | |
| 1 to 5, Q to T, A to G, Z to B (US names) | 1 byte in `06` | 3 bytes in `0b`, in that order | |
| FX, volume (112, 113) | nothing we understand | | |

The slot table (`KEY_SLOT`) is the module's `vulcan_aimo_config/key_pos_mapping.txt` ("left value is default key pos, right value is firmware key mapping array index"). Checked against Swarms default `06`: every key lands on its own HID usage (Esc on `29`, A on `04`, numpad 0 on `62`, ...). The monitor has its own `config/vulcan/key_pos_mapping.txt` with a different numbering (it has room for five media keys after Pause), dont mix them up.

The Easy-Shift keys and their order are `[EASY_KEY]` in the module's `key_data_mapping.txt`, the F keys `[F_KEY]` (items 0 to 14 normal, 15 to 29 with Fn).

The bytes mean what they mean on the Ryos as far as anyone can tell: below `0x73` a HID usage, `f1` Fn, `ff` Easy-Shift, `b3`..`b6` on Fn + F1..F4 (Ryos: profile 1..4), `c0`..`c3` and `cc`..`cf` media and app keys, `b8` shortcut. rockcat names them with the Ryos table. Two things that dont fit: the left Windows key is `00` in Swarms default (`00` is "disabled" on a Ryos, here it is probably "do what the key does", GUESS), and Fn + Scroll Lock is `fc`, which nobody has a name for.

Factory layout (`reset-buttons`) is Swarms default profile, `FACTORY_*` in `vulcan_tables.inc`. Out of the box Easy-Shift does nothing (every one of the 20 keys is itself).

## Lights, report `0d`

| offset | bytes | what |
|---|---|---|
| 0 | 3 | `0d bb 01` (the size is two bytes, `0x1bb`) |
| 3 | 1 | profile |
| 4 | 1 | effect |
| 5 | 1 | speed. The firmware drops the whole report when this is 0 or above 11 |
| 6 | 1 | unknown, 5 in both defaults (brightness 0-5 like the Ryos? GUESS, kept as it is) |
| 7 | 1 | brightness step, 1 to 69, the firmware drops the report for 0 or anything above 69. Swarm maps it through a table in the monitor (`0x827780`: 0, 0, 48, 51, ... 250, 255) |
| 8 | 1 | AIMO colour theme, bit 7 a flag (Swarm `0x80`, firmware default 0), low bits the theme (`FUN_005719f0` in the monitor hands both to its AIMO engine) |
| 9 | 432 | colours, 12 blocks of 36 bytes: 12 red, 12 green, 12 blue |
| 441 | 2 | checksum |

Which led slot a key has is `rgb_led_mapping.ini` from the module: per key the byte position of red, green and blue in the 436 byte frame of the direct light stream (below), which is `4 + 36 * (slot / 12) + slot % 12` and +12, +24 for green and blue. Every one of the 111 keys fits that formula, so the 9 byte header of `0d` and the 4 byte header of the stream sit in front of the same layout. Swarms default profile is a rainbow from left to right and comes out as one when decoded this way, which is the only check this got.

Effects: the monitor treats 1 as "fully lit" and 9 as "AIMO, drawn by the pc" (`FUN_00571b70`), the module stores 9 when you pick AIMO. utility.dll's list of effects (the function around `0x65e675b2`, it is shared with the mice) has the values 0, 1, 2, 3, 4, 6, 7, 8, 10, 11, 0x11 and 0x12 next to pictures for AIMO, Colorwave, Colour cycle, Snake, Fully lit, Heartbeat, Breathing, Blinking, Fade FX and Ripple FX, but which number belongs to which picture was not untangled. rockcat offers 1 to 4 (fully lit, blinking, breathing, heartbeat, the numbers every Swarm device has) and shows any other byte as it is.

`set_led` with "off" writes black, there is no per key on/off bit. There is one brightness for the board, so the alpha of a colour goes into the colour.

## Lights from the pc (NOT implemented)

Swarm streams frames to a second interface, the one whose top collection is usage page 1 / usage 0 (`FUN_0042b900(0x1e7d, pid, 0, 1, -1)` in the monitor, `hid_open` matches usage page and usage). A frame is 436 bytes: `a1 01 01 b4` and the 432 colour bytes laid out like in `0d`, sent as 7 output reports of 64 bytes with report id 0 (`hid_write(dev, buf, 0x41)`, 1 ms apart, the last one padded with zeros). The firmware side is `FUN_00003698` (collects packets that start with `a1 01 01 b4` until it has `0x1b4` bytes). Around that Swarm sends report `13` on the control interface, `[13 08 a profile b level 00 00]`, with `a` and `b` flipping between 0 and 1 depending on who owns the leds (`FUN_0056f2b0`, `FUN_00571b70`), `level` is its brightness as a step of the table above. Which combination means what was not worked out.

rockcats transport can only do feature reports on one hidraw node, so this needs a transport that can write output reports to a second node first.

## Events

Not worked out. The firmware has an Easy-Shift callback into the monitor (`vulcan_base_device_c::on_recviving_easyshift`), what goes over the wire for it was not looked at.

## Firmware version

There is no info report we know of. rockcat shows bcdDevice, that it is the firmware version is a GUESS.

## Not done, and what is known about the neighbours

- Vulcan TKL AIMO (`2fee`): same module family, but its default has `fn_key_data` (147 bytes) and `custom_key_data` (47) where the big one has `f_key_data`. Needs its own key places
- Vulcan 80 (`305c`): same key reports as the 100/120, but `backlight_setting` is 156 bytes (single colour board). Its module is `VULCAN_80/vulcan_80.dll`, resources under `vulcan_config/`
- Vulcan Pro / TKL Pro / II / II Max / II Mini: a different report set altogether (`backlit_default_data`, `es_zone_default_data`, `game_mode_default_data`, `caps_default_data`, the monitor calls the class `core_kb_base_device_c` for some of them). See [swarm-ident.md](swarm-ident.md)
