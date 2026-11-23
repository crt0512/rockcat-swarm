// Skeltr, the one with the phone holder and bluetooth. Ryos conversation, Sova sized primary report (144 slots), Ryos style
// M, thumb and F key reports, 20 Easy-Shift keys, macros in one report, and five light zones (the keys and four corner
// strips) that can also be fed live from the pc. roccat-tools (skeltr/libroccatskeltr), never on hardware. docs/protocol/skeltr.md

#include "../internal.h"
#include "../keyboards.h"

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

#include "skeltr_tables.inc"
#include "isku_plus_force_tables.inc"
#include "../keyboards/skeltr.inc"

#include "../keyboards/isku_plus_force_fx.inc"
const keyboard_layout FORCE_FX_KEYBOARD = {ISKU_PLUS_FORCE_FX_REGIONS, static_cast<int>(sizeof ISKU_PLUS_FORCE_FX_REGIONS / sizeof ISKU_PLUS_FORCE_FX_REGIONS[0]), 680, 370};
const keyboard_layout SKELTR_KEYBOARD = {SKELTR_REGIONS, static_cast<int>(sizeof SKELTR_REGIONS / sizeof SKELTR_REGIONS[0]), 680, 370};

constexpr uint16_t PID = 0x2fc6;
constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_MACRO = 0x0e, REPORT_INFO = 0x0f, REPORT_LIGHT_CONTROL = 0x13, REPORT_LIGHT_CUSTOM = 0x18;
// inputs are Swarms numbers: the slot in the primary report for 0 to 143 (every slot of roccat-tools default primary report
// holds the usage of the key Swarm puts there), then T1 to T3 at 255 to 257 and Caps Lock at 258
constexpr int INPUTS = 259, PROFILES = 5, SLOTS = 144, ZONES = 5;

struct part {
    uint8_t id, request;
    uint16_t size;
};
enum { P_PRIMARY, P_KEYMASK, P_FUNCTION, P_LIGHT, P_EXTRA, P_MACROKEYS, P_EASYZONE, P_THUMB, PARTS };
constexpr uint16_t PID_FORCE_FX = 0x3214;
// size 0 = the model doesnt have it
struct model {
    uint16_t pid;
    part parts[PARTS];
    const uint8_t *factory[PARTS];
    int zones; // light zones we understand, 0 = the light report just gets carried along
};
const model MODELS[] = {
    {PID,
     {{0x06, 0x40, 149}, {0x07, 0x50, 6}, {0x08, 0x60, 95}, {0x10, 0x70, 36}, {0x0a, 0x90, 8}, {0x0d, 0xa0, 35}, {0x09, 0xb0, 65}, {0x0b, 0xd0, 23}},
     {FACTORY_PRIMARY, FACTORY_KEYMASK, FACTORY_FUNCTION, FACTORY_LIGHT, FACTORY_EXTRA, FACTORY_MACROKEYS, FACTORY_EASYZONE, FACTORY_THUMB},
     5},
    // Isku+ Force FX, out of the Swarm monitor (isku_plus_force_device_c, FUN_004bdc00 reads with these selects) and its
    // module default: the Skeltr keys, but the Easy-Shift report on 0b (the shared read FUN_004eaac0 uses 09 only for the
    // Skeltr), no key mask, a 20 byte light that isnt understood (passed through) and two more light reports (c1 23 bytes,
    // c2 103 bytes) that arent even read
    {PID_FORCE_FX,
     {{0x06, 0x40, 149}, {0x07, 0x50, 0}, {0x08, 0x60, 95}, {0x10, 0x70, 20}, {0x0a, 0x90, 8}, {0x0d, 0xa0, 35}, {0x0b, 0xb0, 65}, {0x0b, 0xd0, 23}},
     {FX_PRIMARY, nullptr, FX_FUNCTION, FX_LIGHT, FX_EXTRA, FX_MACROKEYS, FX_EASYZONE, FX_THUMB},
     0},
};
const model &model_of(uint16_t pid)
{
    return pid == PID_FORCE_FX ? MODELS[1] : MODELS[0];
}
static_assert(sizeof FACTORY_PRIMARY == 149 && sizeof FACTORY_KEYMASK == 6 && sizeof FACTORY_FUNCTION == 95 && sizeof FACTORY_LIGHT == 36 &&
                  sizeof FACTORY_EXTRA == 8 && sizeof FACTORY_MACROKEYS == 35 && sizeof FACTORY_EASYZONE == 65 && sizeof FACTORY_THUMB == 23,
              "factory reports are whole reports");

size_t offset_of(const model &m, int p)
{
    size_t at = 0;
    for (int i = 0; i < p; ++i)
        at += m.parts[i].size;
    return at;
}

// light: [10 24 profile mode preset speed start sleep wake switch flow (index r g b) x 5 brightness ? minutes sum]
constexpr size_t L_MODE = 3, L_PRESET = 4, L_SPEED = 5, L_FLOW = 10, L_COLOURS = 11, L_BRIGHTNESS = 31, L_MINUTES = 33;
constexpr int EFFECTS[][2] = {{ROCKCAT_EFFECT_OFF, 0},       {ROCKCAT_EFFECT_FULLY_LIT, 1}, {ROCKCAT_EFFECT_BLINKING, 2},
                              {ROCKCAT_EFFECT_BREATHING, 4}, {ROCKCAT_EFFECT_HEARTBEAT, 6}, {ROCKCAT_EFFECT_FADE, 7},
                              {ROCKCAT_EFFECT_HEATMAP, 8}};

constexpr uint8_t FUNCTION_INPUT[15] = {78, 118, 113, 115, 127, 99, 41, 46, 22, 119, 49, 69, 79, 16, 136}; // F1 .. Pause
constexpr uint8_t MACRO_INPUT[5] = {18, 83, 20, 36, 13};                                                   // M1 .. M5
constexpr int THUMB_INPUT = 255, CAPS_INPUT = 258, ESC_INPUT = 123, FN_INPUT = 2;

enum key_kind : uint8_t { K_NONE, K_PLAIN, K_ZONE, K_FUNCTION, K_MACRO, K_THUMB, K_CAPS };
struct key_place {
    uint8_t kind = K_NONE, at = 0;
};

const key_place &place(int input)
{
    static const struct table {
        key_place key[INPUTS];
        table()
        {
            for (int r = 0; r < SKELTR_KEYBOARD.region_count; ++r)
                for (int i = 0; i < SKELTR_KEYBOARD.regions[r].count; ++i) {
                    int led = SKELTR_KEYBOARD.regions[r].keys[i].led;
                    // Esc and Fn cant be anything else (roccat-tools)
                    if (led >= 0 && led < SLOTS && led != ESC_INPUT && led != FN_INPUT)
                        key[led] = key_place{K_PLAIN, 0};
                }
            for (int i = 0; i < 20; ++i)
                key[ZONE_SLOT[i]] = key_place{K_ZONE, static_cast<uint8_t>(i)};
            for (int i = 0; i < 15; ++i)
                key[FUNCTION_INPUT[i]] = key_place{K_FUNCTION, static_cast<uint8_t>(i)};
            for (int i = 0; i < 5; ++i)
                key[MACRO_INPUT[i]] = key_place{K_MACRO, static_cast<uint8_t>(i)};
            for (int i = 0; i < 3; ++i)
                key[THUMB_INPUT + i] = key_place{K_THUMB, static_cast<uint8_t>(i)};
            key[CAPS_INPUT] = key_place{K_CAPS, 0};
        }
    } T;
    static const key_place none;
    return input >= 0 && input < INPUTS ? T.key[input] : none;
}

struct spot {
    int part = -1;
    size_t at = 0;
    bool wide = false;
};

// layer 0 normal, 1 Easy-Shift, 2 Fn (the second half of the F key report, like on the Ryos)
spot find(int layer, int input)
{
    const key_place &k = place(input);
    if (layer == 0) {
        switch (k.kind) {
        case K_PLAIN:
        case K_ZONE:     return {P_PRIMARY, static_cast<size_t>(3 + input), false};
        case K_FUNCTION: return {P_FUNCTION, static_cast<size_t>(3 + 3 * k.at), true};
        case K_MACRO:    return {P_MACROKEYS, static_cast<size_t>(3 + 3 * k.at), true};
        case K_THUMB:    return {P_THUMB, static_cast<size_t>(3 + 3 * k.at), true};
        case K_CAPS:     return {P_EXTRA, 3, false};
        default:         return {};
        }
    }
    if (layer == 1) {
        switch (k.kind) {
        case K_ZONE:  return {P_EASYZONE, static_cast<size_t>(3 + 3 * k.at), true};
        case K_MACRO: return {P_MACROKEYS, static_cast<size_t>(3 + 3 * (5 + k.at)), true};
        case K_THUMB: return {P_THUMB, static_cast<size_t>(3 + 3 * (3 + k.at)), true};
        default:      return {};
        }
    }
    if (layer == 2 && k.kind == K_FUNCTION)
        return {P_FUNCTION, static_cast<size_t>(3 + 3 * (15 + k.at)), true};
    return {};
}

// M keys 0 to 9, thumb keys 10 to 15, the Easy-Shift side of the zone keys 16 to 35
int macro_slot(int layer, int input)
{
    const key_place &k = place(input);
    switch (k.kind) {
    case K_MACRO: return layer <= 1 ? layer * 5 + k.at : -1;
    case K_THUMB: return layer <= 1 ? 10 + layer * 3 + k.at : -1;
    case K_ZONE:  return layer == 1 ? ZONE_MACRO[k.at] : -1;
    default:      return -1;
    }
}

struct skeltr_profile final : rockcat_profile {
    const model &M;
    std::vector<uint8_t> raw;
    bool dirty[PARTS] = {};
    std::vector<rockcat_macro> macros;

    skeltr_profile(const device_desc *d, int i) : rockcat_profile(d, i), M(model_of(d->pid)), raw(offset_of(M, PARTS)) {}
    uint8_t *at(int p) { return raw.data() + offset_of(M, p); }
    const uint8_t *at(int p) const { return raw.data() + offset_of(M, p); }
    const part &P(int p) const { return M.parts[p]; }
    bool have(int p) const { return M.parts[p].size != 0; }
    rockcat_profile *clone() const override { return new skeltr_profile(*this); }
    bool known(int layer, int input) const { return layer >= 0 && layer < 3 && place(input).kind != K_NONE; }

    int read(rockcat_device *dev) override
    {
        std::fill(dirty, dirty + PARTS, false);
        macros.clear();
        for (int p = 0; p < PARTS; ++p) {
            if (!have(p))
                continue;
            int r = select_data(dev, static_cast<uint8_t>(index), P(p).request);
            if (r != ROCKCAT_OK)
                return r;
            uint8_t *b = at(p);
            std::memset(b, 0, P(p).size);
            b[0] = P(p).id;
            r = get_raw(dev, b, P(p).size);
            if (r != ROCKCAT_OK)
                return r;
            if (b[1] != P(p).size || b[2] != index)
                return ROCKCAT_ERR_PROTOCOL;
            if (!checksum_ok(b, P(p).size))
                return ROCKCAT_ERR_CHECKSUM;
        }
        return ROCKCAT_OK;
    }

    int write(rockcat_device *dev) override
    {
        while (!macros.empty()) {
            int r = macro_single_write(dev, REPORT_MACRO, macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        for (int p = 0; p < PARTS; ++p) {
            if (!dirty[p] || !have(p))
                continue;
            put_checksum(at(p), P(p).size);
            int r = set_raw(dev, at(p), P(p).size, true);
            if (r != ROCKCAT_OK)
                return r;
            dirty[p] = false;
        }
        return ROCKCAT_OK;
    }

    int set_index(int i) override
    {
        if (i < 0 || i >= PROFILES)
            return ROCKCAT_ERR_ARGUMENT;
        index = i;
        for (int p = 0; p < PARTS; ++p) {
            if (!have(p))
                continue;
            at(p)[2] = static_cast<uint8_t>(i);
            dirty[p] = true;
        }
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        skeltr_profile copy(*this);
        for (int p = 0; p < PARTS; ++p)
            if (have(p))
                put_checksum(copy.at(p), P(p).size);
        out.insert(out.end(), copy.raw.begin(), copy.raw.end());
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != raw.size())
            return ROCKCAT_ERR_ARGUMENT;
        for (int p = 0; p < PARTS; ++p)
            if (have(p) && (buf[offset_of(M, p)] != P(p).id || buf[offset_of(M, p) + 1] != P(p).size))
                return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(raw.data(), buf, raw.size());
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        if (!M.zones)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *l = at(P_LIGHT);
        switch (which) {
        case ROCKCAT_SET_BRIGHTNESS:   *value = std::min<int>(l[L_BRIGHTNESS], 5) * 51; break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = std::min<int>(l[L_SPEED], 2) + 1; break;
        case ROCKCAT_SET_IDLE_MINUTES: *value = l[L_MINUTES]; break;
        case ROCKCAT_SET_COLOR_FLOW:   *value = l[L_FLOW] != 0; break;
        case ROCKCAT_SET_EFFECT:
            // "pulsing" (5) has no name of ours, it reads as fully lit and survives as long as nobody picks another
            *value = ROCKCAT_EFFECT_FULLY_LIT;
            for (const auto &e : EFFECTS)
                if (e[1] == l[L_PRESET])
                    *value = e[0];
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        if (!M.zones)
            return ROCKCAT_ERR_UNSUPPORTED;
        uint8_t *l = at(P_LIGHT);
        switch (which) {
        case ROCKCAT_SET_BRIGHTNESS:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_BRIGHTNESS] = static_cast<uint8_t>((value * 5 + 127) / 255);
            break;
        case ROCKCAT_SET_EFFECT_SPEED:
            if (value < 1 || value > 3)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_SPEED] = static_cast<uint8_t>(value - 1);
            break;
        case ROCKCAT_SET_IDLE_MINUTES:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_MINUTES] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_COLOR_FLOW:
            l[L_FLOW] = value ? 1 : 0;
            break;
        case ROCKCAT_SET_EFFECT: {
            bool ok = false;
            for (const auto &e : EFFECTS)
                if (e[0] == value) {
                    l[L_PRESET] = static_cast<uint8_t>(e[1]);
                    ok = true;
                }
            if (!ok)
                return ROCKCAT_ERR_ARGUMENT;
            break;
        }
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    // five zones: the keys, top left, bottom left, top right, bottom right. index r g b each, no on / off per zone
    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= M.zones)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *z = at(P_LIGHT) + L_COLOURS + 4 * led;
        if (c)
            *c = rockcat_color{255, z[1], z[2], z[3]};
        if (enabled)
            *enabled = 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= M.zones)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *z = at(P_LIGHT) + L_COLOURS + 4 * led;
        const int a = enabled ? c.a : 0;
        z[1] = static_cast<uint8_t>((c.r * a + 127) / 255);
        z[2] = static_cast<uint8_t>((c.g * a + 127) / 255);
        z[3] = static_cast<uint8_t>((c.b * a + 127) / 255);
        // custom mode = the zones take our rgb and not the preset colour table
        at(P_LIGHT)[L_MODE] = 1;
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = at(s.part) + s.at;
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        if (!s.wide && (a->param1 || a->param2))
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *p = at(s.part) + s.at;
        p[0] = a->type;
        if (s.wide) {
            p[1] = a->param1;
            p[2] = a->param2;
        }
        dirty[s.part] = true;
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        for (int p : {P_PRIMARY, P_FUNCTION, P_MACROKEYS, P_EASYZONE, P_THUMB})
            std::memcpy(at(p) + 3, M.factory[p] + 3, P(p).size - 5);
        at(P_EXTRA)[3] = M.factory[P_EXTRA][3];
        at(P_EXTRA)[4] = M.factory[P_EXTRA][4];
        for (int p : {P_PRIMARY, P_FUNCTION, P_MACROKEYS, P_EASYZONE, P_THUMB, P_EXTRA})
            dirty[p] = true;
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (!known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return 0;
        if (!s.wide)
            return ROCKCAT_ROOM_KEY;
        return ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS | (macro_slot(layer, input) >= 0 ? ROCKCAT_ROOM_MACRO : 0);
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = M.factory[s.part] + s.at;
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        if (!m || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        int slot = macro_slot(layer, input);
        return slot < 0 ? ROCKCAT_ERR_UNSUPPORTED : macro_single_read(dev, REPORT_MACRO, index, slot, m);
    }

    int set_macro(int layer, int input, const rockcat_macro *m) override
    {
        if (!m || m->count > ROCKCAT_MACRO_KEYSTROKES || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        int slot = macro_slot(layer, input);
        if (slot < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        rockcat_macro copy = *m;
        copy.profile = static_cast<uint8_t>(index);
        copy.button = static_cast<uint8_t>(slot);
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
    return new skeltr_profile(desc, index);
}

int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[8] = {REPORT_INFO};
    int r = get_raw(dev, info, sizeof info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

// [05 04 active count], like the Sova
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t rep[4] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = rep[2];
    if (count)
        *count = PROFILES;
    return ROCKCAT_OK;
}

int set_active(rockcat_device *dev, uint8_t profile)
{
    if (profile >= PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t rep[4] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    rep[1] = 0x04;
    rep[2] = profile;
    return set_raw(dev, rep, sizeof rep, true);
}

// the zones from the pc: [13 08 mode] says who owns them (1 = us), [18 11 rgb x 5] is a frame
int light_control(rockcat_device *dev, bool host)
{
    const uint8_t rep[8] = {REPORT_LIGHT_CONTROL, 0x08, static_cast<uint8_t>(host ? 1 : 0)};
    return set_raw(dev, rep, sizeof rep, true);
}
int direct_begin(rockcat_device *dev)
{
    return light_control(dev, true);
}
int direct_end(rockcat_device *dev)
{
    return light_control(dev, false);
}
int direct_switch(rockcat_device *dev, bool host)
{
    return light_control(dev, host);
}
int direct_frame(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!colors || count != ZONES)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t rep[2 + 3 * ZONES] = {REPORT_LIGHT_CUSTOM, sizeof rep};
    for (int z = 0; z < ZONES; ++z) {
        const rockcat_color &c = colors[z];
        rep[2 + 3 * z] = static_cast<uint8_t>((c.r * c.a + 127) / 255);
        rep[3 + 3 * z] = static_cast<uint8_t>((c.g * c.a + 127) / 255);
        rep[4 + 3 * z] = static_cast<uint8_t>((c.b * c.a + 127) / 255);
    }
    // roccat-tools waits 160 ms before asking whether it took it, which would be six frames a second. asking sooner is a guess
    int r = set_raw(dev, rep, sizeof rep, false);
    return r != ROCKCAT_OK ? r : wait_ready_after(dev, 20);
}
int direct_active(rockcat_device *)
{
    return 1;
}

bool decode_event(const uint8_t *in, int n, rockcat_event *ev)
{
    return RYOS_FAMILY.decode_event(in, n, ev);
}

const char *input_label(int input)
{
    if (input >= THUMB_INPUT && input < THUMB_INPUT + 3) {
        static const char *const t[3] = {"T1", "T2", "T3"};
        return t[input - THUMB_INPUT];
    }
    return input == CAPS_INPUT ? "Caps Lock" : "?";
}
const char *led_label(int led)
{
    static const char *const zones[ZONES] = {"Keys", "Top left", "Bottom left", "Top right", "Bottom right"};
    return led >= 0 && led < ZONES ? zones[led] : "?";
}
const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : layer == 2 ? "Fn" : "?";
}
// the Sova ones, which are the Ryos ones plus the windows shortcuts. the Skeltr knows the same list
const char *type_label(uint8_t t)
{
    const device_desc *sova = device_find(ROCKCAT_VID, 0x2f76);
    return sova ? sova->family->type_label(t) : RYOS_FAMILY.type_label(t);
}
bool type_is_function(uint8_t t)
{
    const device_desc *sova = device_find(ROCKCAT_VID, 0x2f76);
    return sova ? sova->family->type_is_function(t) : RYOS_FAMILY.type_is_function(t);
}

const family_ops SKELTR_FAMILY = {firmware,      get_active,    set_active,   new_profile, direct_begin, direct_frame, direct_end,
                                  direct_switch, direct_active, decode_event, input_label, led_label,    layer_label,  type_label,
                                  type_is_function};

constexpr uint32_t bit(int n)
{
    return 1u << n;
}

const device_desc DEVICES[] = {
    {PID, "ROCCAT Skeltr", &SKELTR_FAMILY, nullptr, 0x06,
     {ROCKCAT_KIND_KEYBOARD, "skeltr", ROCKCAT_TESTED_NO, "",
      ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB | ROCKCAT_HAS_DIRECT_LIGHT, PROFILES, INPUTS, 3, ZONES, 0, 0, 0, 0,
      {0},
      bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) |
          bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_FADE) | bit(ROCKCAT_EFFECT_HEATMAP),
      0, 1, 3, -1},
     &SKELTR_KEYBOARD},
    // the Skeltr key numbers are the Isku+ ones, and the Force FX is an Isku+ with lights. so it gets the Isku+ picture
    {PID_FORCE_FX, "ROCCAT Isku+ Force FX", &SKELTR_FAMILY, nullptr, 0x06,
     {ROCKCAT_KIND_KEYBOARD, "skeltr", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, PROFILES, INPUTS, 3, 0, 0, 0, 0, 0,
      {0}, 0, 0, 0, 0, -1},
     &FORCE_FX_KEYBOARD},
};
const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// pretend Skeltr and Isku+ Force FX: the factory reports in every profile, the Skeltr with brightness 5 - n. the Force FX
// has two reports on 0b (Easy-Shift and thumb keys), reads tell them apart by the select, writes by their size
struct skeltr_sim final : transport {
    const model &m;
    uint8_t active = 0, select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> data[PROFILES][PARTS];
    std::map<int, std::vector<uint8_t>> macros;
    uint8_t custom = 0;
    int frames = 0;
    std::mutex lock;

    explicit skeltr_sim(const model &mod) : m(mod)
    {
        for (uint8_t p = 0; p < PROFILES; ++p)
            for (int i = 0; i < PARTS; ++i) {
                if (!m.parts[i].size)
                    continue;
                data[p][i].assign(m.factory[i], m.factory[i] + m.parts[i].size);
                data[p][i][2] = p;
                if (i == P_LIGHT && m.zones)
                    data[p][i][L_BRIGHTNESS] = static_cast<uint8_t>(5 - p);
                put_checksum(data[p][i].data(), data[p][i].size());
            }
    }

    int part_for_read(uint8_t id) const
    {
        for (int i = 0; i < PARTS; ++i)
            if (m.parts[i].size && m.parts[i].id == id && m.parts[i].request == select_b)
                return i;
        for (int i = 0; i < PARTS; ++i)
            if (m.parts[i].size && m.parts[i].id == id)
                return i;
        return -1;
    }
    int part_for_write(uint8_t id, size_t len) const
    {
        for (int i = 0; i < PARTS; ++i)
            if (m.parts[i].size && m.parts[i].id == id && m.parts[i].size == len)
                return i;
        for (int i = 0; i < PARTS; ++i)
            if (m.parts[i].size && m.parts[i].id == id)
                return i;
        return -1;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        int p = part_for_read(buf[0]);
        if (p >= 0) {
            if (select_b != m.parts[p].request || select_a >= PROFILES)
                return ROCKCAT_ERR_IO;
            out = data[select_a][p];
        } else {
            switch (buf[0]) {
            case 0x04: {
                int st = 1;
                if (!status_queue.empty()) {
                    st = status_queue.front();
                    status_queue.erase(status_queue.begin());
                }
                out = {0x04, static_cast<uint8_t>(st), 0};
                break;
            }
            case REPORT_PROFILE:
                out = {REPORT_PROFILE, 0x04, active, 5};
                break;
            case REPORT_MACRO: {
                auto it = macros.find(select_a * 256 + select_b);
                if (it != macros.end()) {
                    out = it->second;
                } else {
                    out.assign(MACRO_SINGLE_SIZE, 0xff);
                    out[0] = REPORT_MACRO;
                }
                break;
            }
            case REPORT_INFO:
                out = {REPORT_INFO, 0x08, 0x6b, 0x01, 0, 1, 0, 0};
                break;
            default:
                return ROCKCAT_ERR_IO;
            }
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        int p = buf[0] == REPORT_MACRO ? -1 : part_for_write(buf[0], len);
        if (p >= 0 || buf[0] == REPORT_MACRO) {
            const size_t want = p >= 0 ? m.parts[p].size : MACRO_SINGLE_SIZE;
            const uint8_t profile = p >= 0 ? buf[2] : buf[3];
            if (len != want || profile >= PROFILES || get16(buf + len - 2) != rockcat_checksum(buf, len - 2)) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            if (p >= 0)
                data[profile][p].assign(buf, buf + len);
            else
                macros[profile * 256 + buf[4]].assign(buf, buf + len);
            status_queue = {3};
            return ROCKCAT_OK;
        }
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            return ROCKCAT_OK;
        case REPORT_PROFILE:
            if (len != 4 || buf[2] >= PROFILES)
                return ROCKCAT_ERR_IO;
            active = buf[2];
            return ROCKCAT_OK;
        case REPORT_LIGHT_CONTROL:
            custom = buf[2];
            return ROCKCAT_OK;
        case REPORT_LIGHT_CUSTOM:
            if (len != 17)
                return ROCKCAT_ERR_IO;
            ++frames;
            return ROCKCAT_OK;
        default:
            return ROCKCAT_ERR_IO;
        }
    }

    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

skeltr_sim &shared(uint16_t pid)
{
    static skeltr_sim skeltr(MODELS[0]), force(MODELS[1]);
    return pid == PID_FORCE_FX ? force : skeltr;
}

template <uint16_t ID> struct skeltr_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared(ID).lock);
        return shared(ID).get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared(ID).lock);
        return shared(ID).set_feature(buf, len);
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

const sim_registration sim({"skeltr", PID, "ROCCAT Skeltr", 1, [] { return std::unique_ptr<transport>(new skeltr_door<PID>); }});
const sim_registration sim_force({"isku-plus-force-fx", PID_FORCE_FX, "ROCCAT Isku+ Force FX", 1,
                                  [] { return std::unique_ptr<transport>(new skeltr_door<PID_FORCE_FX>); }});

} // namespace
