# Whose pictures these are

Everything in this folder was made by ROCCAT (ROCCAT GmbH, since 2019 part of Turtle Beach Corporation) and shipped inside their ROCCAT Swarm software. It is THEIR artwork. It is not covered by the GPL the rest of this repository is under, and nobody at ROCCAT or Turtle Beach has given this project a license for it.

It is in here because Swarm is discontinued and gone from the download pages, because a settings tool is a lot nicer when it shows you the device you are configuring, and because ROCCAT has historically been relaxed about linux tools doing this. That is an assumption on our side and not a permission.

If you are the rights holder and want it gone: open an issue on https://github.com/crt0512/rockcat-swarm and it will be removed, no discussion needed.

## Building without it

```bash
cmake -S . -B build -DROCKCAT_ROCCAT_ASSETS=OFF
```

That builds against `assets/free/` instead (plain placeholder drawings made for this project, same file names) and the credit line disappears from the settings page. Distributions that cant ship unlicensed artwork should use that switch.

## What came from where

| file | from |
|---|---|
| `devices/2e2c.png` | Kone AIMO Remastered module 1.0009 (`KONE_AIMO_16K`), `device_meta_data/*.devicemetadata-ms`, the 256 px picture inside `DeviceInformation/device.ico` |
| `devices/2cee.png` | Kova module 1.2119 (`KOVA_GRAY`), same place |
| `devices/2cef.png`, `devices/2cf0.png` | Kova modules `KOVA_WHITE` and `KOVA_BLACK`, same place |
| `devices/2fda.png` | Ryos MK FX module (`RYOS_MK_FX`), same place |
| `keyboards/2fda/body.png` | Ryos MK FX module, `graphics/keyboard_layout/base/1920x1080_without_keys.png` |
| `keyboards/2fda/keycaps-eu.png`, `keycaps-us.png` | same module, `graphics/keyboard_layout/<region>/1920x1080/Keycaps_small.png` |
| `keyboards/2fda/legends-eu.png`, `legends-us.png` | same module, `graphics/keyboard_layout/<region>/1920x1080/Keycaps_text_small.png` |
| `views/2cee/top.png`, `views/2cef/top.png`, `views/2cf0/top.png` | Kova module, the top view of the button page (`KOVA_GRAY_TOP_VIEW.png`, `KOVA_WHT_...`, `KOVA_BLK_...`), put on a bigger canvas by `tools/assets/mouse-view.py` |
| `views/*/light0.png` | Kova AIMO module, `Backlight.png` (the glow at the back, same shape of mouse), scaled to fit |
| `views/*/light1.png` | cut out of the top view above: the wheel light ROCCAT painted into it in red, as a mask |
| `views/*/view.ini` | not a picture: where Swarm puts the marker of each button, read out of the Kova module |
| `keyboards/2fda/kb_matrix.xml` | same module, `graphics/keyboard_layout/base/kb_matrix.xml`. Not a picture: which key sits where and which led it has |

Files are named after the usb product id of the device. Icons and keyboard pictures were copied out unchanged by `tools/assets/extract.py`, the `views/` are put together by `tools/assets/mouse-view.py` (recipes in `tools/assets/views.json`). Adding the icon of the next device:

```bash
tools/assets/extract.py /path/to/Swarm/module/KOVA_GRAY 2cee
```

Then add a line to the table above, and run `tools/assets/placeholders.py` so the free set gets a stand in with the same name.
| `devices/*.png` (all others) | the `device_meta_data` icon of each module (Turtle Beach module downloads). 2de6, 2cab, 2ca6, afc9, afca, 2c8b, 3aa9, 6978, 3a3b, 3138, 31ce, 3232, 2dbe, 2dbf, 2db4, 2dc2, 2e22, 2e23, 2d51 have no usable icon in their module and are cut out of the Swarm picture of the device (or its same shell sibling) by `tools/assets/icon-from.py`. 2dd7 is the 2dd2 icon |
| `views/<pid>/` (all others) | the button page picture and lighting layers of the matching module, put together by `tools/assets/mouse-view.py` (recipes in `tools/assets/views.json`). pre-Swarm mice borrow the pictures of a sibling with the same shell: Kone Pure family from the Owl-Eye, Kone XTD / XTD Optical / Kone[+] from the Kone EMP |
| `keyboards/<pid>/` (all others) | `graphics/keyboard_layout/` of the matching module, copied by `tools/assets/extract.py`. 319c / 3264 (Isku, Isku FX) are the Isku+ pictures, 3138 / 31ce / 3232 (Ryos MK, MK Glow, MK Pro) the Ryos MK FX ones |
| `devices/2c2e.png` (Lua) | the Lua has no Swarm module. cut by `tools/assets/icon-from.py` out of ROCCATs own top view render from the old product page (`media.roccat.org/img/products/Lua/images/Lua-top-2-5x-1.png`, via the Internet Archive) |
| `views/2c2e/` (Lua) | the same render, cropped and scaled by `tools/assets/lua-view.py`, the logo mask cut out of it by its colour, button markers placed by hand |
| `devices/` 2ced, 30d4, 2d50, 2c24, 2cf6, 316a, 3106, 2d5a, 2e4a, 2e4b, 3a56, 2d51, 2e22, 2e23, 2dbe, 2dbf, 2db4, 2dc2, 3138, 31ce, 3232, 319c, 3264 | ROCCATs own transparent top view renders from their old product pages (`media.roccat.org/img/products/...`, via the Internet Archive), cut by `tools/assets/icon-from.py`. the Ryos TKL (3106) has no render of its own and shows the TKL Pro. these replace the icons the pre-Swarm devices borrowed from a sibling before |


## Why webp

The table says `.png` because thats what came out of Swarm, and the originals are kept that way outside of git (in `DEV/roccat-assets-original/`, where all the tools above write to). What ends up in this folder and in the build is webp, made by `tools/assets/to-webp.py` (lossy quality 80 by default, the see-through part always lossless). The pngs made the binary about 50 MB, the webp about 9. After adding or changing pictures:

```bash
tools/assets/to-webp.py            # or -q 90, or --lossless
```
