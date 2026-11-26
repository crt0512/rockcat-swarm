// the mice that talk like the Kova 2016 but have their own report sizes: Nyth, Tyon, Kiro, Savu. a settings report on 06
// and a button report on 07, each read after a select on 04, macros on 08, firmware on 09. what differs is where things sit,
// so this is one profile class and a table per mouse (LAYOUTS). the Kova 2016 itself stays in kova.cpp, its code is the one
// that was checked on a real mouse and it doesnt get touched for this. everything here is roccat-tools, only the Nyth (black,
// firmware 1.38) was on real hardware so far.
// docs/protocol/classic.md

#include "../internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

#include "classic_tables.inc"

constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_SETTINGS = 0x06, REPORT_BUTTONS = 0x07, REPORT_MACRO = 0x08, REPORT_INFO = 0x09;

enum macro_kind : uint8_t { MACRO_NONE, MACRO_HALVES, MACRO_SINGLE };
// how the active profile report looks: [05 03 index], [05 04 index count], or there is only the one profile
enum profile_kind : uint8_t { PROFILE_3, PROFILE_4, PROFILE_ONE };
// how the light gets told what it shows. EFFECT_BYTE: a byte with our own effect numbers (0 off, 1 fully lit, 2 blinking,
// 3 breathing, 4 heartbeat). SAVU_MASK: bit 1 of the lights byte is breathing, there is nothing else
enum effect_kind : uint8_t { EFFECT_BYTE, SAVU_MASK };

struct extra_part {
    uint8_t id, request;
    uint16_t size;
};

struct layout {
    uint16_t pid;
    const char *family;
    uint8_t sel_settings, sel_buttons; // 0 = no select, the report is just read
    uint16_t settings_size;
    int settings_sum_at; // where the 16 bit checksum of the settings report sits, -1 = none
    int profile_at;      // the profile byte in settings and buttons, -1 = none
    uint16_t buttons_size; // 0 = the buttons live in the settings report
    int buttons_sum_at;
    int buttons_at;      // first button, 3 bytes each, normal layer then Easy-Shift
    int inputs, layers;
    extra_part extra;    // a report that only gets carried along (size 0 = none)
    macro_kind macros;
    profile_kind profiles;
    uint8_t info_size;
    // dpi: levels at dpi_at, dpi_bytes wide, times dpi_unit. fixed = the mouse only has these values and picks one by index
    int dpi_levels, dpi_at, dpi_bytes, dpi_unit, dpi_min, dpi_max, dpi_step, dpi_enabled_at, dpi_active_at;
    const int *fixed;
    int polling_at, polling_mask;
    int handed_at, handed_bit; // -1 = no such thing
    // lights: index r g b (and maybe a spare byte) each, enabled bits at lights_on_at (bit = led), custom colour bit per
    // led from custom_bit on, or custom_bit_all for all of them at once, or index 0x10 = custom (Savu)
    int leds, light_at, light_stride, lights_on_at, custom_bit, custom_bit_all;
    bool custom_index;
    effect_kind effects;
    int effect_at, speed_at, flow_at, flow_bit; // flow_bit >= 0: its a bit in the byte at flow_at and not a byte of its own
    const uint8_t *factory_settings, *factory_buttons;
    const uint8_t *factory_types; // type byte per input when there is no whole factory button report
    const char *const *input_names;
    const char *const *led_names;
    // a separate y per level at dpi_y_at (-1 = x is all there is), the bytes count from dpi_offset (Kone EMP: 3 = 400 dpi),
    // and the light index that means "take the rgb" (custom_index)
    int dpi_y_at = -1, dpi_offset = 0;
    uint8_t custom_value = 0x10;
};

constexpr int SAVU_DPI[4] = {400, 800, 1600, 4000};

const char *const NYTH_INPUTS[20] = {"Left button", "Right button", "Middle button", "Wheel up", "Wheel down", "Top button",
                                     "Fin left",    "Fin right",    "Side 1",        "Side 2",   "Side 3",     "Side 4",
                                     "Side 5",      "Side 6",       "Side 7",        "Side 8",   "Side 9",     "Side 10",
                                     "Side 11",     "Side 12"};
const char *const TYON_INPUTS[16] = {"Left button",   "Right button",  "Middle button", "Thumb back",     "Thumb forward",
                                     "Thumb pedal",   "Paddle up",     "Paddle down",   "Left back",      "Left forward",
                                     "Right back",    "Right forward", "Fin right",     "Fin left",       "Wheel up",
                                     "Wheel down"};
const char *const KIRO_INPUTS[10] = {"Left button",    "Right button", "Middle button", "Left forward", "Left backward",
                                     "Right forward",  "Right backward", "Top button",  "Wheel up",     "Wheel down"};
const char *const SAVU_INPUTS[7] = {"Left button", "Right button", "Middle button", "Forward", "Backward", "Wheel up", "Wheel down"};
const char *const TWO_LIGHTS[2] = {"Logo", "Bottom"};
const char *const TYON_LIGHTS[2] = {"Wheel", "Bottom"};
const char *const ONE_LIGHT[1] = {"Logo"};

// Tyon and Savu ship their factory buttons as rmp files, these are the type bytes out of those
constexpr uint8_t TYON_TYPES[32] = {0x01, 0x02, 0x03, 0x08, 0x07, 0x41, 0x82, 0x83, 0x16, 0x15, 0x8a, 0x8b, 0x09, 0x0a, 0x0d, 0x0e,
                                    0x8c, 0x8d, 0x25, 0x06, 0x06, 0x00, 0x82, 0x83, 0x12, 0x11, 0x89, 0x88, 0x86, 0x87, 0x26, 0x27};
constexpr uint8_t SAVU_TYPES[14] = {0x01, 0x02, 0x03, 0x07, 0x41, 0x0d, 0x0e, 0x08, 0x07, 0x25, 0x00, 0x06, 0x26, 0x27};

// Tyon and Savu have no default report in roccat-tools either, made from their default rmp values: dpi 800 1600 3200
// 6400 8200 (Tyon, its rmp counts in 200s, the report in 50s) / all four (Savu), 1000 Hz, lights on in roccat blue, breathing, colour flow
constexpr uint8_t TYON_SETTINGS[30] = {0x06, 0x1e, 0x00, 0x00, 0x06, 0x06, 0x1f, 0x10, 0x20, 0x40, 0x80, 0xa4, 0x02, 0x03, 0x13,
                                       0x01, 0x03, 0x02, 0x01, 0x00, 0x64, 0xfa, 0x00, 0x01, 0x00, 0x64, 0xfa, 0x00, 0x00, 0x00};
constexpr uint8_t SAVU_GENERAL[16] = {0x06, 0x10, 0x00, 0x00, 0x06, 0x06, 0x0f, 0x01, 0x03, 0x05, 0x10, 0x00, 0x71, 0xfd, 0x00, 0x00};

//                                pid    family     sel   sel   size sum  prof  bsize bsum bat  in  lay  extra                 macros        profiles     info
const layout NYTH = {0x2e7c, "nyth", 0x80, 0x90, 33, 31, 2, 123, -1, 3, 20, 2, {0x0a, 0xa0, 30}, MACRO_HALVES, PROFILE_4, 6,
                     // dpi levels, at, bytes, unit, min, max, step, enabled, active, fixed
                     5, 7, 2, 1, 200, 12000, 10, 6, 17, nullptr,
                     // polling, handed
                     18, 0xff, -1, -1,
                     // leds, at, stride, on, custom bit, all, index, effects, effect, speed, flow, flow bit
                     2, 23, 4, 19, 4, -1, false, EFFECT_BYTE, 21, 22, 20, -1, FACTORY_NYTH_SETTINGS, FACTORY_NYTH_BUTTONS, nullptr,
                     NYTH_INPUTS, TWO_LIGHTS};
const layout TYON = {0x2e4a, "tyon", 0x80, 0x90, 30, 28, 2, 99, -1, 3, 16, 2, {0, 0, 0}, MACRO_SINGLE, PROFILE_3, 8,
                     5, 7, 1, 50, 200, 8200, 200, 6, 12, nullptr,
                     13, 0x0f, -1, -1,
                     2, 18, 5, 14, -1, 4, false, EFFECT_BYTE, 16, 17, 15, -1, TYON_SETTINGS, nullptr, TYON_TYPES,
                     TYON_INPUTS, TYON_LIGHTS};
// one report for everything, no select, no profile byte, the checksum after the buttons and 13 bytes of nothing after that
const layout KIRO = {0x2c38, "kiro", 0, 0, 65, 50, -1, 0, -1, 20, 10, 1, {0, 0, 0}, MACRO_NONE, PROFILE_ONE, 7,
                     5, 4, 1, 50, 250, 4000, 50, 3, 9, nullptr,
                     10, 0x0f, 10, 6,
                     1, 15, 4, 11, 2, -1, false, EFFECT_BYTE, 13, 14, 12, -1, FACTORY_KIRO_PROFILE, nullptr, nullptr,
                     KIRO_INPUTS, ONE_LIGHT};
// four fixed dpi values, the active one is an index, the light byte is on / breathing / colour flow in one
const layout SAVU = {0x2d5a, "savu", 0x80, 0x90, 16, 14, 2, 47, 45, 3, 7, 2, {0, 0, 0}, MACRO_NONE, PROFILE_3, 8,
                     4, -1, 1, 1, 400, 4000, 0, 6, 7, SAVU_DPI,
                     8, 0xff, -1, -1,
                     1, 10, 4, 9, -1, -1, true, SAVU_MASK, -1, -1, 9, 2, SAVU_GENERAL, nullptr, SAVU_TYPES,
                     SAVU_INPUTS, ONE_LIGHT};

// the Kone EMP has no Swarm default we could find and isnt in roccat-tools. everything below is from Swarms monitor
// (kone_emp_device_c) and module (kone_emp.dll, the reset function FUN_6c123250 / FUN_6c119080 sets every byte), see the doc
const char *const EMP_INPUTS[11] = {"Left button", "Right button", "Middle button", "Side front", "Side back", "Tilt left",
                                    "Tilt right",  "Wheel up",     "Wheel down",    "Top front", "Top back"};
const char *const FOUR_LIGHTS[4] = {"Light 1", "Light 2", "Light 3", "Light 4"};
// factory settings out of the reset functions: dpi 400 800 1200 1600 3200 (x and y), level 2, 1000 Hz, four lights roccat
// blue, colour flow, breathing, speed 2
constexpr uint8_t EMP_SETTINGS[41] = {0x06, 0x29, 0x00, 0x00, 0x06, 0x06, 0x1f, 0x03, 0x07, 0x0b, 0x0f, 0x1f, 0x03, 0x07,
                                      0x0b, 0x0f, 0x1f, 0x01, 0x03, 0x0f, 0x01, 0x03, 0x02, 0x01, 0x00, 0x64, 0xfa, 0x01,
                                      0x00, 0x64, 0xfa, 0x01, 0x00, 0x64, 0xfa, 0x01, 0x00, 0x64, 0xfa, 0x00, 0x00};
const layout EMP = {0x2e24, "kone-emp", 0x80, 0x90, 41, 39, 2, 71, 69, 3, 11, 2, {0, 0, 0}, MACRO_HALVES, PROFILE_4, 8,
                    // dpi levels, at, bytes, unit, min, max, step, enabled, active, fixed
                    5, 7, 1, 100, 100, 12000, 100, 6, 17, nullptr,
                    18, 0xff, -1, -1,
                    4, 23, 4, 19, -1, -1, true, EFFECT_BYTE, 21, 22, 20, -1, EMP_SETTINGS, nullptr, nullptr, EMP_INPUTS, FOUR_LIGHTS,
                    12, 1, 0x1e};

// Kone Pure Owl-Eye: the EMP layout with one light and the Kone Pure buttons. Swarm monitor class kone_pure_owl_eye_device_c
// (settings 0x23, buttons 0x3b, halves macros) and module kone_pure_owl-eye.dll (reset FUN_6b798ef0 / FUN_6b7a30c0, colour
// setter FUN_6b7ad610: rgb at 0x1a and bit 4 of byte 0x15 for "own colour")
const char *const OWL_INPUTS[9] = {"Left button", "Right button", "Middle button", "Side front", "Side back",
                                   "Wheel up",    "Wheel down",   "Top front",     "Top back"};
constexpr uint8_t OWL_SETTINGS[35] = {0x06, 0x23, 0x00, 0x00, 0x06, 0x06, 0x1f, 0x03, 0x07, 0x0b, 0x0f, 0x1f,
                                      0x03, 0x07, 0x0b, 0x0f, 0x1f, 0x01, 0x03, 0x00, 0x00, 0x01, 0x01, 0x03,
                                      0x02, 0x01, 0x00, 0x64, 0xfa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const layout OWL = {0x2dcd, "kone-pure-owl-eye", 0x80, 0x90, 35, 33, 2, 59, 57, 3, 9, 2, {0, 0, 0}, MACRO_HALVES, PROFILE_4, 8,
                    5, 7, 1, 100, 100, 12000, 100, 6, 17, nullptr,
                    18, 0xff, -1, -1,
                    1, 25, 4, 21, 4, -1, false, EFFECT_BYTE, 23, 24, 22, -1, OWL_SETTINGS, nullptr, nullptr, OWL_INPUTS, ONE_LIGHT,
                    12, 1, 0x10};

// Leadr: a Tyon without the cable (same buttons, 16 on two layers, 99 bytes) with the Owl-Eye settings and two lights.
// monitor leadr_device_c (settings 0x23 with checksum at 0x21 in FUN_004f6610, buttons 99 x 5 and 32 macro slots in
// FUN_004f61d0), module leadr.dll (reset FUN_6b319160 / FUN_6b323510, colour setter FUN_6b32de60)
constexpr uint8_t LEADR_SETTINGS[35] = {0x06, 0x23, 0x00, 0x01, 0x06, 0x06, 0x1f, 0x03, 0x07, 0x0b, 0x0f, 0x1f,
                                        0x03, 0x07, 0x0b, 0x0f, 0x1f, 0x01, 0x03, 0x00, 0x00, 0x03, 0x01, 0x03,
                                        0x02, 0x01, 0x00, 0x64, 0xfa, 0x01, 0x00, 0x64, 0xfa, 0x00, 0x00};
const layout LEADR = {0x2e4c, "leadr", 0x80, 0x90, 35, 33, 2, 99, -1, 3, 16, 2, {0, 0, 0}, MACRO_HALVES, PROFILE_4, 8,
                      5, 7, 1, 100, 100, 12000, 100, 6, 17, nullptr,
                      18, 0xff, -1, -1,
                      2, 25, 4, 21, 4, -1, false, EFFECT_BYTE, 23, 24, 22, -1, LEADR_SETTINGS, nullptr, nullptr, TYON_INPUTS,
                      TYON_LIGHTS, 12, 1, 0x10};

// Kova AIMO: the Kova 2016 settings report grown to 37 bytes, its lights [brightness r g b] instead of [index r g b].
// monitor kova_aimo_device_c (settings 0x25, buttons 0x4b like the Kova 2016, halves macros), module KOVA_AIMO (reset
// FUN_65f12c70 dpi, FUN_65f4a440 lights, FUN_65f4eed0 / FUN_65f4edd0 effect and speed)
const char *const KOVA_INPUTS[12] = {"Left button",   "Right button",  "Middle button",    "Left Smart Cast", "Left forward",
                                     "Left backward", "Right Smart Cast", "Right forward", "Right backward",  "Top button",
                                     "Wheel up",      "Wheel down"};
const char *const KOVA_LIGHTS[2] = {"Light pipe", "Wheel"};
// the Kova 2016 factory buttons (same shell, GUESS that the firmware agrees)
constexpr uint8_t KOVA_TYPES[24] = {0x01, 0x02, 0x03, 0x64, 0xd0, 0x41, 0x65, 0x67, 0x66, 0x14, 0x0d, 0x0e,
                                    0x08, 0x07, 0x25, 0x21, 0x23, 0x00, 0x22, 0x62, 0x63, 0x10, 0x26, 0x27};
constexpr uint8_t KOVA_AIMO_SETTINGS[37] = {0x06, 0x25, 0x00, 0x00, 0x06, 0x06, 0x1f, 0x08, 0x10, 0x20, 0x46, 0x8c, 0x01,
                                            0x03, 0x33, 0x00, 0x09, 0x02, 0xff, 0x00, 0x64, 0xfa, 0xff, 0x00, 0x64, 0xfa,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const layout KOVA_AIMO = {0x2cf1, "kova-aimo", 0x80, 0x90, 37, 35, 2, 75, -1, 3, 12, 2, {0, 0, 0}, MACRO_HALVES, PROFILE_4, 8,
                          5, 7, 1, 50, 100, 7000, 50, 6, 12, nullptr,
                          13, 0xff, -1, -1,
                          2, 18, 4, 14, 4, -1, false, EFFECT_BYTE, 16, 17, 15, -1, KOVA_AIMO_SETTINGS, nullptr, KOVA_TYPES,
                          KOVA_INPUTS, KOVA_LIGHTS};

const layout &layout_of(uint16_t pid)
{
    switch (pid) {
    case 0x2e4a:
    case 0x2e4b: return TYON;
    case 0x2c38: return KIRO;
    case 0x2d5a: return SAVU;
    case 0x2e24: return EMP;
    case 0x2dcd: return OWL;
    case 0x2cf1:
    case 0x2cf2:
    case 0x2cf3: return KOVA_AIMO;
    case 0x2e4c:
    case 0x2e4e: return LEADR;
    default:     return NYTH;
    }
}

uint16_t sum16(const uint8_t *b, int n)
{
    uint16_t s = 0;
    for (int i = 0; i < n; ++i)
        s = static_cast<uint16_t>(s + b[i]);
    return s;
}

struct classic_profile final : rockcat_profile {
    const layout &L;
    std::vector<uint8_t> settings, buttons, extra;
    bool settings_dirty = false, buttons_dirty = false, extra_dirty = false;
    std::vector<rockcat_macro> macros;

    classic_profile(const device_desc *d, int i)
        : rockcat_profile(d, i), L(layout_of(d->pid)), settings(L.settings_size), buttons(L.buttons_size), extra(L.extra.size)
    {
    }

    rockcat_profile *clone() const override { return new classic_profile(*this); }

    // the buttons are in their own report or in the settings one (Kiro)
    uint8_t *btn() { return L.buttons_size ? buttons.data() : settings.data(); }
    const uint8_t *btn() const { return L.buttons_size ? buttons.data() : settings.data(); }

    static bool sum_ok(const std::vector<uint8_t> &b, int at)
    {
        return at < 0 || get16(b.data() + at) == sum16(b.data(), at);
    }
    static void seal(std::vector<uint8_t> &b, int at)
    {
        if (at >= 0)
            put16(b.data() + at, sum16(b.data(), at));
    }

    int fetch(rockcat_device *dev, std::vector<uint8_t> &b, uint8_t id, uint8_t request)
    {
        if (request) {
            int r = select_data(dev, static_cast<uint8_t>(index), request);
            if (r != ROCKCAT_OK)
                return r;
        }
        std::fill(b.begin(), b.end(), 0);
        b[0] = id;
        return get_raw(dev, b.data(), b.size());
    }

    int read(rockcat_device *dev) override
    {
        settings_dirty = buttons_dirty = extra_dirty = false;
        macros.clear();
        int r = fetch(dev, settings, REPORT_SETTINGS, L.sel_settings);
        if (r != ROCKCAT_OK)
            return r;
        if ((L.profile_at >= 0 && settings[L.profile_at] != index))
            return ROCKCAT_ERR_PROTOCOL;
        if (!sum_ok(settings, L.settings_sum_at))
            return ROCKCAT_ERR_CHECKSUM;
        if (L.buttons_size) {
            r = fetch(dev, buttons, REPORT_BUTTONS, L.sel_buttons);
            if (r != ROCKCAT_OK)
                return r;
            if (buttons[1] != (L.buttons_size & 0xff) || (L.profile_at >= 0 && buttons[L.profile_at] != index))
                return ROCKCAT_ERR_PROTOCOL;
            if (!sum_ok(buttons, L.buttons_sum_at))
                return ROCKCAT_ERR_CHECKSUM;
        }
        if (L.extra.size) {
            r = fetch(dev, extra, L.extra.id, L.extra.request);
            if (r != ROCKCAT_OK)
                return r;
        }
        return ROCKCAT_OK;
    }

    int write(rockcat_device *dev) override
    {
        if (settings_dirty) {
            seal(settings, L.settings_sum_at);
            int r = set_raw(dev, settings.data(), settings.size(), true);
            if (r != ROCKCAT_OK)
                return r;
            settings_dirty = false;
        }
        while (!macros.empty()) {
            int r = L.macros == MACRO_HALVES ? macro_halves_write(dev, REPORT_MACRO, macros.front())
                                              : macro_single_write(dev, REPORT_MACRO, macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        if (buttons_dirty && L.buttons_size) {
            seal(buttons, L.buttons_sum_at);
            int r = set_raw(dev, buttons.data(), buttons.size(), true);
            if (r != ROCKCAT_OK)
                return r;
        }
        buttons_dirty = false;
        if (extra_dirty && L.extra.size) {
            int r = set_raw(dev, extra.data(), extra.size(), true);
            if (r != ROCKCAT_OK)
                return r;
        }
        extra_dirty = false;
        return ROCKCAT_OK;
    }

    int set_index(int i) override
    {
        if (i < 0 || i >= desc->caps.profile_count)
            return ROCKCAT_ERR_ARGUMENT;
        index = i;
        if (L.profile_at >= 0) {
            settings[L.profile_at] = static_cast<uint8_t>(i);
            if (L.buttons_size)
                buttons[L.profile_at] = static_cast<uint8_t>(i);
            if (L.extra.size)
                extra[2] = static_cast<uint8_t>(i);
        }
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        settings_dirty = buttons_dirty = extra_dirty = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        classic_profile copy(*this);
        seal(copy.settings, L.settings_sum_at);
        seal(copy.buttons, L.buttons_sum_at);
        out.insert(out.end(), copy.settings.begin(), copy.settings.end());
        out.insert(out.end(), copy.buttons.begin(), copy.buttons.end());
        out.insert(out.end(), copy.extra.begin(), copy.extra.end());
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != settings.size() + buttons.size() + extra.size() || buf[0] != REPORT_SETTINGS ||
            (L.buttons_size && buf[settings.size()] != REPORT_BUTTONS))
            return ROCKCAT_ERR_ARGUMENT;
        std::copy(buf, buf + settings.size(), settings.begin());
        std::copy(buf + settings.size(), buf + settings.size() + buttons.size(), buttons.begin());
        std::copy(buf + settings.size() + buttons.size(), buf + len, extra.begin());
        // the extra report goes along with the profile, it gets written too
        return set_index(index);
    }

    int dpi_value(int level) const
    {
        if (L.fixed)
            return L.fixed[level];
        const uint8_t *p = settings.data() + L.dpi_at + L.dpi_bytes * level;
        return ((L.dpi_bytes == 2 ? get16(p) : p[0]) + L.dpi_offset) * L.dpi_unit;
    }

    int get_dpi(int level, int *x, int *y, int *enabled) const override
    {
        if (level < 0 || level >= L.dpi_levels)
            return ROCKCAT_ERR_ARGUMENT;
        if (x)
            *x = dpi_value(level);
        if (y)
            *y = dpi_value(level);
        if (enabled)
            *enabled = (settings[L.dpi_enabled_at] >> level) & 1;
        return ROCKCAT_OK;
    }

    int set_dpi(int level, int x, int y, int enabled) override
    {
        if (level < 0 || level >= L.dpi_levels || x != y)
            return ROCKCAT_ERR_ARGUMENT;
        if (L.fixed) {
            if (x != L.fixed[level])
                return ROCKCAT_ERR_ARGUMENT;
        } else {
            if (x < L.dpi_min || x > L.dpi_max || (L.dpi_step && x % L.dpi_step))
                return ROCKCAT_ERR_ARGUMENT;
            uint8_t *p = settings.data() + L.dpi_at + L.dpi_bytes * level;
            const int raw = x / L.dpi_unit - L.dpi_offset;
            if (L.dpi_bytes == 2)
                put16(p, static_cast<uint16_t>(raw));
            else
                p[0] = static_cast<uint8_t>(raw);
            // one sensor setting for both, like the vendor tools write it
            if (L.dpi_y_at >= 0)
                settings[static_cast<size_t>(L.dpi_y_at + level)] = static_cast<uint8_t>(raw);
        }
        uint8_t &mask = settings[L.dpi_enabled_at];
        mask = static_cast<uint8_t>(enabled ? mask | (1 << level) : mask & ~(1 << level));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_effect() const
    {
        if (L.effects == SAVU_MASK) {
            const uint8_t m = settings[L.lights_on_at];
            return !(m & 1) ? ROCKCAT_EFFECT_OFF : (m & 2) ? ROCKCAT_EFFECT_BREATHING : ROCKCAT_EFFECT_FULLY_LIT;
        }
        const uint8_t e = settings[L.effect_at];
        return e <= 4 ? static_cast<int>(e) : static_cast<int>(ROCKCAT_EFFECT_FULLY_LIT);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE: *value = settings[L.dpi_active_at]; break;
        case ROCKCAT_SET_POLLING_HZ: *value = rockcat_polling_rate_hz(static_cast<uint8_t>(settings[L.polling_at] & L.polling_mask)); break;
        case ROCKCAT_SET_EFFECT:     *value = get_effect(); break;
        case ROCKCAT_SET_EFFECT_SPEED:
            if (L.speed_at < 0)
                return ROCKCAT_ERR_UNSUPPORTED;
            *value = settings[L.speed_at];
            break;
        case ROCKCAT_SET_COLOR_FLOW:
            *value = L.flow_bit >= 0 ? (settings[L.flow_at] >> L.flow_bit) & 1 : settings[L.flow_at] != 0;
            break;
        case ROCKCAT_SET_LEFT_HANDED:
            if (L.handed_at < 0)
                return ROCKCAT_ERR_UNSUPPORTED;
            *value = (settings[L.handed_at] >> L.handed_bit) & 1;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    void set_bit(int at, int bit, bool on)
    {
        settings[at] = static_cast<uint8_t>(on ? settings[at] | (1 << bit) : settings[at] & ~(1 << bit));
    }

    int set_int(rockcat_setting which, int value) override
    {
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:
            if (value < 0 || value >= L.dpi_levels)
                return ROCKCAT_ERR_ARGUMENT;
            settings[L.dpi_active_at] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_POLLING_HZ: {
            int rate = -1;
            for (int i = 0; i < 4; ++i)
                if (rockcat_polling_rate_hz(static_cast<uint8_t>(i)) == value)
                    rate = i;
            if (rate < 0)
                return ROCKCAT_ERR_ARGUMENT;
            // whatever else lives in that byte (the talk fx switch on the Tyon) stays
            settings[L.polling_at] = static_cast<uint8_t>((settings[L.polling_at] & ~L.polling_mask) | rate);
            break;
        }
        case ROCKCAT_SET_EFFECT:
            if (value < 0 || value > 31 || !(desc->caps.effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            if (L.effects == SAVU_MASK) {
                set_bit(L.lights_on_at, 0, value != ROCKCAT_EFFECT_OFF);
                set_bit(L.lights_on_at, 1, value == ROCKCAT_EFFECT_BREATHING);
            } else {
                settings[L.effect_at] = static_cast<uint8_t>(value);
            }
            break;
        case ROCKCAT_SET_EFFECT_SPEED:
            if (L.speed_at < 0)
                return ROCKCAT_ERR_UNSUPPORTED;
            if (value < 1 || value > 3)
                return ROCKCAT_ERR_ARGUMENT;
            settings[L.speed_at] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_COLOR_FLOW:
            if (L.flow_bit >= 0)
                set_bit(L.flow_at, L.flow_bit, value != 0);
            else
                settings[L.flow_at] = value ? 1 : 0;
            break;
        case ROCKCAT_SET_LEFT_HANDED:
            if (L.handed_at < 0)
                return ROCKCAT_ERR_UNSUPPORTED;
            set_bit(L.handed_at, L.handed_bit, value != 0);
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= L.leds)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = settings.data() + L.light_at + L.light_stride * led;
        if (c)
            *c = rockcat_color{255, l[1], l[2], l[3]};
        if (enabled)
            *enabled = L.effects == SAVU_MASK ? settings[L.lights_on_at] & 1 : (settings[L.lights_on_at] >> led) & 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= L.leds)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *l = settings.data() + L.light_at + L.light_stride * led;
        // no brightness per light on any of these, alpha goes into the colour
        l[1] = static_cast<uint8_t>((c.r * c.a + 127) / 255);
        l[2] = static_cast<uint8_t>((c.g * c.a + 127) / 255);
        l[3] = static_cast<uint8_t>((c.b * c.a + 127) / 255);
        // tell the mouse to take our rgb and not the palette entry in front of it
        if (L.custom_index)
            l[0] = L.custom_value;
        if (L.custom_bit >= 0)
            set_bit(L.lights_on_at, L.custom_bit + led, true);
        if (L.custom_bit_all >= 0)
            set_bit(L.lights_on_at, L.custom_bit_all, true);
        set_bit(L.lights_on_at, L.effects == SAVU_MASK ? 0 : led, enabled != 0);
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int slot(int layer, int input) const
    {
        return layer < 0 || layer >= L.layers || input < 0 || input >= L.inputs ? -1 : layer * L.inputs + input;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *p = btn() + L.buttons_at + 3 * i;
        *a = rockcat_assignment{p[0], p[1], p[2]};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *p = btn() + L.buttons_at + 3 * i;
        p[0] = a->type;
        p[1] = a->param1;
        p[2] = a->param2;
        if (L.buttons_size)
            buttons_dirty = true;
        else
            settings_dirty = true;
        return ROCKCAT_OK;
    }

    rockcat_assignment factory(int i) const
    {
        if (L.factory_types)
            return rockcat_assignment{L.factory_types[i], 0, 0};
        const uint8_t *src = L.factory_buttons ? L.factory_buttons : L.factory_settings;
        const uint8_t *p = src + L.buttons_at + 3 * i;
        return rockcat_assignment{p[0], p[1], p[2]};
    }

    int reset_inputs() override
    {
        // nobody knows the factory buttons of this one
        if (!L.factory_types && !L.factory_buttons && L.buttons_size)
            return ROCKCAT_ERR_UNSUPPORTED;
        for (int i = 0; i < L.inputs * L.layers; ++i) {
            rockcat_assignment a = factory(i);
            uint8_t *p = btn() + L.buttons_at + 3 * i;
            p[0] = a.type;
            p[1] = a.param1;
            p[2] = a.param2;
        }
        if (L.buttons_size)
            buttons_dirty = true;
        else
            settings_dirty = true;
        return ROCKCAT_OK;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        if (!L.factory_types && !L.factory_buttons && L.buttons_size)
            return ROCKCAT_ERR_UNSUPPORTED;
        *a = factory(i);
        return ROCKCAT_OK;
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !m)
            return ROCKCAT_ERR_ARGUMENT;
        if (L.macros == MACRO_NONE)
            return ROCKCAT_ERR_UNSUPPORTED;
        return L.macros == MACRO_HALVES ? macro_halves_read(dev, REPORT_MACRO, index, i, m) : macro_single_read(dev, REPORT_MACRO, index, i, m);
    }

    int set_macro(int layer, int input, const rockcat_macro *m) override
    {
        int i = slot(layer, input);
        if (i < 0 || !m || m->count > ROCKCAT_MACRO_KEYSTROKES)
            return ROCKCAT_ERR_ARGUMENT;
        if (L.macros == MACRO_NONE)
            return ROCKCAT_ERR_UNSUPPORTED;
        rockcat_macro copy = *m;
        copy.profile = static_cast<uint8_t>(index);
        copy.button = static_cast<uint8_t>(i);
        for (rockcat_macro &old : macros)
            if (old.button == copy.button) {
                old = copy;
                return ROCKCAT_OK;
            }
        macros.push_back(copy);
        return ROCKCAT_OK;
    }
};

rockcat_profile *new_profile(const device_desc *desc, int index)
{
    return new classic_profile(desc, index);
}

const layout &of(rockcat_device *dev)
{
    return layout_of(dev->desc ? dev->desc->pid : 0);
}

int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[8] = {REPORT_INFO};
    int r = get_raw(dev, info, of(dev).info_size);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    const layout &L = of(dev);
    if (L.profiles == PROFILE_ONE) {
        if (profile)
            *profile = 0;
        if (count)
            *count = 1;
        return ROCKCAT_OK;
    }
    uint8_t rep[4] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, L.profiles == PROFILE_4 ? 4 : 3);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = rep[2];
    if (count)
        *count = 5;
    return ROCKCAT_OK;
}

int set_active(rockcat_device *dev, uint8_t profile)
{
    const layout &L = of(dev);
    if (L.profiles == PROFILE_ONE)
        return profile == 0 ? ROCKCAT_OK : ROCKCAT_ERR_ARGUMENT;
    if (profile >= 5)
        return ROCKCAT_ERR_ARGUMENT;
    if (L.profiles == PROFILE_3) {
        const uint8_t rep[3] = {REPORT_PROFILE, 0x03, profile};
        return set_raw(dev, rep, sizeof rep, true);
    }
    // the count goes back the way it came
    uint8_t rep[4] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    rep[1] = 0x04;
    rep[2] = profile;
    return set_raw(dev, rep, sizeof rep, true);
}

int no_direct(rockcat_device *)
{
    return ROCKCAT_ERR_UNSUPPORTED;
}
int no_frame(rockcat_device *, const rockcat_color *, int)
{
    return ROCKCAT_ERR_UNSUPPORTED;
}
int no_switch(rockcat_device *, bool)
{
    return ROCKCAT_ERR_UNSUPPORTED;
}
int direct_active(rockcat_device *)
{
    return 0;
}

// only the plain mouse report, like on the Kova
bool decode_event(const uint8_t *in, int n, rockcat_event *ev)
{
    if (in[0] != 0x01 || n < 2)
        return false;
    *ev = rockcat_event{ROCKCAT_EVENT_ACTIVITY, in[1], 0};
    return true;
}

// the labels only get the input number, not the device. the gui and the cli ask through rockcat_input_label with the caps,
// and profile.cpp hands it to us. so each mouse gets its own little set of functions
template <const layout &L> const char *input_label(int input)
{
    return input >= 0 && input < L.inputs ? L.input_names[input] : "?";
}
template <const layout &L> const char *led_label(int led)
{
    return led >= 0 && led < L.leds ? L.led_names[led] : "?";
}
const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : "?";
}

template <const layout &L>
constexpr family_ops ops = {firmware,  get_active,    set_active,   new_profile,     no_direct,      no_frame,  no_direct,
                            no_switch, direct_active, decode_event, input_label<L>, led_label<L>, layer_label};

// the Nyth sensor: tracking control unit (TCU, calibrated to the surface or not) and distance control unit (DCU, the
// lift-off distance) in one report, out of roccat-tools nyth_control_unit.c and nyth_tcu_calibration_assistant.c:
// [0f 06 dcu tcu 00 action]. ff in dcu or tcu = leave that one alone. a calibration is tcu 1 action 1 (start), the
// mouse says its done with the input report [03 xx 30 ..] (roccat-tools waits up to 15 s), then action 3 keeps it and
// action 4 throws it away. tcu 0 action 0 = back to how it came from the factory
constexpr uint8_t REPORT_CONTROL_UNIT = 0x0f, CU_START = 1, CU_ACCEPT = 3, CU_CANCEL = 4;
constexpr uint8_t SPECIAL_REPORT = 0x03, SPECIAL_TCU = 0x30;

int nyth_cu_write(rockcat_device *dev, uint8_t dcu, uint8_t tcu, uint8_t action)
{
    const uint8_t buf[6] = {REPORT_CONTROL_UNIT, 6, dcu, tcu, 0, action};
    return set_raw(dev, buf, sizeof buf, true);
}

int nyth_cu_read(rockcat_device *dev, uint8_t out[6])
{
    std::memset(out, 0, 6);
    out[0] = REPORT_CONTROL_UNIT;
    int r = get_raw(dev, out, 6);
    return r != ROCKCAT_OK ? r : out[0] == REPORT_CONTROL_UNIT ? ROCKCAT_OK : ROCKCAT_ERR_PROTOCOL;
}

// mode 2 = calibrated like on the Kone AIMO, 0 = factory. the Nyth has no number for its calibration, value stays 0
int nyth_tdcu_read(rockcat_device *dev, rockcat_tdcu *out)
{
    uint8_t cu[6];
    int r = nyth_cu_read(dev, cu);
    if (r != ROCKCAT_OK)
        return r;
    *out = rockcat_tdcu{static_cast<uint8_t>(cu[3] == 1 ? 2 : 0), 0, 0};
    return ROCKCAT_OK;
}

int nyth_tdcu_start(rockcat_device *dev)
{
    // whatever input reports are still waiting are from before, the one that counts comes after the start
    uint8_t junk[16];
    for (int i = 0; i < 256 && dev->io->read(junk, sizeof junk, 0) > 0; ++i) {
    }
    return nyth_cu_write(dev, 0xff, 1, CU_START);
}

int nyth_tdcu_poll(rockcat_device *dev, int *finished)
{
    *finished = 0;
    uint8_t in[16];
    for (int i = 0; i < 256; ++i) {
        int n = dev->io->read(in, sizeof in, 0);
        if (n < 0)
            return n;
        if (n == 0)
            break;
        if (n >= 3 && in[0] == SPECIAL_REPORT && in[2] == SPECIAL_TCU)
            *finished = 1;
    }
    return ROCKCAT_OK;
}

int nyth_tdcu_finish(rockcat_device *dev, int save)
{
    return nyth_cu_write(dev, 0xff, 1, save ? CU_ACCEPT : CU_CANCEL);
}

int nyth_tdcu_off(rockcat_device *dev)
{
    return nyth_cu_write(dev, 0xff, 0, 0);
}

int nyth_get_lift_off(rockcat_device *dev, int *level)
{
    uint8_t cu[6];
    int r = nyth_cu_read(dev, cu);
    if (r == ROCKCAT_OK)
        *level = cu[2];
    return r;
}

int nyth_set_lift_off(rockcat_device *dev, int level)
{
    return nyth_cu_write(dev, static_cast<uint8_t>(level), 0xff, 0);
}

constexpr calibration_ops NYTH_CALIBRATION = {nyth_tdcu_read, nyth_tdcu_start, nyth_tdcu_poll, nyth_tdcu_finish,
                                              nyth_tdcu_off,  nyth_get_lift_off, nyth_set_lift_off};
constexpr family_ops nyth_ops = {firmware,  get_active,    set_active,   new_profile,         no_direct,          no_frame,
                                 no_direct, no_switch,     direct_active, decode_event,       input_label<NYTH>, led_label<NYTH>,
                                 layer_label, nullptr,     nullptr,      &NYTH_CALIBRATION};

constexpr uint32_t bit(int n)
{
    return 1u << n;
}
constexpr uint32_t FIVE = bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) |
                          bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_HEARTBEAT);
constexpr uint32_t THREE = bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING);
constexpr uint32_t SAVU_FX = bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BREATHING);

#define CLASSIC_CAPS_TESTED(L, tested, firmware, features, profiles, effects, speed_max)                                          \
    {ROCKCAT_KIND_MOUSE, L.family, tested, firmware, features, profiles, L.inputs, L.layers, L.leds, L.dpi_levels, L.dpi_min,       \
     L.dpi_max, L.dpi_step ? L.dpi_step : 1, {125, 250, 500, 1000, 0}, effects, 0, speed_max ? 1 : 0, speed_max, -1}
#define CLASSIC_CAPS(L, features, profiles, effects, speed_max) CLASSIC_CAPS_TESTED(L, ROCKCAT_TESTED_NO, "", features, profiles, effects, speed_max)

constexpr uint32_t MOUSE = ROCKCAT_HAS_DPI | ROCKCAT_HAS_RGB;
constexpr uint32_t NYTH_SENSOR = ROCKCAT_HAS_CALIBRATION | ROCKCAT_HAS_CALIBRATION_OFF | ROCKCAT_HAS_LIFT_OFF;

const device_desc DEVICES[] = {
    // the black one is on the desk here and does what it should. the white one is the same mouse in another shell
    {0x2e7c, "ROCCAT Nyth Black", &nyth_ops, nullptr, 0x06,
     CLASSIC_CAPS_TESTED(NYTH, ROCKCAT_TESTED_HARDWARE, "1.38", MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | NYTH_SENSOR, 5, FIVE, 3)},
    {0x2e7d, "ROCCAT Nyth White", &nyth_ops, nullptr, 0x06, CLASSIC_CAPS(NYTH, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | NYTH_SENSOR, 5, FIVE, 3)},
    {0x2e4a, "ROCCAT Tyon Black", &ops<TYON>, nullptr, 0x06, CLASSIC_CAPS(TYON, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2e4b, "ROCCAT Tyon White", &ops<TYON>, nullptr, 0x06, CLASSIC_CAPS(TYON, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2c38, "ROCCAT Kiro", &ops<KIRO>, nullptr, 0x06, CLASSIC_CAPS(KIRO, MOUSE, 1, THREE, 3)},
    {0x2e24, "ROCCAT Kone EMP", &ops<EMP>, nullptr, 0x06, CLASSIC_CAPS(EMP, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2dcd, "ROCCAT Kone Pure Owl-Eye", &ops<OWL>, nullptr, 0x06, CLASSIC_CAPS(OWL, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    // two ids in Swarms list, which one is the dongle and which the cable nobody here knows
    {0x2e4c, "ROCCAT Leadr", &ops<LEADR>, nullptr, 0x06, CLASSIC_CAPS(LEADR, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2e4e, "ROCCAT Leadr", &ops<LEADR>, nullptr, 0x06, CLASSIC_CAPS(LEADR, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    // three colours in Swarms list (its module comes as KOVA_AIMO_BLACK and _WHITE, the monitor has a gray class too)
    {0x2cf1, "ROCCAT Kova AIMO", &ops<KOVA_AIMO>, nullptr, 0x06, CLASSIC_CAPS(KOVA_AIMO, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2cf2, "ROCCAT Kova AIMO", &ops<KOVA_AIMO>, nullptr, 0x06, CLASSIC_CAPS(KOVA_AIMO, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2cf3, "ROCCAT Kova AIMO", &ops<KOVA_AIMO>, nullptr, 0x06, CLASSIC_CAPS(KOVA_AIMO, MOUSE | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, 5, FIVE, 3)},
    {0x2d5a, "ROCCAT Savu", &ops<SAVU>, nullptr, 0x06, CLASSIC_CAPS(SAVU, MOUSE | ROCKCAT_HAS_EASYSHIFT, 5, SAVU_FX, 0)},
};
const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

#include "classic_sim.inc"

} // namespace
