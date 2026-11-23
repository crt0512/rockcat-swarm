// Vulcan 100 AIMO and Vulcan 120/121/122 AIMO. the Ryos generation of the protocol (select on 04, key reports 06 07 0a 0b, lights in 0d)
// but the keyboard keeps ONE set of everything, Swarm had the five profiles on the pc and wrote whichever was active. docs/protocol/vulcan.md
// nobody here has one. everything is out of the Swarm module, ROCCAT_Swarm_Monitor.exe and the keyboard firmware, see the doc for what came from where

#include "../internal.h"
#include "../keyboards.h"
#include "../sim_vulcan.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#include "vulcan_keys.inc"
#include "vulcan_tables.inc"

const keyboard_layout VULCAN_AIMO_KEYBOARD = {VULCAN_AIMO_REGIONS, static_cast<int>(sizeof VULCAN_AIMO_REGIONS / sizeof VULCAN_AIMO_REGIONS[0]), 680, 370};

// key numbers are Swarms (kb_matrix.xml index), 1 to 113. FX and the volume wheel (112, 113) have no byte anywhere we understand
constexpr int INPUTS = 114, LEDS = 112;

// the parts of a profile: report id, size, what goes into the select. sizes are what the firmware (VULCAN_AIMO_v1.36) checks.
// except 0b: the firmware takes 65 bytes ([0b 41 profile] + 20 keys), Swarm keeps a 294 byte one ([0b 26 01 profile] + 96 keys,
// the Ryos layout) in its default profile. maybe a newer firmware, maybe Swarm being Swarm. we keep room for the big one and
// use whichever the keyboard answers with
struct part {
    uint8_t id, request;
    uint16_t size;
};
enum { P_KEYS, P_FKEYS, P_EXTEND, P_CAPSFN, P_EASY, P_LIGHT, PARTS };
constexpr part PART[PARTS] = {{0x06, 0xa0, 133}, {0x07, 0xa1, 95}, {0x09, 0xa3, 43}, {0x0a, 0xa4, 8}, {0x0b, 0xa5, 294}, {0x0d, 0xb1, 443}};
constexpr size_t EASY_SHORT = 65;

// how long the 0b in front of us really is, and where its first key starts. 0 = makes no sense
size_t easy_length(const uint8_t *b)
{
    if (b[1] == EASY_SHORT)
        return EASY_SHORT;
    if (b[1] == (PART[P_EASY].size & 0xff) && b[2] == (PART[P_EASY].size >> 8))
        return PART[P_EASY].size;
    return 0;
}
size_t easy_head(const uint8_t *b) { return easy_length(b) == EASY_SHORT ? 3 : 4; }

constexpr size_t offset_of(int p)
{
    size_t o = 0;
    for (int i = 0; i < p; ++i)
        o += PART[i].size;
    return o;
}
constexpr size_t TOTAL = offset_of(PARTS);

// light report 0d: [0d bb 01 profile effect speed ? brightness theme] then the colours, then the checksum
enum : size_t { L_EFFECT = 4, L_SPEED = 5, L_BRIGHTNESS = 7, L_THEME = 8, L_COLOURS = 9 };

// Swarms table for the brightness byte (index 1 to 69 -> pwm), out of ROCCAT_Swarm_Monitor.exe at 0x827780
constexpr uint8_t BRIGHTNESS[70] = {0,   0,   48,  51,  54,  57,  60,  63,  66,  69,  72,  75,  78,  81,  84,  87,  90,  93,
                                    96,  99,  102, 105, 108, 111, 114, 117, 120, 123, 126, 129, 132, 135, 138, 141, 144, 147,
                                    150, 153, 156, 159, 162, 165, 168, 171, 174, 177, 180, 183, 186, 189, 192, 195, 198, 201,
                                    204, 207, 210, 213, 216, 219, 222, 225, 228, 231, 234, 237, 240, 245, 250, 255};

// which keys have three bytes where. F1 to Pause are key 2 to 16, normal in the first half of 07, with Fn in the second half.
// the 20 Easy-Shift keys are the ones out of the modules key_data_mapping.txt (1 to 5, Q to T, A to G, Z to B on a US board)
constexpr int EASY_KEYS = 20;
constexpr uint8_t EASY_KEY[EASY_KEYS] = {19, 20, 21, 22, 23, 41, 42, 43, 44, 45, 63, 64, 65, 66, 67, 82, 83, 84, 85, 86};
constexpr int CAPS_KEY = 62, FN_KEY = 104;

int easy_index(int key)
{
    for (int i = 0; i < EASY_KEYS; ++i)
        if (EASY_KEY[i] == key)
            return i;
    return -1;
}

bool is_fkey(int key) { return key >= 2 && key <= 16; }

bool is_key(int key)
{
    // FX and VOLUME (112, 113) are keys too, they just have nothing we know of on any layer
    return key > 0 && key < INPUTS && (KEY_SLOT[key] >= 0 || key == CAPS_KEY || key == FN_KEY || key >= 112);
}

// where the byte(s) of a key on a layer are: part, offset, three bytes or one. part < 0 = nothing there
struct spot {
    int part = -1;
    size_t at = 0;
    bool wide = false;
};

spot find(int layer, int key)
{
    spot s;
    if (layer == 0) {
        if (is_fkey(key))
            s = spot{P_FKEYS, 3 + 3 * static_cast<size_t>(key - 2), true};
        else if (key == CAPS_KEY)
            s = spot{P_CAPSFN, 3, false};
        else if (key == FN_KEY)
            s = spot{P_CAPSFN, 4, false};
        else if (KEY_SLOT[key] >= 0)
            s = spot{P_KEYS, 3 + static_cast<size_t>(KEY_SLOT[key]), false};
    } else if (layer == 1) {
        int e = easy_index(key);
        if (e >= 0)
            s = spot{P_EASY, 3 * static_cast<size_t>(e), true}; // from the first key, the header length depends on the report
    } else if (layer == 2) {
        if (is_fkey(key))
            s = spot{P_FKEYS, 3 + 3 * static_cast<size_t>(15 + key - 2), true};
    }
    return s;
}

const uint8_t *factory(int p)
{
    switch (p) {
    case P_KEYS:   return FACTORY_06;
    case P_FKEYS:  return FACTORY_07;
    case P_CAPSFN: return FACTORY_0A;
    case P_EASY:   return FACTORY_0B;
    default:       return nullptr;
    }
}

// device effect byte <-> rockcat_effect. 1 to 4 are the numbers every Swarm device uses and the effect list in utility.dll has them,
// the module itself stores 9 for AIMO. anything else is read as is and not offered
int effect_from_device(uint8_t v) { return v; }

struct vulcan_profile final : rockcat_profile {
    uint8_t data[TOTAL] = {};
    bool dirty[PARTS] = {};

    using rockcat_profile::rockcat_profile;

    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new vulcan_profile(*this); }
    uint8_t *at(int p) { return data + offset_of(p); }
    const uint8_t *at(int p) const { return data + offset_of(p); }
    size_t length(int p) const { return p == P_EASY ? easy_length(at(p)) : PART[p].size; }
    size_t where(const spot &s) const { return s.part == P_EASY ? easy_head(at(P_EASY)) + s.at : s.at; }

    int read(rockcat_device *dev) override
    {
        for (int p = 0; p < PARTS; ++p) {
            dirty[p] = false;
            int r = select_data(dev, 0, PART[p].request);
            if (r != ROCKCAT_OK)
                return r;
            uint8_t *b = at(p);
            std::memset(b, 0, PART[p].size);
            b[0] = PART[p].id;
            r = get_raw(dev, b, PART[p].size);
            if (r != ROCKCAT_OK)
                return r;
            size_t n = length(p);
            if (n == 0 || b[1] != (n & 0xff))
                return ROCKCAT_ERR_PROTOCOL;
            if (!checksum_ok(b, n))
                return ROCKCAT_ERR_CHECKSUM;
        }
        return ROCKCAT_OK;
    }

    int read_light(rockcat_device *dev) override { return read(dev); }

    int write(rockcat_device *dev) override
    {
        for (int p = 0; p < PARTS; ++p) {
            if (!dirty[p])
                continue;
            uint8_t *b = at(p);
            put_checksum(b, length(p));
            int r = set_raw(dev, b, length(p), true);
            if (r != ROCKCAT_OK)
                return r;
            dirty[p] = false;
        }
        return ROCKCAT_OK;
    }

    // one stored setup and nothing else, so there is only "profile" 0
    int set_index(int i) override
    {
        if (i != 0)
            return ROCKCAT_ERR_ARGUMENT;
        for (bool &d : dirty)
            d = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override { out.insert(out.end(), data, data + TOTAL); }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != TOTAL)
            return ROCKCAT_ERR_ARGUMENT;
        for (int p = 0; p < PARTS; ++p)
            if (buf[offset_of(p)] != PART[p].id)
                return ROCKCAT_ERR_ARGUMENT;
        if (easy_length(buf + offset_of(P_EASY)) == 0)
            return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(data, buf, TOTAL);
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = at(P_LIGHT);
        switch (which) {
        case ROCKCAT_SET_EFFECT:       *value = effect_from_device(l[L_EFFECT]); break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = l[L_SPEED]; break;
        case ROCKCAT_SET_BRIGHTNESS:   *value = BRIGHTNESS[l[L_BRIGHTNESS] < 70 ? l[L_BRIGHTNESS] : 69]; break;
        case ROCKCAT_SET_COLOR_THEME:  *value = l[L_THEME] & 0x7f; break;
        default:                       return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        uint8_t *l = at(P_LIGHT);
        switch (which) {
        case ROCKCAT_SET_EFFECT:
            if (value < 0 || value > 31 || !(caps().effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            l[L_EFFECT] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT_SPEED:
            // the firmware throws the whole report away for anything outside 1 to 11
            if (value < caps().effect_speed_min || value > caps().effect_speed_max)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_SPEED] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_BRIGHTNESS: {
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            // and for a brightness byte of 0 too, so the darkest we can ask for is step 1 (which Swarm maps to 0 anyway)
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

    // colours sit in blocks of 12 leds: 12 red, 12 green, 12 blue, next block
    static size_t colour_at(int slot) { return L_COLOURS + 36 * static_cast<size_t>(slot / 12) + static_cast<size_t>(slot % 12); }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= LEDS || KEY_LED[led] < 0)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *p = at(P_LIGHT) + colour_at(KEY_LED[led]);
        if (c)
            *c = rockcat_color{255, p[0], p[12], p[24]};
        if (enabled)
            *enabled = 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= LEDS || KEY_LED[led] < 0)
            return ROCKCAT_ERR_ARGUMENT;
        // there is no "this key is off", a dark key is black. alpha goes into the colour, the brightness is for the whole board
        int a = enabled ? c.a : 0;
        uint8_t *p = at(P_LIGHT) + colour_at(KEY_LED[led]);
        p[0] = static_cast<uint8_t>((c.r * a + 127) / 255);
        p[12] = static_cast<uint8_t>((c.g * a + 127) / 255);
        p[24] = static_cast<uint8_t>((c.b * a + 127) / 255);
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || !is_key(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = at(s.part) + where(s);
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || !is_key(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        if (!s.wide && (a->param1 || a->param2))
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *p = at(s.part) + where(s);
        p[0] = a->type;
        if (s.wide) {
            p[1] = a->param1;
            p[2] = a->param2;
        }
        dirty[s.part] = true;
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (layer < 0 || layer >= caps().layer_count || !is_key(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return 0;
        return s.wide ? ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS : ROCKCAT_ROOM_KEY;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || !is_key(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        const uint8_t *t = s.part >= 0 ? factory(s.part) : nullptr;
        if (!t)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = t + (s.part == P_EASY ? 3 : 0) + s.at;
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        // everything after the three byte header, the profile byte stays whatever the keyboard said
        for (int p : {P_KEYS, P_FKEYS, P_CAPSFN}) {
            std::memcpy(at(p) + 3, factory(p) + 3, PART[p].size - 5);
            dirty[p] = true;
        }
        // the factory table has the 20 keys, a long 0b gets those and empty entries after them
        size_t n = length(P_EASY), head = easy_head(at(P_EASY));
        std::memset(at(P_EASY) + head, 0, n - head - 2);
        std::memcpy(at(P_EASY) + head, FACTORY_0B + 3, 3 * EASY_KEYS);
        dirty[P_EASY] = true;
        return ROCKCAT_OK;
    }
};

rockcat_profile *new_profile(const device_desc *desc, int index)
{
    return new vulcan_profile(desc, index);
}

// nobody found an info report for this one, bcdDevice is what Windows showed as the version. that it matches the firmware is a guess
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    std::snprintf(buf, len, "%x.%02x", dev->info.release >> 8, dev->info.release & 0xff);
    return ROCKCAT_OK;
}

// report 05 is [05 04 active count] like on the mice, but the keyboard has one setup only and the number is just a label Swarm used
// to pick which of its pc side profiles to send. so: always profile 0 of 1, after making sure the keyboard answers at all
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t rep[4] = {0x05};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = 0;
    if (count)
        *count = 1;
    return ROCKCAT_OK;
}

int set_active(rockcat_device *, uint8_t profile)
{
    return profile == 0 ? ROCKCAT_OK : ROCKCAT_ERR_ARGUMENT;
}

// lights from the pc go out as 64 byte output reports on another interface (usage page 1, usage 0), see the doc.
// our transport only does feature reports on one node, so not yet
int direct_begin(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_frame(rockcat_device *, const rockcat_color *, int) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_end(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_switch(rockcat_device *, bool) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_active(rockcat_device *) { return 0; }

// what the keyboard sends on its own is not worked out
bool decode_event(const uint8_t *, int, rockcat_event *) { return false; }

const char *input_label(int) { return "?"; }
const char *led_label(int) { return "?"; }

const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : layer == 2 ? "Fn" : "?";
}

// the key and function codes are the ones of the Ryos (Swarms defaults for this board use the same: f1 Fn, ff Easy-Shift,
// cc..cf media, b3..b6 on Fn + F1..F4), so the names come from there
const char *type_label(uint8_t t) { return RYOS_FAMILY.type_label(t); }
bool type_is_function(uint8_t t) { return RYOS_FAMILY.type_is_function(t); }

const family_ops VULCAN_FAMILY = {firmware,     get_active,   set_active,    new_profile, direct_begin, direct_frame, direct_end,
                                  direct_switch, direct_active, decode_event, input_label, led_label,    layer_label,  type_label,
                                  type_is_function};

constexpr uint32_t bit(int n) { return 1u << n; }

// 1 profile (see get_active), keys numbered like Swarm, 112 led numbers of which 111 are real keys, three layers like the Ryos.
// effects: only the four whose numbers every Swarm device shares. speed 1 to 11 is what the firmware lets through
#define VULCAN_CAPS                                                                                                                  \
    {ROCKCAT_KIND_KEYBOARD, "vulcan", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB, 1, INPUTS, 3, LEDS, 0, 0, 0, 0, \
     {0}, bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_HEARTBEAT), \
     0, 1, 11, -1}

const device_desc DEVICES[] = {
    {0x307a, "ROCCAT Vulcan 100 AIMO", &VULCAN_FAMILY, nullptr, 0x0d, VULCAN_CAPS, &VULCAN_AIMO_KEYBOARD},
    {0x3098, "ROCCAT Vulcan 120 AIMO", &VULCAN_FAMILY, nullptr, 0x0d, VULCAN_CAPS, &VULCAN_AIMO_KEYBOARD},
};

const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// the pretend one, shared by every handle in the process like the Ryos one, so the gui can write and read back
vulcan_sim &shared()
{
    static vulcan_sim sim(FACTORY_06, FACTORY_07, FACTORY_0A, FACTORY_0B, FACTORY_0D);
    return sim;
}

struct door final : transport {
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

const sim_registration sim({"vulcan-120-aimo", 0x3098, "ROCCAT Vulcan 120 AIMO", 1, [] { return std::unique_ptr<transport>(new door); }});

} // namespace
