# Protocols

One file per protocol family. A family is a bunch of devices that talk the same way and only differ in sizes and tables, so one implementation in `src/lib/families/` covers all of them.

| family | devices | doc | state |
|---|---|---|---|
| kone-aimo | Kone AIMO Remastered (and the original Kone AIMO with a shorter settings report) | [kone-aimo.md](kone-aimo.md) | implemented, written by upstream from the decompiled Swarm module |
| kova | Kova 2016 gray / white / black | [kova.md](kova.md) | implemented, tested on a real gray one (firmware 1.18). white and black untested |
| ryos | Ryos MK FX (MK, MK Glow, MK Pro, TKL Pro sit on the same base and are not done yet) | [ryos.md](ryos.md) | implemented, tested on a real one (firmware 1.31) |
| kone-plus | Kone[+], Kone XTD, XTD Optical, Kone Pure, Pure Optical, Pure Military, Pure Optical Black | [kone-plus.md](kone-plus.md) | implemented from roccat-tools only, never seen on hardware |
| vulcan | Vulcan 100 AIMO, Vulcan 120/121/122 AIMO | [vulcan.md](vulcan.md) | implemented from the firmware and Swarm, never seen a real one |
| kone-aimo-original | Kone AIMO (2e27) | [kone-aimo-original.md](kone-aimo-original.md) | implemented from the Swarm module, never seen a real one |
| swarm-ident | 63 product ids of Swarm era devices | [swarm-ident.md](swarm-ident.md) | name only, nothing gets sent |

What all of them have in common (the code for it is `src/lib/reports.cpp`):

- everything goes through HID feature reports, first byte is the report id
- report `04` is the control report. Writing `[04 a b]` selects what the next read returns, usually `a` = profile and `b` = which kind of data
- after every write you read `04` until byte 1 stops being `3`. `1` = done, `2` = the device didnt like what you sent, `3` = still busy
- most data reports end in a 16 bit checksum, the plain sum of all bytes before it, little endian

How to look at a device thats not in here yet: [../CAPTURES.md](../CAPTURES.md).
