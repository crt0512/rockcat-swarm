# rockcat-swarm

ROCCAT Swarm, but open source and on linux, because Turtle Beach kinda killed Swarm and you cant even download it anymore without extracting links from their CDN.

<p align="center"><img src="data/rockcat-swarm.svg" alt="rockcat-swarm icon" width="128"></p>

![alt text](docs/images/screenshot.png)

This is a fork of [rossat-swarm](https://github.com/TheElevatedOne/rossat-swarm) by TheElevatedOne, who reverse engineered Swarm 1.9482 to get his Kone AIMO Remastered working and then (fair enough) called it a day. The goal here is bigger: every device Swarm supported plus the older ones it never did, in a gui that doesnt make you want to throw the mouse.

It comes in three parts:

- `rockcat-swarm`, the GTK3 window. settings, button / key assignment, illumination, profiles, tray icon, one tab per plugged in device
- `rockcat-cli`, same thing for terminals and scripts
- `librockcat.so` + `include/rockcat/rockcat.h`, if you want to build your own thing in C or C++

<br>

> Note : on windows? you dont need this, the original Swarm and Ryos Software still work fine there. Installers and modules are archived at [512mb Archive/Software/Roccat](https://dsl.512mb.net/Software/Roccat/).

> Note : The docs and code comments are partially kept by an llm, sorry im too lazy for that and documenting everything plus I tend to swear way too much in docs/comments. I do put alot of real human effort into this project wherever I can though, as an example see [Handdrawn Icon Showcase](assets/free/SHOWCASE.md) which serve as drop in replacements incase Roccats new owner (Turtlebeach) doesnt want me using them or to let this package ever ship on true libre oui oui free free distributions.


## What works

TLDR: 56 devices, 6 of them tried on real hardware here so far. The full list is in [MODULES.md](MODULES.md), sorted into tested and working, assumed to work (siblings of tested ones), might work, basics might work and todo.

Tested on a real hardware:

- **ROCCAT Ryos MK FX** (`1e7d:2fda`)
- **ROCCAT Ryos MK Pro** (`1e7d:3232`)
- **ROCCAT Kova 2016** gray (`1e7d:2cee`)
- **ROCCAT Sova** membrane (`1e7d:2f76`)
- **ROCCAT Lua** (`1e7d:2c2e`)
- **ROCCAT Nyth** black (`1e7d:2e7c`)

Soon to be tested/supported hardware (I made bad financial decissions) :
- Mice
    - Tyon
    - Kova Aimo (Aka. Kova Remastered)
    - Kone Aimo
    - Kone Pure (Military)
    - Kone XTD
- Keyboards
    - Sova MK
    - Isku
    - Horde Aimo
    - Vulkan 122 Aimo
    - Valo (their first 2009 Keyboard)

The Roccat Leadr would've also been on that list but some doofus overbid me on the only reasonably priced one I could find. **IF YOU HAVE ONE AND DONT WANT IT ANYMORE ILL HAPPILY TAKE IT OFF YOUR HANDS** 
All above are currently being shipped to me and should therefore soon gain verified support.

## Building

You need CMake 3.16 or newer, a C++17 compiler, `hidapi` 0.14 or newer, GTK 3, `libwebp` (the device pictures) and `glib-compile-resources`. `libayatana-appindicator3` is optional and gets you the tray icon.

Debian (like me) :

```bash
sudo apt install build-essential cmake pkg-config libhidapi-dev libgtk-3-dev libwebp-dev libayatana-appindicator3-dev
```

For the I use Arch btw people:

```bash
sudo pacman -S cmake hidapi gtk3 libwebp libayatana-appindicator
```

Gentoo/LFS or whatever you use: you know what to do.

Theres a Makefile so nobody has to remember the cmake incantations:

```bash
make
```

```bash
sudo make install
```

If you would rather talk to cmake yourself thats fine too, the Makefile doesnt do anything magic. The switches are `-DROCKCAT_BUILD_GUI=OFF`, `-DROCKCAT_BUILD_CLI=OFF` and `-DROCKCAT_BUILD_TESTS=OFF`.

### Packages

Installing through the package manager is nicer than `make install` because it can be removed cleanly again and the udev rule gets reloaded for you. Both of these drop the package into `dist/`.

Based Debian :

```bash
make deb
```

```bash
sudo apt install ./dist/rockcat-swarm_*.deb
```

Arch btw:

```bash
make arch
```

```bash
sudo pacman -U dist/rockcat-swarm-*.pkg.tar.zst
```

## Permissions

Before complaining that it cant find your mouse please read this!

The devices are talked to through `/dev/hidraw*`, which normal users are not allowed to open. The udev rule fixes that. `cmake --install` puts it into `<prefix>/lib/udev/rules.d`, if you didnt install then copy it yourself:

```bash
sudo cp data/70-rockcat.rules /etc/udev/rules.d/
```

```bash
sudo udevadm control --reload
```

```bash
sudo udevadm trigger
```

If it still says no: UNPLUG THE MOUSE AND PLUG IT BACK IN.

## CLI

```bash
rockcat-cli list                         # whats plugged in, with a number in front
rockcat-cli --device 2 info              # more than one device? pick it by that number (or by product id, 2e2c)
rockcat-cli info                         # firmware + active profile
rockcat-cli profile 2                    # switch to profile 2
rockcat-cli show -p 1                    # dump profile 1 settings
rockcat-cli set dpi1=800 dpi2=1600x1200 dpi-active=2 polling=1000
rockcat-cli set effect=fully-lit brightness=200 led=all:#65c3f7 led=wheel:#ff0000
rockcat-cli buttons                      # button assignment of the active profile
rockcat-cli bind 9 shortcut ctrl+c       # thumb backward -> Ctrl+C
rockcat-cli bind shift+11 volume-up
rockcat-cli bind 8 brightness-up         # thumb forward: brightness step up (engine)
rockcat-cli macro -b 8 --text "gg wp"    # store a text macro on thumb forward
rockcat-cli set effect=aimo theme=3      # AIMO uses theme colours, not per led colours
rockcat-cli set effect=breathing render=engine
rockcat-cli shift-light on '#ff0000' pulse  # Easy-Shift colour (engine rendered profiles)
rockcat-cli engine                       # draw AIMO / Heartbeat / *-2.0 effects (Ctrl+C stops)
rockcat-cli calibrate                    # TDCU calibration for your surface
rockcat-cli backup my.rswb               # all 5 profiles, `restore my.rswb` writes them back
rockcat-cli direct '#00ff00'             # host controlled colour, `direct off` to undo
rockcat-cli dump                         # raw hex of the reports
```

For devices that arent supported yet theres a raw mode that talks reports directly, and everything can be recorded:

```bash
rockcat-cli raw list                     # every ROCCAT hidraw node and the report ids it has
rockcat-cli --device /dev/hidraw4 raw get 0f 8   # read feature report 0f, 8 bytes
rockcat-cli capture mine.cap info        # run a command and write down everything that went over the wire
```

How recordings work and how to make one of YOUR device (please do, thats how devices I cant impulse buy can get support through me): [docs/CAPTURES.md](docs/CAPTURES.md).


## Lighting engine

Many of the peripherals built in effects are a bit meh but I mean thats fine by me, if you want more dedicated effects they need to be sent by the PC and require Rockcat Swarm to stay running, close to tray in GUI or if you dont want to start it each time :

```bash
systemctl --user enable --now rockcat-engine.service
```


## Known meh

- most devices are untested (see [MODULES.md](MODULES.md)). They are written from roccat-tools or from the Swarm modules and run fine against their pretend selves, which only proves I understood the documents the way I think. Own one of them? Back it up first (`rockcat-cli --device PID backup my.rswb`, that only reads), then try. A recording of it (`rockcat-cli capture mine.cap backup x.rswb`, read only too) is the best thing you can send in, see [docs/CAPTURES.md](docs/CAPTURES.md)
- nobody working on this fork owns a Kone AIMO Remastered, the one device upstream supported. Changes to its code are guarded by the offline tests and a recording made against a simulated mouse, not a real one. If you own one: a real recording would be very prem
- light from the pc (the engine effects) is only done for the Kone AIMO and AIMO Remastered, the Kone[+] / XTD / Pure family, the Kova 2016, the Ryos MK FX, MK Pro and TKL Pro. the others only show what they store themselves
- the Nyth picture probably has its button markers on the wrong buttons, Swarm counts them differently than roccat-tools and I follow roccat-tools
- firmware updates are not implemented.
- the AIMO animation is my own take on Swarms (same themes, led groups and activity reaction), not a byte exact copy of `aimo.dll`.
- This repo and the resulting executable is very very fat (I love bundeling PNGs ...)
    - Atleast now I understand why Roccat distributed everything as a module in Swarm

## Credits and legal stuff

- [TheElevatedOne](https://github.com/TheElevatedOne/rossat-swarm) wrote rossat-swarm, which this is forked from. The whole Kone AIMO Remastered protocol work is his.
- Not affiliated with ROCCAT or Turtle Beach in any way. No ROCCAT code is in here, the Swarm layout is rebuilt in GTK and the app icon is an original drawing.
- The device pictures in `assets/roccat/` ARE ROCCATs, taken from Swarm, and they are not under the GPL like the rest. Where each one came from and how to build without them (`-DROCKCAT_ROCCAT_ASSETS=OFF`) is in [assets/roccat/ATTRIBUTION.md](assets/roccat/ATTRIBUTION.md). Rights holder and unhappy about it? Open an issue, they get removed.
- License: [GPL-3.0-only](LICENSE) for everything except `assets/roccat/`.

## Whats where

```
include/rockcat/rockcat.h   public C API
src/lib/                    protocol + HID transport (hidapi)
src/cli/                    rockcat-cli
src/gui/                    rockcat-swarm (GTK3)
data/                       icon, css, udev rule, desktop file, user service, pkg-config template
assets/roccat/              device pictures out of Swarm, not mine, read the ATTRIBUTION.md in there
assets/free/                my own stand ins with the same names, for builds without the above
tools/dev/                  gui-shot.sh, takes a picture of the gui without a screen
tools/assets/               the scripts that fill those two folders (extract.py for icons, mouse-view.py for the mouse pictures with markers and lights), plus dump-swarm.sh and sort-swarm.py if you want ALL the artwork out of your own Swarm install, sorted per device
docs/protocol/              what I know about how the devices talk, one file per family (README.md in there is the part all of them share)
docs/CAPTURES.md            how recordings work
tools/re/                   scripts used for the reverse engineering
tools/capture/              read only scripts that record what a device has stored
tests/                      offline tests (no device needed)
tests/captures/             the recordings the tests replay
aur/                        PKGBUILDs
.github/workflows/          build + test and a .deb on every push, release tarball per tag
Makefile                    make, make test, make install, make deb, make arch
packaging/                  what make deb and make arch need (postinst, PKGBUILD template)
MODULES.md                  every Swarm device module plus the older devices, what works and what was tested
icon.svg                    icon source drawing (data/rockcat-swarm.svg adds the accent colours)
```
