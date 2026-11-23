# Protocols

One file per protocol family. A family is a bunch of devices that talk the same way and only differ in sizes and tables, so one implementation in `src/lib/families/` covers all of them.

| family | devices | doc | state |
|---|---|---|---|
| kone-aimo | Kone AIMO Remastered (and the original Kone AIMO with a shorter settings report) | [kone-aimo.md](kone-aimo.md) | implemented, written by upstream from the decompiled Swarm module |
| kova | Kova 2016 gray / white / black | [kova.md](kova.md) | implemented, tested on a real gray one (firmware 1.18). white and black untested |
| ryos | Ryos MK FX, MK, MK Glow, MK Pro, TKL, TKL Pro | [ryos.md](ryos.md) | implemented. MK FX tested (firmware 1.31), MK Pro tested (firmware 1.26), the rest from roccat-tools |
| sova | Sova membrane, Sova MK | [sova.md](sova.md) | implemented, membrane tested (firmware 1.27), MK untested and without its per key light |
| lua | Lua | [lua.md](lua.md) | implemented from roccat-tools, write only. commands accepted by a real one |
| classic | Nyth, Tyon, Kiro, Savu, Kone EMP, Kone Pure Owl-Eye, Leadr, Kova AIMO (one table per mouse, Kova 2016 style reports) | [classic.md](classic.md) | implemented from roccat-tools (the first four) and from the Swarm monitor and modules (the rest), never seen on hardware |
| isku | Isku, Isku FX | [isku.md](isku.md) | implemented from roccat-tools, never seen on hardware |
| skeltr | Skeltr, Isku+ Force FX | [skeltr.md](skeltr.md) | implemented, the Skeltr from roccat-tools, the Force FX from the Swarm monitor and module (no light). never seen on hardware |
| isku-plus | Isku+ | [isku-plus.md](isku-plus.md) | implemented from the Swarm monitor and module, never seen on hardware, no light |
| kone-plus | Kone[+], Kone XTD, XTD Optical, Kone Pure, Pure Optical, Pure Military, Pure Optical Black | [kone-plus.md](kone-plus.md) | implemented from roccat-tools only, never seen on hardware |
| vulcan | Vulcan 100 AIMO, Vulcan 120/121/122 AIMO, Vulcan TKL AIMO, Vulcan 80, Horde, Horde AIMO | [vulcan.md](vulcan.md) | implemented from the firmware and Swarm, never seen a real one |
| suora | Suora, Suora FX | [suora.md](suora.md) | from roccat-tools, output reports on the vendor interface, never seen a real one |
| vulcan-pro | Vulcan Pro, Vulcan TKL Pro, Vulcan II, II Max, II Mini, Pyro, Magma, Magma Mini | [vulcan-pro.md](vulcan-pro.md) | implemented from the Swarm monitor and modules (encrypted firmware), never seen a real one |
| kone-aimo-original | Kone AIMO (2e27) | [kone-aimo-original.md](kone-aimo-original.md) | implemented from the Swarm module, never seen a real one |
| swarm-ident | 63 product ids of Swarm era devices | [swarm-ident.md](swarm-ident.md) | name only, nothing gets sent |

What all of them have in common (the code for it is `src/lib/reports.cpp`):

- everything goes through HID feature reports, first byte is the report id
- report `04` is the control report. Writing `[04 a b]` selects what the next read returns, usually `a` = profile and `b` = which kind of data
- after every write you read `04` until byte 1 stops being `3`. `1` = done, `2` = the device didnt like what you sent, `3` = still busy
- most data reports end in a 16 bit checksum, the plain sum of all bytes before it, little endian

How to look at a device thats not in here yet: [../CAPTURES.md](../CAPTURES.md).
