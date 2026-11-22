// every device we know. this table is the single source of truth, the udev rules and the support list get generated from it

#include "devices.h"

namespace {

constexpr uint32_t bit(int n) { return 1u << n; }

const device_desc DEVICES[] = {
    {ROCKCAT_PID_KONE_AIMO_REMASTERED, "ROCCAT Kone AIMO Remastered", &KONE_AIMO_FAMILY, KONE_AIMO_LEDS,
     {ROCKCAT_KIND_MOUSE, "kone-aimo",
      // upstream ran it on a real mouse with firmware 1.05. nothing since the fork has seen one, so replay is all we can honestly claim
      ROCKCAT_TESTED_REPLAY, "1.05 (upstream, before the fork)",
      ROCKCAT_HAS_DPI | ROCKCAT_HAS_DPI_XY | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_CALIBRATION |
          ROCKCAT_HAS_DIRECT_LIGHT | ROCKCAT_HAS_LED_BRIGHTNESS | ROCKCAT_HAS_RGB | ROCKCAT_HAS_DEBOUNCE | ROCKCAT_HAS_ANGLE_SNAP,
      5, 12, 2, 11,
      5, 100, 16000, 50,
      {125, 250, 500, 1000, 0},
      bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) |
          bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_COLOR_CYCLE) | bit(ROCKCAT_EFFECT_SNAKE) | bit(ROCKCAT_EFFECT_BREATHING_2) |
          bit(ROCKCAT_EFFECT_HEARTBEAT_2) | bit(ROCKCAT_EFFECT_AIMO) | bit(ROCKCAT_EFFECT_COLORWAVE)}},
};

} // namespace

const device_desc *device_find(uint16_t vid, uint16_t pid)
{
    if (vid != ROCKCAT_VID)
        return nullptr;
    for (const device_desc &d : DEVICES)
        if (d.pid == pid)
            return &d;
    return nullptr;
}

const device_desc *device_at(int i)
{
    return i >= 0 && i < static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]) ? &DEVICES[i] : nullptr;
}

const device_desc *device_of(const rockcat_caps *caps)
{
    for (const device_desc &d : DEVICES)
        if (&d.caps == caps)
            return &d;
    return nullptr;
}
