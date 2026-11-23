# Isku+

`1e7d:31e2`. Code: `src/lib/families/isku_plus.cpp`, factory reports in `isku_plus_tables.inc`, pretend one `isku-plus`. NOBODY HERE HAS ONE and roccat-tools doesnt know it. Everything is out of ROCCATs Swarm:

- `ROCCAT_Swarm_Monitor.exe` 1.9477, class `isku_plus_device_c` (vtable at `0x8cd630`, found with `tools/re/rtti.py`). `FUN_004c05b0` reads a whole profile: selects `a1`, `a4`, `a3`, `a5`, `a2`, `a0` and the light on `b1`, each `[04 profile request]` then a read. The sizes come from the reads (`FUN_0050b100` 95, `FUN_0050b300` 8, `FUN_0050c310` 23, `FUN_0050c180` 35, `FUN_004ea6b0` 149, `FUN_004eaac0` 65, `FUN_004bfdc0` 18). The Easy-Shift read is shared: it always reads 65 bytes and puts them into a 294 byte buffer of its own, with report id `09` for one product type (the Skeltr) and `0b` for every other one. The module default holds that 294 byte buffer, NOT what goes over the wire (a first version here sent the 294 bytes) `FUN_004ea6b0` is the same primary read the Skeltr uses (with select `40` there)
- the default profile inside the module `isku+.dll` (a QSettings file as plain text in the dll, every value a 4 byte length and the report). That gives the report ids and the key lists: `[F_KEY]`, `[M_KEY]`, `[T_KEY]` and `[EASY_KEY]`, the order of the Easy-Shift report (`[KEYS]` is the order of Swarms 294 byte buffer). Copy in `DEV/re-notes/isku_plus_default.ini` (not in git)

| id | select | bytes | what |
|---|---|---|---|
| `06` | `a0` | 149 | primary, a byte per slot, 144 slots |
| `07` | `a1` | 95 | F1 to Pause, 15 plain then 15 with Fn |
| `08` | `a2` | 35 | M1 to M5, then with Easy-Shift |
| `09` | `a3` | 23 | T1 to T3, then with Easy-Shift |
| `0a` | `a4` | 8 | Caps Lock, Fn |
| `0b` | `a5` | 65 | the 20 keys of the Easy-Shift zone (1-5, Q-T, A-G, Z-B), 3 bytes each |
| ? | `b1` | 18 | light. id not known, NOT done |
| `0e` | profile, slot | 2002 | macros, `0x7d2` shows up in the class. layout assumed to be the Ryos / Sova one |

Inputs are Swarms key numbers, which are the primary slots (every slot of the module default holds the usage of the key Swarm puts there). The same numbers as the Skeltr. T1 to T3 are 255 to 257, Caps Lock 258, Fn 2.

GUESSES on top: the profile report is the Ryos one (`[05 03 xy]`), macros are numbered like on the Ryos (M keys 0 to 9, thumb keys 10 to 15, the Easy-Shift side of the 20 zone keys from 16 on), firmware in byte 2 of `0f`.
