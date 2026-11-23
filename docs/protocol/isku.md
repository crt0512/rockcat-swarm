# Isku, Isku FX

`1e7d:319c` and `1e7d:3264`, the keyboards from before the Ryos. Out of roccat-tools 5.9.1 (`isku/libroccatisku/`, `iskufx/libroccatiskufx/`), never on hardware. Code: `src/lib/families/isku.cpp`, pretend ones `isku` and `isku-fx`.

Same conversation as the Ryos: `[04 profile request]` select, read, poll `04` after a write. There is no report with every key in it, only the keys around the edges can be set, each group in a report of its own. Those reports keep their keys column wise: all type bytes, then all modifier bytes, then all key bytes, then the checksum.

| id | select | bytes | what |
|---|---|---|---|
| `07` | `50` | 6 | game mode key mask |
| `08` | `60` | 41 | F1 to F12 |
| `10` | `70` | 10 / 16 (FX) | light |
| `13` | `90` | 6 | Caps Lock, one type byte |
| `0d` | `a0` | 35 | M1 to M5, then the same with Easy-Shift |
| `09` | `b0` | 65 | the 20 keys of the Easy-Shift zone (1 to 5, Q to T, A to G, Z to B), with Easy-Shift held |
| `0a` | `c0` | 29 | 8 media keys |
| `0b` | `d0` | 23 | T1 to T3, then the same with Easy-Shift |
| `0e` | profile, key index | 2083 | a macro: `[0e size16 profile index type loop talk[24] set[24] name[24] count16 keys[500 x 4] sum]` |
| `0f` | | 6 | firmware in byte 2 |
| `05` | | 3 | `[05 03 profile]` |

Inputs are roccat-tools key indices: M1 to M5 `00` to `04`, the Easy-Shift zone `0a` to `1d`, T1 to T3 `1e` to `20`, F1 to F12 `24` to `2f`, media `30` to `37`, Caps Lock `38`. The second halves of M and T keys have their own index (shift M1 is `05`, shift T1 is `21`), that is what their macros are filed under. Macros only below F1. The key table (`src/lib/keyboards/isku.inc`) is the Isku+ one out of Swarm with these numbers put in, the media keys are not on that picture.

Out of the box every key is type `ef` ("does what is printed on it", roccat-tools calls it standard) and Caps Lock is Easy-Shift (`ff`).

Light, Isku: `[10 0a profile ? ? brightness dim minutes sum]`, brightness and dim 0 to 5. Isku FX: `[10 10 profile on breathing colourtype index r g b brightness dim minutes talkfx sum]`, colour type 0 palette, 1 own colour, 3 colour flow.

Not done: events on `03`, talk fx (`17` on the FX), the light from the pc.
