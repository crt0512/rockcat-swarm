# ROCCAT Swarm device modules

Every device module that ROCCAT Swarm 1.9482 can download, as returned by the official update server (`POST https://acpv.prod.turtlebeach.com/swarm1/autoupdate/SWARM`) when queried with each USB product ID listed in the bundled `settings.xml`. Module versions are as of 2026-10-21. `tools/re/allmods.py` regenerates the list from the update server. Devices from before Swarm never had a module, they say "no Swarm module" and come out of [roccat-tools](https://sourceforge.net/projects/roccat/) (GPL), which documented them years ago.

The list is sorted by how much I trust the support:

- **Tested and Working**: checked on a real one, with the firmware it had
- **Assumed to Work**: never on hardware here, but a sibling of a tested one (same reports, another colour, layout or size)
- **Might Work**: written from roccat-tools or from what the Swarm monitor, modules and firmware do, nearly everything supported, only ever ran against a pretend device (`ROCKCAT_SIM=...`)
- **Basics might Work**: the same, but only the basics (mostly the keys), the rest is still missing
- **Todo**: known by name (they show up in `rockcat-cli list` and the gui with "not supported yet"), nothing gets sent to them

If you own one of the untested ones PLEASE try it and tell in issues of the repo, make a backup of your device first though! : (`rockcat-cli --device PID backup FILE`)

Protocol notes per family: [docs/protocol/](docs/protocol/).

## Tested and Working

### Mouse

- [x] **Kova** - `1E7D:2CEE`, module `KOVA` v1.2123 (3.7 MB) - tested, firmware 1.18 (gray)
- [x] **Kone AIMO Remastered** - `1E7D:2E2C`, module `KONE_AIMO` v1.0009 (1.4 MB) - tested by upstream, firmware 1.05. dont own one though.
- [x] **Lua** - `1E7D:2C2E`, no Swarm module - settings get through on a real one, the mouse cant be read afaik, does what it should when configured from PC though.

### Keyboard

- [x] **Ryos MK FX** - `1E7D:2FDA`, module `RYOS_MK_FX` v1.2128 (4.7 MB) - tested, firmware 1.31
- [x] **Ryos MK Pro** - `1E7D:3232`, no Swarm module - tested, firmware 1.26
- [x] **Sova** - `1E7D:2F76`, module `SOVA_MEMBRANE` v1.1128 (4.9 MB) - tested, firmware 1.27 (membrane)

## Assumed to Work

### Mouse

- [x] **Kova** - `1E7D:2CEF`, module `KOVA` v1.2123 (3.7 MB) - untested, same mouse as the tested gray one in white
- [x] **Kova** - `1E7D:2CF0`, module `KOVA` v1.2123 (3.7 MB) - untested, same mouse as the tested gray one in black
- [x] **Kone AIMO** - `1E7D:2E27`, module `KONE_AIMO` v1.0135 (3.5 MB) - untested, the original of the Remastered

### Keyboard

- [x] **Ryos MK** - `1E7D:3138`, no Swarm module - untested, the MK FX / MK Pro reports
- [x] **Ryos MK Glow** - `1E7D:31CE`, no Swarm module - untested, the MK FX / MK Pro reports
- [x] **Ryos TKL** - `1E7D:3106`, no Swarm module - untested, the MK FX / MK Pro reports
- [x] **Ryos TKL Pro** - `1E7D:316A`, no Swarm module - untested, the MK Pro reports
- [x] **Sova** - `1E7D:2F94`, module `SOVA_MECHANICAL` v1.1128 (4.9 MB) - untested, treated like the tested membrane one, should technically support per key lighting but not sure, treated like membranes one backlight for now

## Might Work

### Mouse

- [x] **Kiro** - `1E7D:2C38`, module `KIRO` v1.2123 (3.3 MB) - untested
- [x] **Kone EMP** - `1E7D:2E24`, module `KONE_EMP` v1.0624 (3.2 MB) - untested, from the Swarm monitor and module. no light from the pc, no factory buttons
- [x] **Kone Pure Owl-Eye** - `1E7D:2DCD`, module `KONE_PURE_OWL-EYE` v1.0127 (2.7 MB) - untested, from the Swarm monitor and module, no factory buttons
- [x] **Kova AIMO** - `1E7D:2CF1`, module `KOVA_AIMO` v1.0017 (1.5 MB) - untested, from the Swarm monitor and module, AIMO effect not drawn
- [x] **Kova AIMO** - `1E7D:2CF2`, module `KOVA_AIMO` v1.0017 (1.5 MB) - untested, from the Swarm monitor and module, AIMO effect not drawn
- [x] **Kova AIMO** - `1E7D:2CF3`, module `KOVA_AIMO` v1.0017 (1.5 MB) - untested, from the Swarm monitor and module, AIMO effect not drawn
- [x] **Leadr** - `1E7D:2E4C`, module `LEADR` v1.0122 (1.9 MB) - untested, from the Swarm monitor and module (a Tyon without the cable), no factory buttons, nothing wireless
- [x] **Leadr** - `1E7D:2E4E`, module `LEADR` v1.0122 (1.9 MB) - untested, from the Swarm monitor and module (a Tyon without the cable), no factory buttons, nothing wireless
- [x] **Nyth** - `1E7D:2E7C`, module `NYTH` v1.7124 (6.2 MB) - untested
- [x] **Nyth** - `1E7D:2E7D`, module `NYTH` v1.7124 (6.2 MB) - untested
- [x] **Kone[+]** - `1E7D:2D51`, no Swarm module - untested
- [x] **Kone XTD** - `1E7D:2E22`, no Swarm module - untested
- [x] **Kone XTD Optical** - `1E7D:2E23`, no Swarm module - untested
- [x] **Kone Pure** - `1E7D:2DBE`, no Swarm module - untested
- [x] **Kone Pure Optical** - `1E7D:2DB4`, no Swarm module - untested
- [x] **Kone Pure Military** - `1E7D:2DBF`, no Swarm module - untested
- [x] **Kone Pure Optical Black** - `1E7D:2DC2`, no Swarm module - untested
- [x] **Tyon** - `1E7D:2E4A` (black), `1E7D:2E4B` (white), no Swarm module - untested
- [x] **Savu** - `1E7D:2D5A`, no Swarm module - untested, no macros yet

### Keyboard

- [x] **Isku** - `1E7D:319C`, no Swarm module - untested
- [x] **Isku FX** - `1E7D:3264`, no Swarm module - untested
- [x] **Magma** - `1E7D:3124`, module `MAGMA` v1.0009 (5.1 MB) - untested, from the Swarm monitor and module (the Vulcan Pro family): 5 profiles, keys on three layers, the 5 colour zones (which key is in which zone is a guess), effects
- [x] **Magma Mini** - `1E7D:69A0`, module `MAGMA_MINI` v1.0000 (2.3 MB) - untested, from the Swarm monitor and module (the Vulcan Pro family): 5 profiles, keys on three layers, the 5 colour zones (which key is in which zone is a guess), effects
- [x] **Pyro** - `1E7D:314C`, module `PYRO` v1.0009 (5.1 MB) - untested, from the Swarm monitor and module (the Vulcan Pro family): 5 profiles, keys on three layers, per key colour, effects. no macros, no light from the pc
- [x] **Skeltr** - `1E7D:2FC6`, module `SKELTR` v1.2128 (6.1 MB) - untested
- [x] **Suora** - `1E7D:2FA8`, module `SUORA` v1.3124 (6.2 MB) - untested, from roccat-tools: keys (one layer), the backlight and its effects. no macros
- [x] **Vulcan 100 AIMO** - `1E7D:307A`, module `VULCAN` v1.0036 (6.1 MB) - untested, keys and stored lights, no light from the pc yet
- [x] **Vulcan 120/121/122 AIMO** - `1E7D:3098`, module `VULCAN` v1.0036 (6.1 MB) - untested, keys and stored lights, no light from the pc yet
- [x] **Vulcan TKL AIMO** - `1E7D:2FEE`, module `VULCAN` v1.0012 (5.6 MB) - untested, keys and stored lights like the 100 / 120, no light from the pc yet
- [x] **Vulcan 80** - `1E7D:305C`, module `VULCAN_80` v1.0027 (4.7 MB) - untested, from the Swarm module and firmware: keys, the blue light per key, its five effects. no effects from the pc
- [x] **Vulcan Pro** - `1E7D:30F7`, module `VULCAN_PRO` v1.0013 (6.2 MB) - untested, from the Swarm monitor and module: 5 profiles, keys on three layers, per key colour, effects. no macros, no game mode keys, no light from the pc
- [x] **Vulcan TKL Pro** - `1E7D:311A`, module `VULCAN_PRO` v1.0016 (6.5 MB) - untested, from the Swarm monitor and module: 5 profiles, keys on three layers, per key colour, effects. no macros, no game mode keys, no light from the pc
- [x] **Vulcan TKL Pro** - `1E7D:3167`, module `VULCAN_PRO` v1.0016 (6.5 MB) - untested, from the Swarm monitor and module: 5 profiles, keys on three layers, per key colour, effects. no macros, no game mode keys, no light from the pc
- [x] **Vulcan II** - `1E7D:2F4E`, module `VULCAN_II` v1.0002 (7.2 MB) - untested, from the Swarm monitor and module: 5 profiles, keys on three layers, per key colour, effects. no macros, no game mode keys, no light from the pc
- [x] **Vulcan II Max** - `1E7D:2EE2`, module `VULCAN_II_MAX` v1.0012 (7.6 MB) - untested, from the Swarm monitor and module: 5 profiles, keys on three layers, per key colour, effects. no macros, no game mode keys, no light from the pc, palm rest lights not yet
- [x] **Vulcan II Mini** - `1E7D:2F09`, module `VULCAN_II_MINI` v1.0014 (3.2 MB) - untested, from the Swarm monitor and module: 5 profiles, keys on three layers, per key colour, effects. no macros, no game mode keys, no light from the pc

## Basics might Work

### Keyboard

- [x] **Horde** - `1E7D:300C`, module `HORDE` v1.0132 (3.7 MB) - untested, from the Swarm module: keys on three layers, M keys. the light not yet
- [x] **Horde AIMO** - `1E7D:303E`, module `HORDE_AIMO` v1.0136 (4.6 MB) - untested, from the Swarm module: keys on three layers, M keys. the light not yet
- [x] **Isku+** - `1E7D:31E2`, module `ISKU+` v1.0126 (2.9 MB) - untested, keys and macros, the light not yet
- [x] **Isku+ Force FX** - `1E7D:3214`, module `ISKU+_FORCE_FX` v1.0129 (4.6 MB) - untested, keys and macros from the Swarm monitor and module, the light not yet
- [x] **Suora FX** - `1E7D:3246`, module `SUORA_FX` v1.0128 (5.9 MB) - untested, from the Swarm monitor and firmware: keys (one layer). the per key light and macros not yet

## Todo

### Mouse

- [ ] **Burst Core** - `1E7D:2DE6`, module `BURST` v1.0006 (1.7 MB)
- [ ] **Burst Pro** - `1E7D:2DE1`, module `BURST` v1.0011 (2.4 MB)
- [ ] **Burst Pro Air** - `1E7D:2CA6`, module `BURST_PRO_AIR` v1.0014 (6.7 MB)
- [ ] **Burst Pro Air** - `1E7D:2CAB`, module `BURST_PRO_AIR` v1.0014 (6.7 MB)
- [ ] **Kain 100 AIMO** - `1E7D:2D00`, module `KAIN` v1.0016 (2.4 MB)
- [ ] **Kain 120 AIMO** - `1E7D:2D5C`, module `KAIN` v1.0015 (2.4 MB)
- [ ] **Kain 200 AIMO** - `1E7D:2D5F`, module `KAIN_200` v1.0018 (2.0 MB)
- [ ] **Kain 200 AIMO** - `1E7D:2D60`, module `KAIN_200` v1.0018 (2.0 MB)
- [ ] **Kone Air** - `1E7D:AFC9`, module `KONE_AIR` v1.0005 (0.4 MB)
- [ ] **Kone Air** - `1E7D:AFCA`, module `KONE_AIR` v1.0005 (0.4 MB)
- [ ] **Kone Pro** - `1E7D:2C88`, module `2C88` v1.0010 (2.0 MB)
- [ ] **Kone Pro Air** - `1E7D:2C8E`, module `2C8E` v1.0018 (2.6 MB)
- [ ] **Kone Pro Air** - `1E7D:2C92`, module `2C92` v1.0018 (2.6 MB)
- [ ] **Kone Pure SE** - `1E7D:2DCA`, module `KONE_PURE_SE` v1.0123 (1.5 MB)
- [ ] **Kone Pure SEL** - `1E7D:2DCB`, module `KONE_PURE_SEL` v1.0014 (1.5 MB)
- [ ] **Kone Pure Ultra** - `1E7D:2DD2`, module `2DD2` v1.0013 (1.8 MB)
- [ ] **Kone Pure Ultra iCafe** - `1E7D:2DD7`, module `2DD7` v1.0006 (1.1 MB)
- [ ] **Kone XP** - `1E7D:2C8B`, module `KONE_XP` v1.0006 (2.5 MB)
- [ ] **Kone XP Air** - `1E7D:2CB2`, module `KONE_XP_AIR` v1.0019 (9.1 MB)
- [ ] **Kone XP Air** - `1E7D:2CB6`, module `KONE_XP_AIR` v1.0019 (9.1 MB)
- [ ] **Pure Air** - `1E7D:2CCC`, module `PURE_AIR` v1.0007 (2.1 MB)
- [ ] **Pure Air** - `1E7D:3C6E`, module `PURE_AIR` v1.0007 (2.1 MB)
- [ ] **Pure SEL** - `1E7D:2CC8`, module `PURE_SEL` v1.0006 (1.7 MB)
- [ ] **Kone** (the first one) - `1E7D:2CED`, no Swarm module - roccat-tools talks to it through a kernel driver with usb control requests, hidraw cant send those
- [ ] **Kova[+]** - `1E7D:2D50`, no Swarm module
- [ ] **Pyra** - `1E7D:2C24` (wired), `1E7D:2CF6` (wireless), no Swarm module

### Keyboard

- [ ] **Vulcan II Mini Air** - `1E7D:3AA9`, module `VULCAN_II_MINI_AIR` v1.0014 (24.8 MB)
- [ ] **Vulcan II Mini Air** - `1E7D:6978`, module `VULCAN_II_MINI_AIR` v1.0014 (24.8 MB)
    - Both Vulcan II Mini Air Keyboards use a wireless DoNgLe, Too much effort for me for something I cant test, if you want this supported send me one of these boards lol.
- [ ] **Arvo** - `1E7D:30D4`, no Swarm module
    - The cheap tiny one
- [ ] **Valo** - `????:????`, no Swarm module
    - The one with the LCD Screen

### Headset

- [ ] **Elo 7.1 Air** - `1E7D:3A37`, module `3A37` v1.0019 (14.0 MB)
- [ ] **Elo 7.1 Air** - `1E7D:3A39`, module `3A39` v1.0019 (14.0 MB)
- [ ] **Elo 7.1 USB** - `1E7D:3A34`, module `KHAN_AIMO` v1.0022 (12.3 MB)
- [ ] **Khan AIMO** - `1E7D:39D0`, module `KHAN_AIMO` v1.0052 (13.5 MB)
- [ ] **Syn Pro Air *(no module on server)*** - `1E7D:3A3D`, module `SYN_SERIES`
- [ ] **Syn Max Air** - `1E7D:3A3F`, module `SYN_SERIES` v1.0016 (3.1 MB)
- [ ] **Syn Max Air** - `1E7D:3A41`, module `SYN_SERIES` v1.0016 (3.1 MB)
- [ ] **Syn Pro Air** - `1E7D:3A3B`, module `SYN_SERIES` v1.0004 (3.0 MB)
- [ ] **Torch** - `1E7D:3A56`, module `TORCH` v1.0005 (0.4 MB)

### Mousepad / Dock

- [ ] **Kone XP Air** - `1E7D:2CB8`, module `KONE_XP_AIR_DOCK` v1.0019 (9.1 MB)
- [ ] **Sense Aimo *(no module on server)*** - `1E7D:343A`, module `AIMO_PAD`
- [ ] **Sense AIMO XXL** - `1E7D:343B`, module `AIMO_PAD_WIDE` v1.0013 (2.2 MB)

### Software module replacements

- [ ] **ROCCAT Talk FX** - id `2712`, v1.2895
- [ ] **Alienware AlienFX** - id `2713`, v1.02
- [ ] **Swarm Connect** - id `0004`, v1.3114
- [ ] **Swarm Waves Driver** - id `2714`, v1.0010
- [ ] **AIMO Stuff** - idk