# Suora, Suora FX

`1e7d:2fa8`. Code: `src/lib/families/suora.cpp`, factory keys in `suora_tables.inc`, the picture in `suora_keys.inc`. Pretend one: `ROCKCAT_SIM=suora`.

NOBODY HERE HAS ONE. Everything is out of roccat-tools 5.9.1 (`suora/libroccatsuora`, GPL), which knew this keyboard for years. The Swarm module (`SUORA/suora.dll`) only added that the index in its `kb_matrix.xml` is the key slot.

## Conversation

No feature reports at all. Everything goes as **output reports without a report id** to interface 2 (the vendor one), and comes back as input reports on the same interface. rockcat picks that node by its interface number (`device_desc::interface_number`), because it declares no report ids to find it by.

A command is 8 bytes: `[action payload(6) checksum]`. `action` is the request, with bit 7 set to ask for something. The checksum is the byte sum of the first 7 bytes, xor `8b`. To ask: drop whatever input is still queued, send, wait 100 ms, read the 8 byte answer (same shape, action `00` = refused).

| request | what |
|---|---|
| `00` | info: `[major minor ...]`, the firmware version |
| `02` | game mode: `[on ...]` |
| `08` | illumination: `[? effect ? brightness ? ?]` |
| `0d` | keys: a declaration `[01 00 packets 00 00 00]`, then 10 packets of 64 bytes |
| `11` | macros (6 of them, 2 packets each), not done |
| `12` | reset to factory (write only), not used |

Keys and macros: asking for `8d` with the declaration gets the declaration back with the packet count in byte 2, then that many 64 byte input reports. Writing sends `0d` with the declaration (packet count filled in), then the packets as 64 byte output reports.

## Keys

160 slots of 4 bytes, `[type modifier1 key modifier2]`. The slot number is the key number in rockcat (Swarms `kb_matrix.xml` numbers the keys that way). One layer, no Easy-Shift, no profiles: rockcat shows one profile.

| type | what | rockcat shows it as |
|---|---|---|
| `00` | a key, HID usage in `key`, up to two modifier keys (`e0` to `e3`) in the modifier bytes | the key, or a shortcut |
| `02` | system: 1 shutdown, 2 sleep, 4 wake | Ryos `81 82 83` |
| `03` | consumer usage, low byte in `key`, high byte in `modifier2` | the Ryos media / app / www names |
| `0d` | macro, index in `key` | kept |
| `10` | "the key itself" for the F keys, Print Screen, Scroll Lock, Pause, the six above the arrows, two arrows, right Alt and right Ctrl, by number (`[10 01 n 00]`). `[10 00 00 00]` is Fn | that key / fn |
| `05` | unknown (Vol+ has it) | kept |

Slots that come as type `10` after a reset dont take anything else (roccat-tools: "does not support NORMAL"), except right Alt and right Ctrl, which Swarm writes as plain keys. rockcat lets those two change and shows the others without letting them change. Anything rockcat has no name for shows as `kept` and goes back unchanged.

## Light

`[? effect ? brightness ? ?]`: effect 0 off, 1 fully lit, 2 breathing. Brightness 0 to 10, and `0b` while breathing. One backlight for the whole board, the gui shows the picture lit.

## Not done

Macros, game mode (it can be read, rockcat only uses that to see the keyboard answers), reset, the key events.

## Suora FX

`1e7d:3246`, pretend one `ROCKCAT_SIM=suora-fx`, same code (`MODELS[]` in `suora.cpp`). NOBODY HERE HAS ONE, and nothing in roccat-tools: this is out of `ROCCAT_Swarm_Monitor.exe` (`suora_fx_device_c`, vtable `0x8ccd88`, reading in `FUN_004e2d10`, writing in `FUN_004e1fc0`, `FUN_004df060` keys, `FUN_004dfe60` macros) and the keyboard firmware that ships in the module (`SUORA_FX/firmware/Suora_fx_V3.00.bin`, plain ARM, loads at 0, the command handler is `FUN_00005344` and around it).

The same conversation as the Suora (8 byte commands on interface 2, answers as input reports, bulk data as 64 byte packets), with these differences:

- the checksum is `0xff - sum` (byte sum xor `ff`, the Suora xors with `8b`). The firmware drops a command whose checksum is wrong
- no profiles in the firmware: Swarm kept five on the pc and wrote one. rockcat shows one
- keys: 126 slots in 8 packets (504 bytes and 8 spare), the slot number is the index in Swarms `kb_matrix.xml` (0 to 125, Esc is 5). Type `00` is a key (and its modifier keys), `03` a consumer usage (read like on the Suora, GUESS), `04` a macro (macros are `11`, 3 packets each, up to 35 of them). Type `01` goes through a table Swarm loads from its config, rockcat keeps it. Nobody knows the factory keys, so there is no "reset keys"
- the light is per key rgb: Swarm sends `08 01 ...` (stop), the colours as request `12` with 8 packets, `04`, the keys, the macros and `08 02 ...` (apply with effect, speed, brightness). What the bytes of `08` are and how the 512 colour bytes are laid out isnt worked out, Swarm never reads the colours back. rockcat leaves the light alone
- the keyboard can change effect (1 to 10), speed (steps of 5 up to 50) and brightness (0 to 10) by itself with Fn, the firmware has that in one place (`FUN_00003430`)
- no info request found, the firmware version is bcdDevice
