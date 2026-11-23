// every device we know. this table is the single source of truth, the udev rules and the support list get generated from it

#include "devices.h"

#include <utility>
#include <vector>
#include "keyboards.h"

namespace {

constexpr uint32_t bit(int n) { return 1u << n; }

// 5 profiles, 12 buttons on two layers, 2 lights, 5 dpi levels of 250 to 7000 in steps of 50 (one value, no separate x/y)
// effect speed is 1 to 3 in the vendor tools, a real mouse had 0 in a profile nobody touched, so 0 has to be allowed
// colour flow (the smooth one) is drawn by the engine. the mouse has "fully lit" stored meanwhile, so without the engine
// it simply shows its colours
#define KOVA_CAPS(tested, firmware)                                                                                    \
    {ROCKCAT_KIND_MOUSE, "kova", tested, firmware,                                                                    \
     ROCKCAT_HAS_DPI | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB | ROCKCAT_HAS_DIRECT_LIGHT,        \
     5, 12, 2, 2, 5, 250, 7000, 50, {125, 250, 500, 1000, 0},                                                          \
     bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_COLOR_FLOW), \
     bit(ROCKCAT_EFFECT_COLOR_FLOW), 0, 3, ROCKCAT_EFFECT_FULLY_LIT}

const device_desc DEVICES[] = {
    {ROCKCAT_PID_KONE_AIMO_REMASTERED, "ROCCAT Kone AIMO Remastered", &KONE_AIMO_FAMILY, KONE_AIMO_LEDS, 0x06,
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
          bit(ROCKCAT_EFFECT_HEARTBEAT_2) | bit(ROCKCAT_EFFECT_AIMO) | bit(ROCKCAT_EFFECT_COLORWAVE),
      // these four the firmware cant draw (or draws wrong), the engine does them
      bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_BREATHING_2) | bit(ROCKCAT_EFFECT_HEARTBEAT_2) | bit(ROCKCAT_EFFECT_AIMO),
      1, 11, ROCKCAT_EFFECT_AIMO}},
    // tested = the gray one on the desk here. white and black are the same mouse in another shell as far as roccat-tools is concerned,
    // but nobody here has held one
    {ROCKCAT_PID_KOVA_2016_GRAY, "ROCCAT Kova 2016 Gray", &KOVA_FAMILY, nullptr, 0x06, KOVA_CAPS(ROCKCAT_TESTED_HARDWARE, "1.18")},
    {ROCKCAT_PID_KOVA_2016_WHITE, "ROCCAT Kova 2016 White", &KOVA_FAMILY, nullptr, 0x06, KOVA_CAPS(ROCKCAT_TESTED_NO, "")},
    {ROCKCAT_PID_KOVA_2016_BLACK, "ROCCAT Kova 2016 Black", &KOVA_FAMILY, nullptr, 0x06, KOVA_CAPS(ROCKCAT_TESTED_NO, "")},
    // 5 profiles. inputs and leds are both numbered like Swarms key matrix: 120 slots plus the three thumb keys behind a gap,
    // only the 120 have a led. three layers: normal, Easy-Shift, and Fn (which only F1 to Pause have). effects: six the keyboard
    // draws by itself, and six the engine draws (engine.cpp, kb_effects.cpp)
    {ROCKCAT_PID_RYOS_MK_FX, "ROCCAT Ryos MK FX", &RYOS_FAMILY, nullptr, 0x06,
     {ROCKCAT_KIND_KEYBOARD, "ryos", ROCKCAT_TESTED_HARDWARE, "1.31",
      ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_DIRECT_LIGHT | ROCKCAT_HAS_RGB,
      5, 124, 3, 120,
      0, 0, 0, 0,
      {0},
      bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) |
          bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_FADE) | bit(ROCKCAT_EFFECT_WAVE) | bit(ROCKCAT_EFFECT_RIPPLE) |
          bit(ROCKCAT_EFFECT_HEATMAP) | bit(ROCKCAT_EFFECT_EQUALIZER) | bit(ROCKCAT_EFFECT_FIRE) | bit(ROCKCAT_EFFECT_DRIP),
      bit(ROCKCAT_EFFECT_WAVE) | bit(ROCKCAT_EFFECT_RIPPLE) | bit(ROCKCAT_EFFECT_HEATMAP) | bit(ROCKCAT_EFFECT_EQUALIZER) |
          bit(ROCKCAT_EFFECT_FIRE) | bit(ROCKCAT_EFFECT_DRIP),
      1, 3,
      // fire and drip are ours, the keyboard has no number for them. it gets told "game preset" (one of the five that
      // make it report key presses and show nothing itself) and which effect it really is stays on the pc
      ROCKCAT_EFFECT_GAME_PRESET},
     // measured on a real one: it answers a select right away, with the right data, on every profile. reading a profile takes
     // 0.2 s that way instead of 1.5
     &RYOS_MK_FX_KEYBOARD, 2},
};

} // namespace

// the tables the family files registered, after the one above. function local so it exists before the first registration
static std::vector<std::pair<const device_desc *, int>> &registered()
{
    static std::vector<std::pair<const device_desc *, int>> tables;
    return tables;
}

device_registration::device_registration(const device_desc *table, int count)
{
    registered().push_back({table, count});
}

const device_desc *device_at(int i)
{
    const int own = static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]);
    if (i < 0)
        return nullptr;
    if (i < own)
        return &DEVICES[i];
    i -= own;
    for (const auto &t : registered()) {
        if (i < t.second)
            return &t.first[i];
        i -= t.second;
    }
    return nullptr;
}

const device_desc *device_find(uint16_t vid, uint16_t pid)
{
    if (vid != ROCKCAT_VID)
        return nullptr;
    for (int i = 0; const device_desc *d = device_at(i); ++i)
        if (d->pid == pid)
            return d;
    return nullptr;
}

const device_desc *device_of(const rockcat_caps *caps)
{
    for (int i = 0; const device_desc *d = device_at(i); ++i)
        if (&d->caps == caps)
            return d;
    return nullptr;
}
