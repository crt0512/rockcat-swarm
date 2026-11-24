# Sova

Membrane `1e7d:2f76` and mechanical (MK) `1e7d:2f94`. Layouts from roccat-tools 5.9.1 (`sova/libroccatsova/`), everything below was read, written back and switched on a real membrane one with firmware 1.27. The MK was never here. Code: `src/lib/families/sova.cpp`, pretend ones `sova-membrane` and `sova-mk`.

It talks like a Ryos (see [ryos.md](ryos.md)): select on `04`, read, poll `04` after a write, same event report on `03`. Interface 1 is the one with the reports (three hidraw nodes, the other two declare nothing). Ready right after a select like the Ryos boards, so 2 ms wait and not 150: a full backup takes 0.7 s instead of 5.

## Reports

| id | bytes | select | what |
|---|---|---|---|
| `05` | 4 | | `[05 04 active count]`, count says 1 on the real one. switching writes it back with the new active profile |
| `06` | 149 | `a0` | primary: a byte per slot, 144 slots |
| `07` | 95 | `a1` | F1 to F12, Print, Insert, Del: 3 bytes each, 15 plain then 15 with Fn held |
| `0a` | 13 | `a4` | byte 3 Caps Lock, the rest unknown and passed through |
| `0b` | 294 | `a5` | two byte size. 40 keys with Easy-Shift held, 3 bytes each, then 168 bytes of what looks like leftovers of a function report |
| `0c` | 6 | `b0` | game mode key mask |
| `0d` | 160 | `b1` | light |
| `0e` | 2002 | profile, slot | one macro, `[0e size16 profile slot loop setname[40] name[32] count16 keys[480 x 4] sum]` |
| `0f` | 8 | | info, byte 2 firmware (`7f` = 1.27) |

## Keys

Input numbers are Swarms, from its `kb_matrix.xml`. For 0 to 143 they are the slot in `06`, checked on the real one: every slot holds the usage of the key Swarm puts there. roccat-tools has Z, U and Y in the wrong slots (`sova_key_relations.c`), the type of each key (plain, Easy-Shift capable, function, Caps Lock) is still taken from there by key order. Caps Lock is input 225 and lives in `0a`, Fn is 241 and cant be anything else, Esc neither (roccat-tools says so, its slot does have `29` in it).

Layers like the Ryos: 0 normal, 1 Easy-Shift (only the 40 keys of `0b`, each can hold a macro, the macro slot is its place in `0b`), 2 Fn (the second half of `07`).

Factory reports are roccat-tools defaults, they match the real keyboard wherever nobody changed anything.

## Light

Membrane: one backlight for the whole board. `0d`: byte 4 effect (0 off, 1 fully lit, 2 blinking, 3 breathing, 4 heartbeat, 5 impulse, 6 heatmap), 5 speed 0 to 2 (rockcat says 1 to 3), 11 brightness 0 to 5, 13 minutes until it sleeps. Bytes 6 to 9 are what it does when switched on, going to sleep, waking up and on a profile switch, passed through, nothing in the api for them. No light from the pc.

The MK lights every key on its own (says somebody who knows the thing), roccat-tools has no report for that. Swarms module for it is in the archive (`SOVA_MECHANICAL`), thats where to look. Until then it is a membrane with another name, and it gets the membrane key table: Swarm numbers the MK keys differently (probably its led numbers), which would point key assignment at the wrong slots.

## Not tried on the real one

- writing a macro (the pretend one round trips it, the layout is the same as the Ryos one in one piece)
- the Fn layer and Easy-Shift writes (the reports came back the same after writing them back unchanged)
