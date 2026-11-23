// Sova, the couch keyboard with the lapboard. membrane 2f76 and mechanical (MK) 2f94, same reports for both says roccat-tools.
// it talks like a Ryos (select on 04, wait for 04, the same event report) but every report has its own size, macros come in
// one report and not in halves, and the light is one backlight for the whole board on the membrane. docs/protocol/sova.md

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

#include "sova_tables.inc"
#include "../keyboards/sova_membrane.inc"

const keyboard_layout SOVA_KEYBOARD = {SOVA_MEMBRANE_REGIONS, static_cast<int>(sizeof SOVA_MEMBRANE_REGIONS / sizeof SOVA_MEMBRANE_REGIONS[0]), 705, 415};

constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_MACRO = 0x0e, REPORT_INFO = 0x0f;
// inputs are Swarms numbers, which are the slots of the primary report for 0 to 143 (checked on a real one: every slot
// holds the usage of the key Swarm puts there). Caps Lock and Fn sit outside of it, Swarm gives them 225 and 241
constexpr int INPUTS = 242, PROFILES = 5, SLOTS = 144;

struct part {
    uint8_t id, request;
    uint16_t size;
    uint8_t profile_at;
};
enum { P_PRIMARY, P_FUNCTION, P_EXTRA, P_EASYZONE, P_KEYMASK, P_LIGHT, PARTS };
constexpr part PART[PARTS] = {
    {0x06, 0xa0, 149, 2}, // a byte per slot
    {0x07, 0xa1, 95, 2},  // F1 to F12, Print, Insert, Del: 3 bytes each, first plain, then with Fn held
    {0x0a, 0xa4, 13, 2},  // Caps Lock, the rest nobody knows
    {0x0b, 0xa5, 294, 3}, // 40 keys with Easy-Shift held, 3 bytes each, then 168 bytes of leftovers. two byte size
    {0x0c, 0xb0, 6, 2},   // which keys the game mode switches off, passed through
    {0x0d, 0xb1, 160, 2}, // light
};
static_assert(sizeof FACTORY_PRIMARY == 149 && sizeof FACTORY_FUNCTION == 95 && sizeof FACTORY_EXTRA == 13 && sizeof FACTORY_EASYZONE == 294 &&
                  sizeof FACTORY_KEYMASK == 6 && sizeof FACTORY_LIGHT == 160,
              "the factory reports are whole reports");
const uint8_t *const FACTORY[PARTS] = {FACTORY_PRIMARY, FACTORY_FUNCTION, FACTORY_EXTRA, FACTORY_EASYZONE, FACTORY_KEYMASK, FACTORY_LIGHT};

constexpr size_t offset_of(int p)
{
    size_t at = 0;
    for (int i = 0; i < p; ++i)
        at += PART[i].size;
    return at;
}
constexpr size_t TOTAL = offset_of(PARTS);

// light report
enum : size_t {
    L_EFFECT = 4,     // see EFFECTS
    L_SPEED = 5,      // 0 to 2
    L_START = 6,      // what it does when switched on, passed through
    L_SLEEP = 7,      // going to sleep, passed through
    L_WAKE = 8,       // waking up, passed through
    L_SWITCH = 9,     // profile switched, passed through
    L_BRIGHTNESS = 11, // 0 to 5
    L_SLEEP_AFTER = 13, // minutes
};
constexpr int EFFECTS[][2] = {
    {ROCKCAT_EFFECT_OFF, 0},       {ROCKCAT_EFFECT_FULLY_LIT, 1}, {ROCKCAT_EFFECT_BLINKING, 2}, {ROCKCAT_EFFECT_BREATHING, 3},
    {ROCKCAT_EFFECT_HEARTBEAT, 4}, {ROCKCAT_EFFECT_IMPULSE, 5},   {ROCKCAT_EFFECT_HEATMAP, 6},
};

// where each key lives besides the primary report. out of roccat-tools (sova_key_relations.c), but matched to Swarms numbers by
// the order of the keys: roccat-tools has Z, U and Y in the wrong slots, the real keyboard agrees with Swarm
constexpr int FUNCTION_KEYS = 15, ZONE_KEYS = 40;
constexpr uint8_t FUNCTION_INPUT[FUNCTION_KEYS] = {72, 17, 25, 50, 43, 32, 24, 16, 73, 41, 49, 64, 96, 80, 81};
constexpr uint8_t ZONE_INPUT[ZONE_KEYS] = {79, 71, 63, 55, 47,  34, 26,  18, 74, 42, 0,   119, 1,   2,  3,  35, 27, 19, 75, 83,
                                           127, 135, 143, 4, 5, 37, 29, 21, 77, 85, 6, 15, 23, 31, 39, 38, 30, 22, 86, 94};
constexpr int ESC_INPUT = 120, CAPS_INPUT = 225, FN_INPUT = 241;

enum key_kind : uint8_t { K_NONE, K_PLAIN, K_ZONE, K_FUNCTION, K_CAPS };
struct key_place {
    uint8_t kind = K_NONE, at = 0;
};

const key_place &place(int input)
{
    static const struct table {
        key_place key[INPUTS];
        table()
        {
            // every slot a key of the tables sits on can be another key. Esc and Fn cant be anything else (roccat-tools)
            for (int r = 0; r < SOVA_KEYBOARD.region_count; ++r)
                for (int i = 0; i < SOVA_KEYBOARD.regions[r].count; ++i) {
                    int led = SOVA_KEYBOARD.regions[r].keys[i].led;
                    if (led >= 0 && led < SLOTS && led != ESC_INPUT)
                        key[led] = key_place{K_PLAIN, 0};
                }
            for (int i = 0; i < ZONE_KEYS; ++i)
                key[ZONE_INPUT[i]] = key_place{K_ZONE, static_cast<uint8_t>(i)};
            for (int i = 0; i < FUNCTION_KEYS; ++i)
                key[FUNCTION_INPUT[i]] = key_place{K_FUNCTION, static_cast<uint8_t>(i)};
            key[CAPS_INPUT] = key_place{K_CAPS, 0};
        }
    } T;
    static const key_place none;
    return input >= 0 && input < INPUTS ? T.key[input] : none;
}

struct sova_profile final : rockcat_profile {
    uint8_t raw[TOTAL] = {};
    bool dirty[PARTS] = {};
    std::vector<rockcat_macro> macros;

    using rockcat_profile::rockcat_profile;

    uint8_t *at(int p) { return raw + offset_of(p); }
    const uint8_t *at(int p) const { return raw + offset_of(p); }
    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new sova_profile(*this); }

    static bool shaped(const uint8_t *b, int p)
    {
        return b[0] == PART[p].id && b[1] == (PART[p].size & 0xff) && (PART[p].profile_at == 2 || b[2] == PART[p].size >> 8);
    }

    int fetch(rockcat_device *dev, int p)
    {
        int r = select_data(dev, static_cast<uint8_t>(index), PART[p].request);
        if (r != ROCKCAT_OK)
            return r;
        uint8_t *b = at(p);
        std::memset(b, 0, PART[p].size);
        b[0] = PART[p].id;
        r = get_raw(dev, b, PART[p].size);
        if (r != ROCKCAT_OK)
            return r;
        if (!shaped(b, p) || b[PART[p].profile_at] != index)
            return ROCKCAT_ERR_PROTOCOL;
        return checksum_ok(b, PART[p].size) ? ROCKCAT_OK : ROCKCAT_ERR_CHECKSUM;
    }

    int read(rockcat_device *dev) override
    {
        std::fill(dirty, dirty + PARTS, false);
        macros.clear();
        for (int p = 0; p < PARTS; ++p) {
            int r = fetch(dev, p);
            if (r != ROCKCAT_OK)
                return r;
        }
        return ROCKCAT_OK;
    }

    int read_light(rockcat_device *dev) override { return fetch(dev, P_LIGHT); }

    int write(rockcat_device *dev) override
    {
        while (!macros.empty()) {
            int r = macro_single_write(dev, REPORT_MACRO, macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        for (int p = 0; p < PARTS; ++p) {
            if (!dirty[p])
                continue;
            put_checksum(at(p), PART[p].size);
            int r = set_raw(dev, at(p), PART[p].size, true);
            if (r != ROCKCAT_OK)
                return r;
            dirty[p] = false;
        }
        return ROCKCAT_OK;
    }

    int set_index(int i) override
    {
        if (i < 0 || i >= caps().profile_count)
            return ROCKCAT_ERR_ARGUMENT;
        index = i;
        for (int p = 0; p < PARTS; ++p) {
            at(p)[PART[p].profile_at] = static_cast<uint8_t>(i);
            dirty[p] = true;
        }
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        sova_profile copy(*this);
        for (int p = 0; p < PARTS; ++p)
            put_checksum(copy.at(p), PART[p].size);
        out.insert(out.end(), copy.raw, copy.raw + TOTAL);
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != TOTAL)
            return ROCKCAT_ERR_ARGUMENT;
        for (int p = 0; p < PARTS; ++p)
            if (!shaped(buf + offset_of(p), p))
                return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(raw, buf, TOTAL);
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = at(P_LIGHT);
        switch (which) {
        case ROCKCAT_SET_BRIGHTNESS:   *value = std::min<int>(l[L_BRIGHTNESS], 5) * 51; break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = std::min<int>(l[L_SPEED], 2) + 1; break;
        case ROCKCAT_SET_IDLE_MINUTES: *value = l[L_SLEEP_AFTER]; break;
        case ROCKCAT_SET_EFFECT:
            *value = ROCKCAT_EFFECT_FULLY_LIT;
            for (const auto &e : EFFECTS)
                if (e[1] == l[L_EFFECT])
                    *value = e[0];
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
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
            l[L_SLEEP_AFTER] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT: {
            bool known = false;
            for (const auto &e : EFFECTS)
                if (e[0] == value) {
                    l[L_EFFECT] = static_cast<uint8_t>(e[1]);
                    known = true;
                }
            if (!known)
                return ROCKCAT_ERR_ARGUMENT;
            break;
        }
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    struct spot {
        int part = -1;
        size_t at = 0;
        bool wide = false;
    };

    static spot find(int layer, int input)
    {
        const key_place &k = place(input);
        if (layer == 0) {
            switch (k.kind) {
            case K_PLAIN:
            case K_ZONE:     return {P_PRIMARY, static_cast<size_t>(3 + input), false};
            case K_FUNCTION: return {P_FUNCTION, static_cast<size_t>(3 + 3 * k.at), true};
            case K_CAPS:     return {P_EXTRA, 3, false};
            default:         return {};
            }
        }
        if (layer == 1 && k.kind == K_ZONE)
            return {P_EASYZONE, static_cast<size_t>(4 + 3 * k.at), true};
        // the second half of the function report is Fn, like on the Ryos (out of the box: mute, volume, calculator ...)
        if (layer == 2 && k.kind == K_FUNCTION)
            return {P_FUNCTION, static_cast<size_t>(3 + 3 * (FUNCTION_KEYS + k.at)), true};
        return {};
    }

    bool known(int layer, int input) const { return layer >= 0 && layer < caps().layer_count && place(input).kind != K_NONE; }

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
        for (int p : {P_PRIMARY, P_FUNCTION, P_EXTRA, P_EASYZONE}) {
            // keys only, the header stays ours (profile number) and so do the bytes nobody knows in the extra report
            const size_t head = PART[p].profile_at + 1;
            if (p == P_EXTRA)
                at(p)[3] = FACTORY[p][3];
            else if (p == P_EASYZONE)
                std::memcpy(at(p) + head, FACTORY[p] + head, 3 * ZONE_KEYS);
            else
                std::memcpy(at(p) + head, FACTORY[p] + head, PART[p].size - head - 2);
            dirty[p] = true;
        }
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
        const uint8_t *p = FACTORY[s.part] + s.at;
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    // a macro per Easy-Shift key, the slot is its place in the Easy-Shift report
    static int macro_slot(int layer, int input)
    {
        const key_place &k = place(input);
        return layer == 1 && k.kind == K_ZONE ? k.at : -1;
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
    return new sova_profile(desc, index);
}

// [0f 08 firmware ...], 0x7f = 127 = 1.27
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[8] = {REPORT_INFO};
    int r = get_raw(dev, info, sizeof info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

// [05 04 active count]. the count says 1 on the real one, whatever that means, it goes back the way it came
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

// the event report is the Ryos one, number for number
bool decode_event(const uint8_t *in, int n, rockcat_event *ev)
{
    return RYOS_FAMILY.decode_event(in, n, ev);
}

const char *input_label(int input)
{
    return input == FN_INPUT ? "Fn" : input == CAPS_INPUT ? "Caps Lock" : "?";
}
const char *led_label(int)
{
    return "?";
}
const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : layer == 2 ? "Fn" : "?";
}

// the Ryos functions plus the windows shortcuts only the Sova has (folders, control panel bits). the numbers the Ryos has
// for profile 1 to 5, macro play / pause and favourites are not in roccat-tools for the Sova, they show as on a Ryos
struct type_names {
    std::string text[256];
    bool function[256] = {};
    type_names()
    {
        for (int i = 0; i < 256; ++i) {
            text[i] = RYOS_FAMILY.type_label(static_cast<uint8_t>(i));
            function[i] = RYOS_FAMILY.type_is_function(static_cast<uint8_t>(i));
        }
        const struct {
            int code;
            const char *name;
        } own[] = {
            {0x73, "mute-microphone"}, {0x74, "folder-desktop"},  {0x75, "folder-favorites"}, {0x76, "folder-fonts"},
            {0x77, "folder-documents"}, {0x78, "folder-downloads"}, {0x79, "folder-music"},  {0x7a, "folder-pictures"},
            {0x7b, "folder-network"},  {0x7c, "folder-printers"}, {0x7d, "folder-system"},   {0x9f, "hibernate"},
            {0xa0, "reboot"},          {0xa1, "lock"},            {0xa2, "logoff"},          {0xa5, "browser-new-tab"},
            {0xa6, "browser-new-window"}, {0xa8, "control-panel"}, {0xa9, "system-panel"},   {0xaa, "task-manager"},
            {0xab, "screen-settings"}, {0xac, "screensaver"},     {0xad, "themes"},          {0xae, "date-time"},
            {0xaf, "network-settings"}, {0xe8, "admin-panel"},    {0xe9, "firewall"},        {0xea, "regedit"},
            {0xeb, "event-monitor"},   {0xec, "performance-monitor"}, {0xed, "sound-settings"}, {0xee, "internet-settings"},
            {0xef, "directx-diag"},    {0xf0, "commandline"},     {0xf2, "open-document"},   {0xf3, "open-folder"},
            {0xf4, "open-website"},
        };
        for (const auto &k : own) {
            text[k.code] = k.name;
            function[k.code] = true;
        }
    }
};

const type_names &names()
{
    static const type_names N;
    return N;
}

const char *type_label(uint8_t type)
{
    return names().text[type].c_str();
}

bool type_is_function(uint8_t type)
{
    return names().function[type];
}

const family_ops SOVA_FAMILY = {firmware,  get_active,    set_active,   new_profile, no_direct, no_frame,  no_direct,
                                no_switch, direct_active, decode_event, input_label, led_label, layer_label, type_label,
                                type_is_function};

constexpr uint32_t bit(int n)
{
    return 1u << n;
}

// one backlight for the whole board on the membrane. the MK lights every key on its own (says the user, roccat-tools has no
// report for that), until somebody finds how it does that it gets the same as the membrane
#define SOVA_CAPS(tested, firmware)                                                                                          \
    {ROCKCAT_KIND_KEYBOARD, "sova", tested, firmware, ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, PROFILES, INPUTS, 3, 0, 0, 0, \
     0, 0, {0},                                                                                                              \
     bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) |  \
         bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_IMPULSE) | bit(ROCKCAT_EFFECT_HEATMAP),                          \
     0, 1, 3, -1}

const device_desc DEVICES[] = {
    // read, written back and switched on a real one (firmware 1.27), with the 2 ms select wait (a backup takes 0.7 s, not 5)
    {0x2f76, "ROCCAT Sova Membrane", &SOVA_FAMILY, nullptr, 0x06, SOVA_CAPS(ROCKCAT_TESTED_HARDWARE, "1.27"), &SOVA_KEYBOARD, 2},
    // the MK has a key table of its own in Swarm with other numbers (its led numbers, probably). the report slots are the
    // membranes as far as roccat-tools knows, so it gets the membrane table and the membrane picture for now
    {0x2f94, "ROCCAT Sova MK", &SOVA_FAMILY, nullptr, 0x06, SOVA_CAPS(ROCKCAT_TESTED_NO, ""), &SOVA_KEYBOARD},
};
const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

#include "sova_sim.inc"

} // namespace
