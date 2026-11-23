# Nyth, Tyon, Kiro, Savu, Kone EMP, Kone Pure Owl-Eye, Leadr, Kova AIMO

The mice that talk like the Kova 2016 ([kova.md](kova.md)) with reports of their own sizes. One table per mouse in `src/lib/families/classic.cpp` (`NYTH`, `TYON`, `KIRO`, `SAVU`), one profile class for all of them. Everything here is out of roccat-tools 5.9.1 (`nyth/`, `tyon/`, `kiro/`, `savu/`), none of these mice was ever here. Pretend ones: `nyth`, `tyon`, `kiro`, `savu`, `kone-emp`, `kone-pure-owl-eye`, `leadr`, `kova-aimo`.

The conversation is the Kova one: `[04 profile request]` select, read the report, after a write poll `04` until it says ready. Settings on `06` (select `80`), buttons on `07` (select `90`), 3 bytes per button, normal layer then Easy-Shift. Macros on `08`, firmware in byte 2 of `09`.

| | Nyth `2e7c` / `2e7d` | Tyon `2e4a` / `2e4b` | Kiro `2c38` | Savu `2d5a` |
|---|---|---|---|---|
| profiles | 5, `[05 04 index count]` | 5, `[05 03 index]` | 1 | 5, `[05 03 index]` |
| settings | 33, checksum | 30, checksum | 65 with the buttons in it, no select, checksum at 50, 13 spare bytes after | 16, checksum |
| buttons | 123, no checksum, 20 buttons | 99, no checksum, 16 buttons | in the settings at 20, 10 buttons, one layer | 47, checksum, 7 buttons |
| dpi | 5 levels, 16 bit, 200 to 12000 in 10s | 5 levels, a byte x 50, 200 to 8200 | 5 levels, a byte x 50, 250 to 4000 | 4 fixed: 400 800 1600 4000, byte 7 says which |
| lights | 2, index r g b, on bits 0 1, own colour bits 4 5 | 2 (wheel, bottom), index r g b spare, own colour bit 4 for both | 1, own colour bit 2 | 1, index `10` = own colour |
| effect | byte 21: 0 off 1 lit 2 blink 3 breathe 4 heartbeat | byte 16, same numbers | byte 13, 1 to 3 | bit 1 of byte 9 = breathing, bit 0 = on |
| colour flow | byte 20 (0 off, 1 to 3 how) | byte 15, same | byte 12 | bit 2 of byte 9 |
| macros | two halves of 1024 | one report of 2002, like the Sova | none | its own 2083 byte layout, not done |
| extra | button set preset `0a` (select `a0`, 30 bytes) carried along | | left handed bit 6 of byte 10 | |

Polling is the shared `0 125, 1 250, 2 500, 3 1000` everywhere, on the Tyon and Kiro in the low half of the byte (the Tyon keeps its talk fx switch in the other half, the Kiro its side button switches).

Factory settings: the Nyth and Kiro ones are roccat-tools default arrays. Tyon and Savu have only default rmp files, their settings are put together from those values (that only matters for the pretend ones) and their factory buttons are the type bytes out of the rmp.

## Unsure

- the Nyth numbers its buttons in roccat-tools order here. Swarms pictures put marker 4 under the wheel, 5 in front of it and 6 at the front left, so its order is another one and the markers on the mouse view probably point at the wrong buttons. the module dll knows
- light from the pc (talk fx `10` on Nyth and Tyon, `10` with 13 bytes on the Kiro) is not done
- sensitivity bytes (x, y, advanced) survive but nothing can set them yet
- the Savu macros

## Kone EMP

`1e7d:2e24`, table `EMP`. NOT in roccat-tools: this one comes out of ROCCATs Swarm. The monitor (`ROCCAT_Swarm_Monitor.exe` 1.9477, class `kone_emp_device_c`, vtable `0x8cc51c`) keeps per profile a settings report of `0x29` bytes and a button report of `0x47`, writes settings with their checksum at 39 (`FUN_004f8880`), reads macros for every button slot whose type is `30` (`FUN_004f9050`, 22 slots, halves of 1024 on `08` through the shared `FUN_004b0f40`), and drives the lights from the pc with a 16 byte `10` (`FUN_004f8240`, not done here). The module (`kone_emp.dll`) has the reset functions that name every byte: `FUN_6c119080` (dpi, sensitivity) and `FUN_6c123250` (lights), plus setters (`FUN_6c12e110` `15`, `FUN_6c12e290` `16`, `FUN_6c12e410` `14`, `FUN_6c12df70` custom colour: index `1e` then r g b).

| byte | what | factory |
|---|---|---|
| 3 | advanced sensitivity | 0 |
| 4, 5 | sensitivity x, y | 6, 6 |
| 6 | dpi levels on | `1f` |
| 7 to 11 | dpi x, (v + 1) x 100 | 400 800 1200 1600 3200 |
| 12 to 16 | dpi y, same | |
| 17 | active level | 1 |
| 18 | polling | 3 |
| 19 | lights on, bit per light | `0f` |
| 20 | colour flow (GUESS from the reset values) | 1 |
| 21 | effect (GUESS, Nyth numbers assumed) | 3 |
| 22 | speed | 2 |
| 23 to 38 | 4 lights, index r g b, index `1e` = our rgb | |
| 39 | checksum | |

Buttons: 11 on two layers, numbered like the markers Swarm puts on its picture (left, right, wheel, side front, side back, tilt left, tilt right, wheel up, wheel down, two behind the wheel). That the report order is the marker order is a GUESS. Nobody knows the factory buttons, so "reset buttons" says no.

## Kone Pure Owl-Eye, Leadr, Kova AIMO

Same way as the EMP: the size of every report out of the monitor class, the meaning of every byte out of the reset functions and setters of the module. None of the three ever seen for real, none in roccat-tools.

Kone Pure Owl-Eye `1e7d:2dcd`, table `OWL`. Monitor `kone_pure_owl_eye_device_c`: settings `0x23` (35), buttons `0x3b` (59 = 9 buttons on two layers, checksum), macros in halves. Module `kone_pure_owl-eye.dll`: `FUN_6b798ef0` (bytes 3 to 17 exactly like the EMP), `FUN_6b7a30c0` (lights), `FUN_6b7ad610` (colour: rgb at 26 to 28 and bit `4 + led` of byte 21).

| byte | what |
|---|---|
| 3 to 17 | like the EMP: advanced sensitivity, x and y, dpi mask, dpi x, dpi y ((v + 1) x 100), active level |
| 18 | polling |
| 21 | bit 0 light on, bit 4 our rgb |
| 22 | colour flow |
| 23 | effect |
| 24 | speed |
| 25 to 28 | the light, index r g b |
| 33 | checksum |

Leadr `1e7d:2e4c` and `2e4e`, table `LEADR`. Said to be a Tyon without the cable, and the buttons agree: monitor `leadr_device_c` keeps 99 bytes per profile (`FUN_004f61d0` walks 32 macro slots in it), the Tyon size. The settings are the Owl-Eye layout with two lights (25 to 28 and 29 to 32, bits 0 / 1 and 4 / 5 of byte 21, factory `03`), checksum at 33 (`FUN_004f6610`). Module `leadr.dll`: `FUN_6b319160`, `FUN_6b323510`, `FUN_6b32de60`. Which of the two ids is the dongle is not known, wireless only things (battery, sleep) are not done.

Kova AIMO `1e7d:2cf1`, `2cf2`, `2cf3`, table `KOVA_AIMO`. The Kova 2016 report grown to 37 bytes: monitor `kova_aimo_device_c` (settings `0x25` with checksum at 35, buttons `0x4b` like the Kova 2016, halves macros), module `KOVA_AIMO` (`FUN_65f12c70` dpi, `FUN_65f4a440` lights, `FUN_65f4eed0` / `FUN_65f4edd0` effect and speed).

| byte | what | factory |
|---|---|---|
| 6 | dpi mask | `1f` |
| 7 to 11 | dpi, x 50 | 400 800 1600 3500 7000 |
| 12 | active level | 1 |
| 13 | polling | |
| 14 | lights on bits 0 1, our rgb bits 4 5 | `33` |
| 15 | colour flow | 0 |
| 16 | effect | 9 (AIMO) |
| 17 | speed | 2 |
| 18 to 25 | 2 lights, brightness r g b | |
| 26 to 34 | unknown, kept | |
| 35 | checksum | |

Its buttons use the Kova 2016 names and the Kova 2016 factory layout (same shell, GUESS). Effect 9 (AIMO) shows as "fully lit", the AIMO colours of this mouse are not done.
