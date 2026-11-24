# Swarm era devices rockcat can only name

Code: `src/lib/families/swarm_ident.cpp`, family `swarm-ident`. Every device below is in the table with its product id (from Swarms module list, `MODULES.md`), its name and its kind, so `rockcat-cli list` and the gui recognise it and udev lets it open them. Thats all. Firmware shows bcdDevice, every other call answers "unsupported", nothing gets sent to the device. `ROCKCAT_SIM=vulcan-ii-mini-air` is a pretend one that answers nothing, to see how the gui copes.

None of these got the same treatment as [vulcan.md](vulcan.md) or [kone-aimo-original.md](kone-aimo-original.md) yet. What follows is what turned up along the way, as a head start, NOT checked.

## Where the protocol lives in Swarm

The module dlls (Swarms `Modules/*` and `Turtlebeach-Modules/*`) import no hid functions at all, they are the gui. All usb traffic is in `ROCCAT_Swarm_Monitor.exe`, one class per device: `kova_aimo_device_c`, `kone_emp_device_c`, `leadr_device_c`, `horde_aimo_device_c`, `isku_plus_device_c`, `suora_fx_device_c`, `kone_pure_se_device_c`, `kone_pure_sel_device_c`, `kone_xp_device_c`, `kone_pro_wired_device_c`, `kone_pro_air_device_c`, `burst_pro_air_device_c`, `kain_100_device_c`, `kain_120_device_c`, `kain_200_device_c`, `aimo_pad_device_c`, `aimo_pad_wide_device_c`, `khan_aimo_device_c`, `elo_usb_device_c`, `syn_pro_air_device_c`, `syn_max_air_device_c`, `magma_device_c`, `pyrox_device_c`, `vulcan_pro_device_c`, `vulcan_tkl_device_c`, `vulcan2_mk_device_c`, `vulcan2_max_device_c`, `vulcan2_mini_device_c`, `core_kb_base_device_c`, and more (`strings` on the exe, the names are RTTI). They all write through `hid_send_feature_report` / `hid_get_feature_report` / `hid_write`, exported by name from the exe (it has hidapi built in, with a `hid_open` that also matches usage page and usage).

`USBCmdLib.dll`, which the newer modules ship (2DD2, AIMO_PAD, KAIN_100/120, KONE_PURE_SE/SEL, SUORA_FX), is NOT a second protocol. It exports `ISP_ResetToIAP`, `ISP_EraseMass`, `ISP_WriteProgramB`, `ISP_CRCCheck`, `CRYPT_SetCommandKey` and friends and is loaded by Swarms `firmware_upgrade.dll`: it is the vendors firmware flasher (those modules also carry a `Command_Key.bin` next to the firmware). Settings on those devices go through their monitor class like everywhere else.

Default profiles are Qt resources. The monitor has `config/<device>/...` (`tools/re/qrcfind.py` misses them because they hold no png, see vulcan.md for the trick), the modules have their own `*_config/default.dat`. Both are QSettings files where every value is a QDataStream list of raw reports, which hands you report ids and sizes for free.

## Per device

| device | pid | kind | what turned up |
|---|---|---|---|
| Vulcan II Mini Air | `3aa9`, `6978` | keyboard | wireless with a dongle, monitor `config/vulcan2_mini_air/` and a lot of `pre_check_*` logging around the dongle |
| Isku+ | `31e2` | keyboard | module resources `isku_plus_config/` |
| Isku+ Force FX | `3214` | keyboard | module `ISKU+_FORCE_FX` |
| Kova AIMO | `2cf1`, `2cf2`, `2cf3` | mouse | module `KOVA_AIMO.dll`, firmware `Kova_Aimo_Black_V1.03.bin` in the module (24 KiB). Monitor classes `kova_aimo_device_c`, `kova_aimo_gray_device_c` |
| Kone EMP | `2e24` | mouse | module `kone_emp.dll`, monitor `kone_emp_device_c` |
| Kone Pure Owl-Eye | `2dcd` | mouse | module `kone_pure_owl-eye.dll` |
| Kone Pure SE | `2dca` | mouse | monitor `kone_pure_se_device_c` |
| Kone Pure SEL | `2dcb` | mouse | monitor `kone_pure_sel_device_c`, firmware `kone_pure_sel_V1.02.bin` |
| Kone Pure Ultra | `2dd2`, iCafe `2dd7` | mouse | module `2DD2.dll` |
| Leadr | `2e4c`, `2e4e` | mouse | wireless, monitor `leadr_device_c`, `leadr_stand_device_c`, `config/leadr/LEADR` |
| Kone Pro / Pro Air | `2c88` / `2c8e`, `2c92` | mouse | monitor `kone_pro_wired_device_c`, `kone_pro_air_device_c`, `config/kone_pro_air/KPA_battery.ini` |
| Kone XP / XP Air | `2c8b` / `2cb2`, `2cb6` | mouse | monitor `kone_xp_device_c`, `kone_xp_air_device_c`, `config/kone_xp_air/kone_xp_air_default_profile.dat` |
| Burst Core / Pro / Pro Air | `2de6` / `2de1` / `2ca6`, `2cab` | mouse | monitor `config/burst/` (`Burst_default_profile.dat`, `Burst_Core_dpi.ini`) |
| Kain 100 / 120 / 200 | `2d00` / `2d5c` / `2d5f`, `2d60` | mouse | monitor `config/kain/kain_dpi.ini` |
| Kone Air | `afc9`, `afca` | mouse | monitor `kone_air_device_c` |
| Pure Air | `2ccc`, `3c6e` | mouse | monitor `pure_air_device_c` |
| Pure SEL | `2cc8` | mouse | monitor `pure_sel_device_c` |
| Sense AIMO / XXL | `343a` / `343b` | pad | monitor `aimo_pad_device_c`, `aimo_pad_wide_device_c` |
| Kone XP Air dock | `2cb8` | pad | the charging dock of the XP Air |
| Khan AIMO | `39d0` | headset | monitor `config/khan_aimo/default_profile.dat`, `eq.ini` |
| Elo 7.1 USB | `3a34` | headset | monitor `elo_usb_device_c`, `cmedia_device_c` (a C-Media sound chip) |
| Elo 7.1 Air | `3a37`, `3a39` | headset | probably monitor `ca300rx_device_c` / `ca300tx_device_c` (receiver and headset, GUESS from the CA300 battery pictures being the Elo Air ones) |
| Syn Pro Air / Max Air | `3a3b`, `3a3d` / `3a3f`, `3a41` | headset | monitor `config/syn_series/` |
| Torch | `3a56` | microphone | module `TORCH`, filed as a headset because there is no microphone kind |
