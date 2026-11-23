# Kone[+] and friends

The Kone line between the original Kone and the Kone AIMO, all talking the same way:

| mouse | usb id | sim | buttons | lights |
|---|---|---|---|---|
| Kone[+] | `1e7d:2d51` | `kone-plus` | 12 | 4 |
| Kone XTD | `1e7d:2e22` | `kone-xtd` | 12 | 4 |
| Kone XTD Optical | `1e7d:2e23` | `kone-xtd-optical` | 12 | 4 |
| Kone Pure | `1e7d:2dbe` | `kone-pure` | 9 | 1 |
| Kone Pure Optical | `1e7d:2db4` | `kone-pure-optical` | 9 | 1 |
| Kone Pure Military | `1e7d:2dbf` | `kone-pure-military` | 9 | 1 |
| Kone Pure Optical Black | `1e7d:2dc2` | (uses the Military one) | 9 | 1 |

EVERYTHING in here comes out of roccat-tools 5.9.1 (`koneplus/`, `konextd/`, `konextdoptical/`, `konepure/`, `konepuremilitary/`, `konepureoptical/`). Nobody here has ever held one of these mice, none of it was seen on a wire, the pretend mice (`ROCKCAT_SIM=kone-plus` and so on) only do what this page says. The code is `src/lib/families/kone_plus.cpp`, the pretend mice live in `kone_plus_sim.inc` next to it.

The general way of talking is the one from [README.md](README.md): feature reports, `[04 profile what]` selects, status on `04` after every write (roccat-tools waits 110 ms first and then polls). roccat-tools talks to interface 0 (the mouse one), the node that declares report `06` is the one to use.

## Reports

| id | bytes | what |
|---|---|---|
| `01` | | plain mouse report |
| `03` | 5 (8) | events, see below |
| `04` | 3 | control: `[04 profile 80]` selects settings, `[04 profile 90]` buttons, `[04 profile button]` the macro of that button. Reading it gives the status, 0 is "critical" in roccat-tools |
| `05` | 3 | `[05 03 profile]` the active profile, write it to switch. 3 bytes, the Kone AIMO has 4 |
| `06` | 43 / 46 / 31 | settings of the selected profile, layouts below |
| `07` | 77 / 59 | buttons of the selected profile |
| `08` | 2082 | one macro, in one piece |
| `09` | 6 | info: `[09 06 firmware dfu ? ?]`, firmware 100 = "1.00". writing `0b` as firmware resets the mouse (not used here) |
| `0a` | 8 | unknown |
| `0c` | 4 / 1028 | sensor registers and the 30x30 sensor picture for the TCU calibration (not done) |
| `0d` | | SROM write (XTD and Pure only, firmware stuff, not touched) |
| `0e` | 3 | `[0e 03 state]` 1 = the driver handles the multimedia buttons, 0 = the mouse sends them itself |
| `0f` | 6 | `[0f 06 dcu tcu median action]` distance and tracking control unit (not done) |
| `10` | 16 | Talk FX, the lights from the pc |
| `1b` `1c` | | firmware update, nope |

## Settings, report `06`

Byte 2 is always the profile (0-4), then the layouts go apart. All of them: sensitivity is 1-11 with 6 in the middle, xy sync 1 = x and y sensitivity separate. dpi levels enabled is bit n = level n. Polling 0-3 = 125 / 250 / 500 / 1000 Hz. Effect speed 1-3. A light is 4 bytes: palette index, r, g, b, index `ff` = use the rgb. Colour flow 0 off, 1 all lights at once, 2 up, 3 down, 4 left, 5 right.

Kone[+] and Kone XTD, 43 bytes:

| offset | what |
|---|---|
| 0 | `06 2b profile` |
| 3 | xy sync |
| 4, 5 | sensitivity x, y |
| 6 | dpi levels enabled |
| 7 | 5 dpi levels x |
| 12 | active dpi level |
| 13 | 5 dpi levels y. roccat-tools: "can be set differently, reported in events, but not used" |
| 18 | Talk FX allowed, XTD only |
| 19 | polling |
| 20 | lights on, bit n = light n |
| 21 | effect mode, "does nothing" says roccat-tools |
| 22 | colour flow |
| 23 | effect: 0 off, 1 fully lit, 2 blinking, 3 breathing, 4 heartbeat |
| 24 | effect speed |
| 25 | 4 lights: top left, bottom left, top right, bottom right |
| 41 | checksum |

The dpi byte is dpi / 100 on the Kone[+] (100-6000). On the XTD it is dpi / 50, and the vendor tool only ever offered 200-8200 in steps of 200.

The Kone[+] only ever got colours out of its table of 33 from roccat-tools (index + that entrys rgb), so rockcat picks the closest table colour there and never writes index `ff`. Whether a Kone[+] would take `ff` is anybodys guess. The XTDs look at the index of the first light to decide table or custom for all four, so rockcat sets all four to `ff` once you set a colour.

Kone XTD Optical, 46 bytes. Same as above up to 25 with three changes, then three more bytes:

| offset | what |
|---|---|
| 7 | 5 dpi levels, called y in roccat-tools |
| 13 | 5 dpi levels, called x. roccat-tools writes the same into both, x and y here |
| 18 | "advanced": bit 0 angle snap on, bit 1 sensor alignment on |
| 41 | the same advanced byte again, roccat-tools writes both and reads this one |
| 42 | angle snap strength, 1-6 |
| 43 | sensor alignment, stored as degrees + 30 (0-60). the struct in roccat-tools says signed +-30 but every value it writes there is the +30 one |
| 44 | checksum |

Dpi is dpi / 50, 100-6400 in steps of 100.

Kone Pure, 31 bytes:

| offset | what |
|---|---|
| 0 | `06 1f profile` |
| 3-17 | like the Kone[+] |
| 18 | unknown |
| 19 | polling |
| 20 | bit 0 = light on (roccat-tools: "only recognized when effect type = breathing") |
| 21 | 0 = light colour out of the table, 1 = custom |
| 22 | colour flow |
| 23 | effect: 0 off, 1 fully lit, 2 breathing. rockcat calls the 2 breathing (3) |
| 24 | effect speed |
| 25 | the one light |
| 29 | checksum |

Dpi is dpi / 50 like the XTD, the vendor tool offered 200-8200 in steps of 200.

Kone Pure Optical, 31 bytes, the Pure with a different cpi block: 7-11 are five codes out of a table, 12-16 unused, 17 the active level. The codes: `10` 400, `00` 800, `11` 1200, `12` 1600, `13` 2000, `02` 3200, `03` 4000. Nothing else exists, rockcat refuses 2400.

Kone Pure Military and Kone Pure Optical Black, 31 bytes and NO checksum:

| offset | what |
|---|---|
| 3-17 | like the Kone[+], dpi / 50, 100-5000 in steps of 100, x and y separate |
| 18 | advanced, like on the XTD Optical |
| 19-24 | like the Pure |
| 25 | the light, but as index, red, BLUE, green. roccat-tools has a struct of its own for that, so its probably not a typo |
| 29 | angle snap strength 1-6 |
| 30 | sensor alignment, degrees + 30 |

## Buttons, report `07`

`[07 size profile]`, 3 bytes per button (function, parameter 1, parameter 2), checksum at the end. The Kova and the Kone AIMO have no checksum here, these do.

12 button mice (77 bytes): left, right, middle, thumb forward, thumb backward, wheel tilt left, wheel tilt right, wheel up, wheel down, plus, minus, top, then the same 12 for Easy-Shift[+].

Pures (59 bytes): left, right, middle, thumb forward, thumb backward, wheel up, wheel down, plus, minus, then the same 9 for Easy-Shift[+].

The function codes are the usual ROCCAT ones (`ROCKCAT_BT_*`), plus some only roccat-tools names: `17` / `18` / `19` sensitivity cycle / up / down, `42` Easy-Wheel sensitivity, `47` Easy-Wheel aero flip 3d. A shortcut is `05 modifiers key`. Factory layout (from the default rmp files): `01 02 03 07 41 09 0a 0d 0e 15 16 1a` and on the shift layer `08 07 25 06 00 21 22 26 27 11 12 1b`, the Pures the same with tilt and top taken out.

## Macros, report `08`

One report, 2082 bytes, selected with `[04 profile button]` where button is the index in the buttons report (0-23, on a Pure 0-17):

| offset | what |
|---|---|
| 0 | `08` |
| 1 | size, 16 bit, `22 08` |
| 3 | profile |
| 4 | button |
| 5 | loop count |
| 6 | 24 bytes unknown |
| 30 | 24 bytes macro set name |
| 54 | 24 bytes macro name |
| 78 | number of keystrokes, 16 bit |
| 80 | 500 keystrokes of 4 bytes: key, 1 press / 2 release, delay ms 16 bit |
| 2080 | checksum |

rockcat only has room for 480 keystrokes and doesnt keep the 24 unknown bytes when it writes a macro (roccat-tools writes zeros there too). A slot that never had a macro is read as "no macro" when the size bytes are not `22 08`, thats a guess about what an empty slot looks like.

## Events, report `03`

`[03 00 type data1 data2]`, some mice send 3 bytes of junk after that. Types: `10` tilt, `1b` scroll, `20` profile switched (data1 = profile), `60` quicklaunch, `80` / `90` timer start / stop, `a0` open driver, `b0` dpi switched (data1 = level + 1), `c0` sensitivity, `f0` multimedia, `ff` a button with Easy-Shift or a talk function, data1 = button + 1, data2 0 down / 1 up. rockcat turns `20` `b0` `c0` into "changed" and `ff` into Easy-Shift.

## Lights from the pc, report `10`

Everyone but the XTD: `[10 10 ff ff ff state zone 0 effect speed r g b r g b]`. The three `ff` leave Easy-Shift, its lock and Easy-Aim alone. roccat-tools sends state 1, zone 1, effect 1 (on), speed 1, and the colour as ambient and event colour, so thats what rockcat sends. One colour for the whole mouse, the first led of a frame speaks for all. State 0 with all zero after the `ff` gives the lights back.

The Kone XTD has its own layout: `[10 10 02 effect rgb rgb rgb rgb]`, mode 2 = "gfx", effect 1 = on (2 = blink), a colour per light. roccat-tools never switches that off again, so `direct off` sends the plain Talk FX "off" above and hopes. If the lights stay stuck on an XTD after `direct off`, switching profiles or writing a setting is the next thing to try.

## Not done

- sensitivity x / y and xy sync, Talk FX allowed (XTD), colour flow direction, angle snap strength, sensor alignment: they survive every write, but the api has no name for them yet
- TCU / DCU (report `0f` and `0c`)
- the original Kone (`2ced`) is NOT in here: it talks through USB control requests with odd wValues (`0x5a`, `0x15a`, ...) that only the kernel driver `hid-roccat-kone` sends, hidraw feature reports cant express them
