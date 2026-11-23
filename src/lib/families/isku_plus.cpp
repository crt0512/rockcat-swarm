// Isku+, the Isku after the Ryos came out: the Ryos reports (ids and selects) with the 144 slot primary report of the Sova
// and Skeltr, M and thumb keys like the Ryos and the 20 Easy-Shift keys of the Skeltr. all of it out of ROCCATs Swarm: the class
// isku_plus_device_c in ROCCAT_Swarm_Monitor.exe and the default profile in the module isku+.dll. never on hardware, the
// light isnt done (its report id is not known). docs/protocol/isku-plus.md

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

#include "isku_plus_tables.inc"
#include "../keyboards/isku_plus.inc"

const keyboard_layout ISKU_PLUS_KEYBOARD = {ISKU_PLUS_REGIONS, static_cast<int>(sizeof ISKU_PLUS_REGIONS / sizeof ISKU_PLUS_REGIONS[0]), 680, 370};

constexpr uint16_t PID = 0x31e2;
constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_MACRO = 0x0e, REPORT_INFO = 0x0f;
// inputs are Swarms key numbers: the primary slot for 0 to 143 (the module default has every usage in the slot Swarm gives
// its key), T1 to T3 255 to 257, Caps Lock 258
constexpr int INPUTS = 259, PROFILES = 5, SLOTS = 144, ZONE_KEYS = sizeof ZONE_KEY;

struct part {
    uint8_t id, request;
    uint16_t size;
    uint8_t profile_at;
};
enum { P_PRIMARY, P_FUNCTION, P_MACROKEYS, P_THUMB, P_EXTRA, P_EASYZONE, PARTS };
constexpr part PART[PARTS] = {{0x06, 0xa0, 149, 2}, {0x07, 0xa1, 95, 2}, {0x08, 0xa2, 35, 2},
                              {0x09, 0xa3, 23, 2},  {0x0a, 0xa4, 8, 2},  {0x0b, 0xa5, 65, 2}};
const uint8_t *const FACTORY[PARTS] = {FACTORY_PRIMARY, FACTORY_FUNCTION, FACTORY_MACROKEYS, FACTORY_THUMB, FACTORY_EXTRA, FACTORY_EASYZONE};
static_assert(sizeof FACTORY_PRIMARY == 149 && sizeof FACTORY_FUNCTION == 95 && sizeof FACTORY_MACROKEYS == 35 && sizeof FACTORY_THUMB == 23 &&
                  sizeof FACTORY_EXTRA == 8 && sizeof FACTORY_EASYZONE == 65,
              "factory reports are whole reports");

constexpr size_t offset_of(int p)
{
    size_t at = 0;
    for (int i = 0; i < p; ++i)
        at += PART[i].size;
    return at;
}
constexpr size_t TOTAL = offset_of(PARTS);

// [F_KEY], [M_KEY], [T_KEY] of the module default
constexpr uint8_t FUNCTION_INPUT[15] = {78, 118, 113, 115, 127, 99, 41, 46, 22, 119, 49, 69, 79, 16, 136};
constexpr uint8_t MACRO_INPUT[5] = {18, 83, 20, 36, 13};
constexpr int THUMB_INPUT = 255, CAPS_INPUT = 258, FN_INPUT = 2;

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
            for (int r = 0; r < ISKU_PLUS_KEYBOARD.region_count; ++r)
                for (int i = 0; i < ISKU_PLUS_KEYBOARD.regions[r].count; ++i) {
                    int led = ISKU_PLUS_KEYBOARD.regions[r].keys[i].led;
                    if (led >= 0 && led < SLOTS && led != FN_INPUT)
                        key[led] = key_place{K_PLAIN, 0};
                }
            for (int i = 0; i < ZONE_KEYS; ++i)
                key[ZONE_KEY[i]] = key_place{K_ZONE, static_cast<uint8_t>(i)};
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

// layer 0 normal, 1 Easy-Shift, 2 Fn (second half of the F key report)
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

// numbered like on the Ryos: M keys 0 to 9, thumb keys 10 to 15, the Easy-Shift side of the zone keys from 16 on (GUESS)
int macro_slot(int layer, int input)
{
    const key_place &k = place(input);
    switch (k.kind) {
    case K_MACRO: return layer <= 1 ? layer * 5 + k.at : -1;
    case K_THUMB: return layer <= 1 ? 10 + layer * 3 + k.at : -1;
    case K_ZONE:  return layer == 1 ? 16 + k.at : -1;
    default:      return -1;
    }
}

struct isku_plus_profile final : rockcat_profile {
    uint8_t raw[TOTAL] = {};
    bool dirty[PARTS] = {};
    std::vector<rockcat_macro> macros;

    using rockcat_profile::rockcat_profile;
    uint8_t *at(int p) { return raw + offset_of(p); }
    const uint8_t *at(int p) const { return raw + offset_of(p); }
    rockcat_profile *clone() const override { return new isku_plus_profile(*this); }
    bool known(int layer, int input) const { return layer >= 0 && layer < 3 && place(input).kind != K_NONE; }
    static bool shaped(const uint8_t *b, int p)
    {
        return b[0] == PART[p].id && b[1] == (PART[p].size & 0xff) && (PART[p].profile_at == 2 || b[2] == PART[p].size >> 8);
    }

    int read(rockcat_device *dev) override
    {
        std::fill(dirty, dirty + PARTS, false);
        macros.clear();
        for (int p = 0; p < PARTS; ++p) {
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
            if (!checksum_ok(b, PART[p].size))
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
        if (i < 0 || i >= PROFILES)
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
        isku_plus_profile copy(*this);
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
            std::memcpy(at(p) + PART[p].profile_at + 1, FACTORY[p] + PART[p].profile_at + 1, PART[p].size - PART[p].profile_at - 3);
        at(P_EXTRA)[3] = FACTORY_EXTRA[3];
        at(P_EXTRA)[4] = FACTORY_EXTRA[4];
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
        const uint8_t *p = FACTORY[s.part] + s.at;
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
    return new isku_plus_profile(desc, index);
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

// the Ryos way, [05 03 xy] with the active profile in the low half. GUESS, the Isku+ came out in the Ryos years
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    return RYOS_FAMILY.get_active(dev, profile, count);
}
int set_active(rockcat_device *dev, uint8_t profile)
{
    return RYOS_FAMILY.set_active(dev, profile);
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
const char *led_label(int)
{
    return "?";
}
const char *layer_label(int layer)
{
    return RYOS_FAMILY.layer_label(layer);
}
const char *type_label(uint8_t t)
{
    return RYOS_FAMILY.type_label(t);
}
bool type_is_function(uint8_t t)
{
    return RYOS_FAMILY.type_is_function(t);
}

const family_ops ISKU_PLUS_FAMILY = {firmware,  get_active,    set_active,   new_profile, no_direct, no_frame,  no_direct,
                                     no_switch, direct_active, decode_event, input_label, led_label, layer_label, type_label,
                                     type_is_function};

const device_desc DEVICES[] = {
    {PID, "ROCCAT Isku+", &ISKU_PLUS_FAMILY, nullptr, 0x06,
     {ROCKCAT_KIND_KEYBOARD, "isku-plus", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, PROFILES, INPUTS, 3, 0, 0, 0,
      0, 0, {0}, 0, 0, 0, 0, -1},
     &ISKU_PLUS_KEYBOARD},
};
const device_registration reg(DEVICES, 1);

// pretend Isku+: the module default in every profile
struct isku_plus_sim final : transport {
    uint8_t active = 0, select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> data[PROFILES][PARTS];
    std::map<int, std::vector<uint8_t>> macros;
    std::mutex lock;

    isku_plus_sim()
    {
        for (uint8_t p = 0; p < PROFILES; ++p)
            for (int i = 0; i < PARTS; ++i) {
                data[p][i].assign(FACTORY[i], FACTORY[i] + PART[i].size);
                data[p][i][PART[i].profile_at] = p;
                put_checksum(data[p][i].data(), data[p][i].size());
            }
    }
    static int part_of(uint8_t id)
    {
        for (int i = 0; i < PARTS; ++i)
            if (PART[i].id == id)
                return i;
        return -1;
    }
    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        int p = part_of(buf[0]);
        if (p >= 0) {
            if (select_b != PART[p].request || select_a >= PROFILES)
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
                out = {REPORT_PROFILE, 0x03, static_cast<uint8_t>(0x50 | active)};
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
                out = {REPORT_INFO, 0x08, 0x6c, 0, 0, 0, 0, 0};
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
        int p = part_of(buf[0]);
        if (p >= 0 || buf[0] == REPORT_MACRO) {
            const size_t want = p >= 0 ? PART[p].size : MACRO_SINGLE_SIZE;
            const uint8_t profile = p >= 0 ? buf[PART[p].profile_at] : buf[3];
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
            active = buf[2] & 0x0f;
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

isku_plus_sim &shared()
{
    static isku_plus_sim sim;
    return sim;
}

struct isku_plus_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared().lock);
        return shared().get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared().lock);
        return shared().set_feature(buf, len);
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

const sim_registration sim({"isku-plus", PID, "ROCCAT Isku+", 1, [] { return std::unique_ptr<transport>(new isku_plus_door); }});

} // namespace
