// Vulcan Pro, Vulcan TKL Pro, Vulcan II, II Max, II Mini, Pyro, Magma and Magma Mini: the newer Vulcans and their cheaper siblings (optical / Titan II switches). same feature report habits as the
// AIMO boards (select on 04, status on 04, 16 bit byte sum at the end) but five profiles on the board again, a select of four
// bytes [04 profile request 00], and key actions as 4 byte entries [00 00 code kind] instead of one Ryos byte.
// docs/protocol/vulcan-pro.md. NOBODY HERE HAS ONE: everything is out of ROCCAT_Swarm_Monitor.exe (vulcan_pro_device_c,
// vulcan_tkl_pro_device_c, vulcan2_mk_device_c, vulcan2_max_device_c, vulcan2_mini_device_c) and the Swarm modules, the firmware in
// the modules is encrypted

#include "../internal.h"
#include "../keyboards.h"
#include "../transport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {

#include "vulcan_pro_keys.inc"
#include "vulcan_tkl_pro_keys.inc"
#include "vulcan_ii_keys.inc"
#include "vulcan_ii_max_keys.inc"
#include "vulcan_ii_mini_keys.inc"
#include "pyro_keys.inc"
#include "magma_keys.inc"
#include "magma_mini_keys.inc"
#include "vulcan_pro_tables.inc"

constexpr int INPUTS = 124, PROFILES = 5;

// the reports of one profile: id, what goes into the select, size (0 = the board has none), where the profile byte is.
// 06 is what Swarm calls game_mode_default_data: one byte per key matrix slot like the AIMO 06, after a 4 byte header
enum { P_MATRIX, P_CAPS, P_FN, P_M, P_ES, P_LIGHT, PARTS };
struct part {
    uint8_t id, request;
};
constexpr part PART[PARTS] = {{0x06, 0xa1}, {0x07, 0xa2}, {0x0a, 0xa3}, {0x0b, 0xa4}, {0x0c, 0xa5}, {0x11, 0xb0}};
constexpr size_t ROOM[PARTS] = {150, 14, 294, 53, 518, 542};
constexpr size_t offset_of(int p)
{
    size_t o = 0;
    for (int i = 0; i < p; ++i)
        o += ROOM[i];
    return o;
}
constexpr size_t TOTAL = offset_of(PARTS);
// a report longer than 255 has a two byte size, so its profile byte and everything after it sit one further. 06 has a 01 after
// the profile byte
size_t head_of(int p, size_t size) { return p == P_MATRIX || size > 255 ? 4 : 3; }
size_t profile_at(int p, size_t size) { return p == P_MATRIX ? 2 : head_of(p, size) - 1; }

struct model {
    uint16_t pid;
    uint16_t size[PARTS];
    const uint8_t *factory[PARTS];
    const int16_t *slot; // key -> byte in 06 after the header
    const int16_t *rgb; // key -> red, green, blue offsets in 11
    const int16_t *fn; // keys of 0a (the second half of the report is the same keys with Fn held), -1 = no key
    int fn_count;
    const int16_t *es; // keys of 0c (Easy-Shift), -1 = no key. the II Max and Mini have one entry per matrix slot
    int es_count;
};
const model MODELS[] = {
    {0x30f7, {134, 11, 125, 45, 85, 371}, {PRO_06, PRO_07, PRO_0A, PRO_0B, PRO_0C, PRO_11}, PRO_SLOT, PRO_RGB, PRO_FN, 15, PRO_ES, 20},
    {0x311a, {134, 11, 189, 0, 85, 299}, {TKL_PRO_06, TKL_PRO_07, TKL_PRO_0A, nullptr, TKL_PRO_0C, TKL_PRO_11}, TKL_PRO_SLOT,
     TKL_PRO_RGB, TKL_PRO_FN, 23, TKL_PRO_ES, 20},
    {0x3167, {134, 11, 189, 0, 85, 299}, {TKL_PRO_06, TKL_PRO_07, TKL_PRO_0A, nullptr, TKL_PRO_0C, TKL_PRO_11}, TKL_PRO_SLOT,
     TKL_PRO_RGB, TKL_PRO_FN, 23, TKL_PRO_ES, 20},
    {0x2f4e, {134, 11, 189, 53, 89, 377}, {II_06, II_07, II_0A, II_0B, II_0C, II_11}, II_SLOT, II_RGB, II_FN, 23, II_ES, 21},
    {0x2ee2, {134, 14, 181, 53, 518, 542}, {II_MAX_06, II_MAX_07, II_MAX_0A, II_MAX_0B, II_MAX_0C, II_MAX_11}, II_MAX_SLOT, II_MAX_RGB,
     II_MAX_FN, 22, II_MAX_ES, 128},
    {0x2f09, {134, 11, 262, 0, 518, 332}, {II_MINI_06, II_MINI_07, II_MINI_0A, nullptr, II_MINI_0C, II_MINI_11}, II_MINI_SLOT,
     II_MINI_RGB, II_MINI_FN, 32, II_MINI_ES, 128},
    {0x3124, {150, 11, 157, 0, 85, 26}, {MAGMA_06, MAGMA_07, MAGMA_0A, nullptr, MAGMA_0C, MAGMA_11}, MAGMA_SLOT, MAGMA_RGB, MAGMA_FN,
     19, MAGMA_ES, 20},
    {0x69a0, {150, 11, 294, 0, 61, 26}, {MAGMA_MINI_06, MAGMA_MINI_07, MAGMA_MINI_0A, nullptr, MAGMA_MINI_0C, MAGMA_MINI_11},
     MAGMA_MINI_SLOT, MAGMA_MINI_RGB, MAGMA_MINI_FN, 36, MAGMA_MINI_ES, 14},
    {0x314c, {134, 11, 157, 0, 85, 365}, {PYRO_06, PYRO_07, PYRO_0A, nullptr, PYRO_0C, PYRO_11}, PYRO_SLOT, PYRO_RGB, PYRO_FN, 19,
     PYRO_ES, 20},
};
const model &model_of(uint16_t pid)
{
    for (const model &m : MODELS)
        if (m.pid == pid)
            return m;
    return MODELS[0];
}

// light report 11: [11 size16 profile effect speed brightness theme ?] then the colours where the rgb table says. a light report
// that fits a one byte size (the Magmas, 26 bytes) has everything one byte earlier
enum : size_t { L_EFFECT = 0, L_SPEED = 1, L_BRIGHTNESS = 2, L_THEME = 3 }; // after the header
// Swarms brightness steps (1 to 69 -> pwm), the same table as the AIMO boards (monitor 0x827780, and 0x82b8e1 for these)
constexpr uint8_t BRIGHTNESS[70] = {0,   0,   48,  51,  54,  57,  60,  63,  66,  69,  72,  75,  78,  81,  84,  87,  90,  93,
                                    96,  99,  102, 105, 108, 111, 114, 117, 120, 123, 126, 129, 132, 135, 138, 141, 144, 147,
                                    150, 153, 156, 159, 162, 165, 168, 171, 174, 177, 180, 183, 186, 189, 192, 195, 198, 201,
                                    204, 207, 210, 213, 216, 219, 222, 225, 228, 231, 234, 237, 240, 245, 250, 255};

// a key action: [a b code kind]. kind 0c = the key with HID usage `code`, 07 = macro, the rest are functions. rockcat shows them
// with the Ryos names: what the AIMO boards have on the same key out of the box is what these have, so Fn + F1..F4 [08 04..07]
// is Ryos b3..b6, Fn + F5..F8 [03 10 09 12 11] is c2 c3 c0 c1, Fn + F9..F12 [03 02 05 04 03] is ce cf cc cd. 03 06/07/08 as
// mute / volume up / down is a GUESS (the volume knob of the Pro has 07 and 08). everything else is kept as it is and shows as
// KEPT with code and kind in the two parameters. [00 00 00 0c] is Ryos 00, disabled
constexpr uint8_t KEPT = 0xfe;
constexpr uint8_t KIND_KEY = 0x0c;
constexpr uint8_t FUNCTIONS[][3] = {
    {0x0c, 0x00, 0x00}, // the key with usage 0: nothing, the II Mini has that on every Easy-Shift key out of the box
    {0x08, 0x04, 0xb3}, {0x08, 0x05, 0xb4}, {0x08, 0x06, 0xb5}, {0x08, 0x07, 0xb6}, {0x03, 0x02, 0xce}, {0x03, 0x03, 0xcd},
    {0x03, 0x04, 0xcc}, {0x03, 0x05, 0xcf}, {0x03, 0x06, 0xd2}, {0x03, 0x07, 0xd0}, {0x03, 0x08, 0xd1}, {0x03, 0x09, 0xc3},
    {0x03, 0x10, 0xc2}, {0x03, 0x11, 0xc1}, {0x03, 0x12, 0xc0},
};

bool is_function(uint8_t t) { return RYOS_FAMILY.type_is_function(t); }

rockcat_assignment decode(const uint8_t *e)
{
    if (e[0] == 0 && e[1] == 0) {
        if (e[3] == KIND_KEY && !is_function(e[2]))
            return rockcat_assignment{e[2], 0, 0};
        for (const auto &f : FUNCTIONS)
            if (f[0] == e[3] && f[1] == e[2])
                return rockcat_assignment{f[2], 0, 0};
    }
    return rockcat_assignment{KEPT, e[2], e[3]};
}

// false = nothing we can write for that
bool encode(const rockcat_assignment &a, uint8_t *e)
{
    if (a.type == KEPT) {
        e[0] = e[1] = 0;
        e[2] = a.param1;
        e[3] = a.param2;
        return true;
    }
    if (a.param1 || a.param2)
        return false;
    if (!is_function(a.type)) {
        const uint8_t out[4] = {0, 0, a.type, KIND_KEY};
        std::memcpy(e, out, 4);
        return true;
    }
    for (const auto &f : FUNCTIONS)
        if (f[2] == a.type) {
            const uint8_t out[4] = {0, 0, f[1], f[0]};
            std::memcpy(e, out, 4);
            return true;
        }
    return false;
}

int index_in(const int16_t *list, int n, int key)
{
    for (int i = 0; i < n; ++i)
        if (list[i] == key)
            return i;
    return -1;
}

// where a key sits on a layer: a 4 byte entry in a part, or (normal layer, everything else) one byte in 06
struct spot {
    int part = -1;
    size_t at = 0;
    bool entry = false;
};

spot find(const model &m, int layer, int key)
{
    spot s;
    const int f = index_in(m.fn, m.fn_count, key);
    const size_t fh = head_of(P_FN, m.size[P_FN]);
    if (layer == 0) {
        if (f >= 0)
            s = spot{P_FN, fh + 4 * static_cast<size_t>(f), true};
        else if (m.slot[key] >= 0)
            s = spot{P_MATRIX, 4 + static_cast<size_t>(m.slot[key]), false};
    } else if (layer == 1) {
        int e = index_in(m.es, m.es_count, key);
        if (e >= 0)
            s = spot{P_ES, head_of(P_ES, m.size[P_ES]) + 4 * static_cast<size_t>(e), true};
    } else if (layer == 2 && f >= 0) {
        s = spot{P_FN, fh + 4 * static_cast<size_t>(m.fn_count + f), true};
    }
    return s;
}

int select_part(rockcat_device *dev, uint8_t profile, uint8_t request)
{
    const uint8_t sel[4] = {REPORT_CONTROL, profile, request, 0};
    int r = set_raw(dev, sel, sizeof sel, false);
    return r == ROCKCAT_OK ? wait_ready_after(dev, dev->wait.select_delay_ms) : r;
}

struct pro_profile final : rockcat_profile {
    uint8_t data[TOTAL] = {};
    bool dirty[PARTS] = {};

    using rockcat_profile::rockcat_profile;

    const model &board() const { return model_of(desc->pid); }
    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new pro_profile(*this); }
    uint8_t *at(int p) { return data + offset_of(p); }
    const uint8_t *at(int p) const { return data + offset_of(p); }
    size_t size_of(int p) const { return board().size[p]; }

    int read_parts(rockcat_device *dev, bool light_only)
    {
        for (int p = light_only ? P_LIGHT : 0; p < PARTS; ++p) {
            dirty[p] = false;
            size_t n = size_of(p);
            if (!n)
                continue;
            int r = select_part(dev, static_cast<uint8_t>(index), PART[p].request);
            if (r != ROCKCAT_OK)
                return r;
            uint8_t *b = at(p);
            std::memset(b, 0, ROOM[p]);
            b[0] = PART[p].id;
            r = get_raw(dev, b, n);
            if (r != ROCKCAT_OK)
                return r;
            if (b[0] != PART[p].id || b[1] != (n & 0xff))
                return ROCKCAT_ERR_PROTOCOL;
            if (!checksum_ok(b, n))
                return ROCKCAT_ERR_CHECKSUM;
        }
        return ROCKCAT_OK;
    }

    int read(rockcat_device *dev) override { return read_parts(dev, false); }
    int read_light(rockcat_device *dev) override { return read_parts(dev, true); }

    int write(rockcat_device *dev) override
    {
        for (int p = 0; p < PARTS; ++p) {
            size_t n = size_of(p);
            if (!dirty[p] || !n)
                continue;
            uint8_t *b = at(p);
            b[profile_at(p, n)] = static_cast<uint8_t>(index);
            put_checksum(b, n);
            int r = set_raw(dev, b, n, true);
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
            if (size_of(p))
                at(p)[profile_at(p, size_of(p))] = static_cast<uint8_t>(i);
            dirty[p] = true;
        }
        return ROCKCAT_OK;
    }

    // the parts back to back, each as long as it is on this board
    void export_payload(std::vector<uint8_t> &out) const override
    {
        for (int p = 0; p < PARTS; ++p)
            out.insert(out.end(), at(p), at(p) + size_of(p));
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        size_t total = 0;
        for (int p = 0; p < PARTS; ++p)
            total += size_of(p);
        if (len != total)
            return ROCKCAT_ERR_ARGUMENT;
        size_t o = 0;
        for (int p = 0; p < PARTS; o += size_of(p), ++p)
            if (size_of(p) && buf[o] != PART[p].id)
                return ROCKCAT_ERR_ARGUMENT;
        o = 0;
        for (int p = 0; p < PARTS; o += size_of(p), ++p)
            std::memcpy(at(p), buf + o, size_of(p));
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = at(P_LIGHT) + head_of(P_LIGHT, size_of(P_LIGHT));
        switch (which) {
        case ROCKCAT_SET_EFFECT:       *value = l[L_EFFECT]; break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = l[L_SPEED]; break;
        case ROCKCAT_SET_BRIGHTNESS:   *value = BRIGHTNESS[l[L_BRIGHTNESS] < 70 ? l[L_BRIGHTNESS] : 69]; break;
        case ROCKCAT_SET_COLOR_THEME:  *value = l[L_THEME] & 0x7f; break;
        default:                       return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        uint8_t *l = at(P_LIGHT) + head_of(P_LIGHT, size_of(P_LIGHT));
        switch (which) {
        case ROCKCAT_SET_EFFECT:
            if (value < 0 || value > 31 || !(caps().effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            l[L_EFFECT] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT_SPEED:
            if (value < caps().effect_speed_min || value > caps().effect_speed_max)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_SPEED] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_BRIGHTNESS: {
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            int best = 1;
            for (int i = 1; i < 70; ++i)
                if (std::abs(BRIGHTNESS[i] - value) < std::abs(BRIGHTNESS[best] - value))
                    best = i;
            l[L_BRIGHTNESS] = static_cast<uint8_t>(best);
            break;
        }
        case ROCKCAT_SET_COLOR_THEME:
            if (value < 0 || value >= ROCKCAT_AIMO_THEMES)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_THEME] = static_cast<uint8_t>((l[L_THEME] & 0x80) | value);
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    const int16_t *rgb_of(int led) const
    {
        if (led <= 0 || led >= INPUTS)
            return nullptr;
        const int16_t *c = board().rgb + 3 * led;
        return c[0] >= 0 ? c : nullptr;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        const int16_t *o = rgb_of(led);
        if (!o)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = at(P_LIGHT);
        if (c)
            *c = rockcat_color{255, l[o[0]], l[o[1]], l[o[2]]};
        if (enabled)
            *enabled = 1;
        return ROCKCAT_OK;
    }

    // no "this key is off" either, a dark key is black and alpha goes into the colour
    int set_led(int led, rockcat_color c, int enabled) override
    {
        const int16_t *o = rgb_of(led);
        if (!o)
            return ROCKCAT_ERR_ARGUMENT;
        int a = enabled ? c.a : 0;
        uint8_t *l = at(P_LIGHT);
        l[o[0]] = static_cast<uint8_t>((c.r * a + 127) / 255);
        l[o[1]] = static_cast<uint8_t>((c.g * a + 127) / 255);
        l[o[2]] = static_cast<uint8_t>((c.b * a + 127) / 255);
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    bool known(int layer, int input) const { return layer >= 0 && layer < caps().layer_count && input > 0 && input < INPUTS; }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = at(s.part) + s.at;
        *a = s.entry ? decode(p) : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        uint8_t *p = at(s.part) + s.at;
        if (s.entry) {
            uint8_t e[4];
            if (!encode(*a, e))
                return ROCKCAT_ERR_ARGUMENT;
            // a KEPT that is what is there already leaves the entry alone (its first two bytes too)
            rockcat_assignment now = decode(p);
            if (a->type == KEPT && now.type == KEPT && now.param1 == a->param1 && now.param2 == a->param2)
                return ROCKCAT_OK;
            std::memcpy(p, e, 4);
        } else {
            // 06 holds plain keys only
            if (a->param1 || a->param2 || a->type == KEPT || is_function(a->type))
                return ROCKCAT_ERR_ARGUMENT;
            p[0] = a->type;
        }
        dirty[s.part] = true;
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (!known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        return find(board(), layer, input).part < 0 ? 0 : ROCKCAT_ROOM_KEY;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = board().factory[s.part] + s.at;
        *a = s.entry ? decode(p) : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    // everything after the header, the profile byte stays
    int reset_inputs() override
    {
        for (int p : {P_MATRIX, P_FN, P_ES}) {
            size_t head = head_of(p, size_of(p));
            std::memcpy(at(p) + head, board().factory[p] + head, size_of(p) - head - 2);
            dirty[p] = true;
        }
        return ROCKCAT_OK;
    }
};

rockcat_profile *new_profile(const device_desc *desc, int index)
{
    return new pro_profile(desc, index);
}

// no info report found, bcdDevice like on the other Vulcans
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    std::snprintf(buf, len, "%x.%02x", dev->info.release >> 8, dev->info.release & 0xff);
    return ROCKCAT_OK;
}

// [05 04 active count]. the monitor reads it like that for these (FUN_004b1e10, the 4 byte branch) and writes it back to switch
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t rep[4] = {0x05};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = rep[2] < PROFILES ? rep[2] : 0;
    if (count)
        *count = rep[3] >= 1 && rep[3] <= PROFILES ? rep[3] : PROFILES;
    return ROCKCAT_OK;
}

int set_active(rockcat_device *dev, uint8_t profile)
{
    if (profile >= PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    const uint8_t rep[4] = {0x05, 0x04, profile, PROFILES};
    return set_raw(dev, rep, sizeof rep, true);
}

// light from the pc goes out on another interface, like on the AIMO boards. not yet
int direct_begin(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_frame(rockcat_device *, const rockcat_color *, int) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_end(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_switch(rockcat_device *, bool) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_active(rockcat_device *) { return 0; }
bool decode_event(const uint8_t *, int, rockcat_event *) { return false; }
const char *input_label(int) { return "?"; }
const char *led_label(int) { return "?"; }
const char *layer_label(int layer) { return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : layer == 2 ? "Fn" : "?"; }

const char *type_label(uint8_t t) { return t == KEPT ? "kept" : RYOS_FAMILY.type_label(t); }
bool type_is_function(uint8_t t) { return t == KEPT || is_function(t); }

const family_ops VULCAN_PRO_FAMILY = {firmware,     get_active,   set_active,    new_profile, direct_begin, direct_frame, direct_end,
                                      direct_switch, direct_active, decode_event, input_label, led_label,    layer_label,  type_label,
                                      type_is_function};

constexpr uint32_t bit(int n) { return 1u << n; }

#define VULCAN_PRO_CAPS                                                                                                              \
    {ROCKCAT_KIND_KEYBOARD, "vulcan-pro", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB, PROFILES, INPUTS, 3, INPUTS, \
     0, 0, 0, 0, {0},                                                                                                                \
     bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_HEARTBEAT),    \
     0, 1, 11, -1}

const keyboard_layout PRO_KEYBOARD = {VULCAN_PRO_REGIONS, static_cast<int>(sizeof VULCAN_PRO_REGIONS / sizeof VULCAN_PRO_REGIONS[0]), 680, 370};
const keyboard_layout TKL_PRO_KEYBOARD = {VULCAN_TKL_PRO_REGIONS,
                                          static_cast<int>(sizeof VULCAN_TKL_PRO_REGIONS / sizeof VULCAN_TKL_PRO_REGIONS[0]), 680, 370};
const keyboard_layout II_KEYBOARD = {VULCAN_II_REGIONS, static_cast<int>(sizeof VULCAN_II_REGIONS / sizeof VULCAN_II_REGIONS[0]), 680, 370};
const keyboard_layout PYRO_KEYBOARD = {PYRO_REGIONS, static_cast<int>(sizeof PYRO_REGIONS / sizeof PYRO_REGIONS[0]), 680, 370};
const keyboard_layout MAGMA_KEYBOARD = {MAGMA_REGIONS, static_cast<int>(sizeof MAGMA_REGIONS / sizeof MAGMA_REGIONS[0]), 680, 370};
const keyboard_layout MAGMA_MINI_KEYBOARD = {MAGMA_MINI_REGIONS, static_cast<int>(sizeof MAGMA_MINI_REGIONS / sizeof MAGMA_MINI_REGIONS[0]),
                                             680, 370};
const keyboard_layout II_MAX_KEYBOARD = {VULCAN_II_MAX_REGIONS,
                                         static_cast<int>(sizeof VULCAN_II_MAX_REGIONS / sizeof VULCAN_II_MAX_REGIONS[0]), 680, 370};
const keyboard_layout II_MINI_KEYBOARD = {VULCAN_II_MINI_REGIONS,
                                          static_cast<int>(sizeof VULCAN_II_MINI_REGIONS / sizeof VULCAN_II_MINI_REGIONS[0]), 680, 370};

const device_desc DEVICES[] = {
    {0x30f7, "ROCCAT Vulcan Pro", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &PRO_KEYBOARD},
    {0x311a, "ROCCAT Vulcan TKL Pro", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &TKL_PRO_KEYBOARD},
    {0x3167, "ROCCAT Vulcan TKL Pro", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &TKL_PRO_KEYBOARD},
    {0x2f4e, "ROCCAT Vulcan II", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &II_KEYBOARD},
    {0x2ee2, "ROCCAT Vulcan II Max", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &II_MAX_KEYBOARD},
    {0x2f09, "ROCCAT Vulcan II Mini", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &II_MINI_KEYBOARD},
    {0x314c, "ROCCAT Pyro", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &PYRO_KEYBOARD},
    {0x3124, "ROCCAT Magma", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &MAGMA_KEYBOARD},
    {0x69a0, "ROCCAT Magma Mini", &VULCAN_PRO_FAMILY, nullptr, 0x11, VULCAN_PRO_CAPS, &MAGMA_MINI_KEYBOARD},
};

const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// the pretend ones: five profiles of every report, starting on the factory ones. answers the 4 byte select, refuses a write with a
// wrong size, checksum or profile byte
struct pro_sim final : transport {
    const model &m;
    std::map<int, std::vector<uint8_t>> store; // profile * 256 + id
    uint8_t active = 0, sel_profile = 0, sel_request = 0;
    std::vector<int> status_queue;
    std::mutex lock;

    explicit pro_sim(const model &mm) : m(mm)
    {
        for (int p = 0; p < PARTS; ++p) {
            if (!m.size[p])
                continue;
            for (uint8_t i = 0; i < PROFILES; ++i) {
                std::vector<uint8_t> b(m.factory[p], m.factory[p] + m.size[p]);
                b[profile_at(p, b.size())] = i;
                put_checksum(b.data(), b.size());
                store[i * 256 + PART[p].id] = b;
            }
        }
    }

    int part_of(uint8_t id) const
    {
        for (int p = 0; p < PARTS; ++p)
            if (PART[p].id == id && m.size[p])
                return p;
        return -1;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        if (buf[0] == 0x04) {
            int st = 1;
            if (!status_queue.empty()) {
                st = status_queue.front();
                status_queue.erase(status_queue.begin());
            }
            out = {0x04, static_cast<uint8_t>(st), 0};
        } else if (buf[0] == 0x05) {
            out = {0x05, 0x04, active, PROFILES};
        } else {
            int p = part_of(buf[0]);
            if (p < 0 || PART[p].request != sel_request || sel_profile >= PROFILES)
                return ROCKCAT_ERR_IO;
            out = store[sel_profile * 256 + buf[0]];
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        if (buf[0] == 0x04) {
            if (len != 4)
                return ROCKCAT_ERR_IO;
            sel_profile = buf[1];
            sel_request = buf[2];
            return ROCKCAT_OK;
        }
        if (buf[0] == 0x05) {
            if (len != 4 || buf[2] >= PROFILES)
                return ROCKCAT_ERR_IO;
            active = buf[2];
            return ROCKCAT_OK;
        }
        int p = part_of(buf[0]);
        if (p < 0 || len != m.size[p])
            return ROCKCAT_ERR_IO;
        uint8_t profile = buf[profile_at(p, len)];
        if (profile >= PROFILES || !checksum_ok(buf, len) || (p == P_LIGHT && (buf[head_of(p, len) + L_SPEED] == 0 || buf[head_of(p, len) + L_SPEED] > 11))) {
            status_queue = {2};
            return ROCKCAT_OK;
        }
        store[profile * 256 + buf[0]].assign(buf, buf + len);
        status_queue = {3};
        return ROCKCAT_OK;
    }

    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

pro_sim &shared(uint16_t pid)
{
    static std::map<uint16_t, pro_sim> sims;
    static std::mutex guard;
    std::lock_guard<std::mutex> g(guard);
    auto it = sims.find(pid);
    if (it == sims.end())
        it = sims.emplace(std::piecewise_construct, std::forward_as_tuple(pid), std::forward_as_tuple(model_of(pid))).first;
    return it->second;
}

template <uint16_t PID> struct door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        pro_sim &s = shared(PID);
        std::lock_guard<std::mutex> g(s.lock);
        return s.get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        pro_sim &s = shared(PID);
        std::lock_guard<std::mutex> g(s.lock);
        return s.set_feature(buf, len);
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

const sim_registration sim_pro({"vulcan-pro", 0x30f7, "ROCCAT Vulcan Pro", 1, [] { return std::unique_ptr<transport>(new door<0x30f7>); }});
const sim_registration sim_tkl_pro({"vulcan-tkl-pro", 0x311a, "ROCCAT Vulcan TKL Pro", 1,
                                    [] { return std::unique_ptr<transport>(new door<0x311a>); }});
const sim_registration sim_ii_max({"vulcan-ii-max", 0x2ee2, "ROCCAT Vulcan II Max", 1,
                                   [] { return std::unique_ptr<transport>(new door<0x2ee2>); }});
const sim_registration sim_ii_mini({"vulcan-ii-mini", 0x2f09, "ROCCAT Vulcan II Mini", 1,
                                    [] { return std::unique_ptr<transport>(new door<0x2f09>); }});
const sim_registration sim_magma({"magma", 0x3124, "ROCCAT Magma", 1, [] { return std::unique_ptr<transport>(new door<0x3124>); }});
const sim_registration sim_magma_mini({"magma-mini", 0x69a0, "ROCCAT Magma Mini", 1,
                                       [] { return std::unique_ptr<transport>(new door<0x69a0>); }});
const sim_registration sim_pyro({"pyro", 0x314c, "ROCCAT Pyro", 1, [] { return std::unique_ptr<transport>(new door<0x314c>); }});
const sim_registration sim_ii({"vulcan-ii", 0x2f4e, "ROCCAT Vulcan II", 1, [] { return std::unique_ptr<transport>(new door<0x2f4e>); }});

} // namespace
