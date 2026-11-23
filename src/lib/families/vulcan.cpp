// Vulcan 100 AIMO, Vulcan 120/121/122 AIMO, Vulcan TKL AIMO and Vulcan 80. the Ryos generation of the protocol (select on 04, key
// reports 06 07 0a 0b, lights in 0d) but the keyboard keeps ONE set of everything, Swarm had the five profiles on the pc and wrote
// whichever was active. the TKL has a longer 07 (more keys with an Fn side), the Vulcan 80 a smaller 0d: one colour (blue), a
// brightness byte per key. docs/protocol/vulcan.md
// nobody here has one. everything is out of the Swarm modules, ROCCAT_Swarm_Monitor.exe and the keyboard firmware, see the doc

#include "../internal.h"
#include "../keyboards.h"
#include "../sim_vulcan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

#include "vulcan_keys.inc"
#include "vulcan_tables.inc"
#include "vulcan_80_keys.inc"
#include "vulcan_80_tables.inc"
#include "vulcan_tkl_keys.inc"
#include "vulcan_tkl_tables.inc"
#include "horde_keys.inc"
#include "horde_tables.inc"

const keyboard_layout VULCAN_AIMO_KEYBOARD = {VULCAN_AIMO_REGIONS, static_cast<int>(sizeof VULCAN_AIMO_REGIONS / sizeof VULCAN_AIMO_REGIONS[0]), 680, 370};
const keyboard_layout VULCAN_80_KEYBOARD = {VULCAN_80_REGIONS, static_cast<int>(sizeof VULCAN_80_REGIONS / sizeof VULCAN_80_REGIONS[0]), 680, 370};
const keyboard_layout HORDE_KEYBOARD = {HORDE_REGIONS, static_cast<int>(sizeof HORDE_REGIONS / sizeof HORDE_REGIONS[0]), 680, 370};
const keyboard_layout VULCAN_TKL_KEYBOARD = {VULCAN_TKL_REGIONS, static_cast<int>(sizeof VULCAN_TKL_REGIONS / sizeof VULCAN_TKL_REGIONS[0]), 680, 370};

// key numbers are Swarms (kb_matrix.xml index of the module). AIMO: 1 to 113, FX and the volume wheel (112, 113) have no byte anywhere
// we understand. Vulcan 80: 1 to 115, its Mute, Volume down, Volume up and light key (112 to 115) are plain keys in 06.
// TKL: 1 to 123, the numbering of the newer module (the Vulcan Pro one, Caps Lock is 67 there and Fn 116)
constexpr int INPUTS = 114, LEDS = 112, INPUTS_80 = 116, INPUTS_TKL = 124, INPUTS_HORDE = 260;

// which keys have three bytes where. F1 to Pause are key 2 to 16 on the big ones, normal in the first half of 07, with Fn in the
// second half. the 20 Easy-Shift keys are the ones out of the modules key_data_mapping.txt (1 to 5, Q to T, A to G, Z to B on a US board)
constexpr int EASY_KEYS = 20;
constexpr uint8_t EASY_KEY[EASY_KEYS] = {19, 20, 21, 22, 23, 41, 42, 43, 44, 45, 63, 64, 65, 66, 67, 82, 83, 84, 85, 86};
constexpr uint8_t FKEY[15] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

// what differs between the boards
struct model {
    uint16_t pid;
    bool mono;          // the Vulcan 80 light report: 156 bytes, a brightness per key, no colours
    int inputs;         // key numbers 0 .. inputs - 1
    int silent_from;    // keys from here on exist but have no byte we know of (FX and the volume wheel of the AIMO boards)
    const int16_t *slot; // key number -> byte in 06 after the header
    const int16_t *led; // key number -> led slot (colour boards)
    int leds;
    const uint8_t *fkeys; // the keys of 07 in order, the second half of 07 is the same keys with Fn held
    int fkey_count;
    const uint8_t *easy; // the 20 keys of 0b
    int caps, fn;        // the two keys of 0a
    const uint8_t *factory_06, *factory_07, *factory_0a, *factory_0b, *factory_0d;
    // what only some boards have. 0 / nullptr = not there
    uint16_t keys_size = 133;       // 06: 133 (128 slots), the Horde 149 (144)
    uint16_t light_size = 443;      // 0d
    bool light_known = true;        // false: 0d is read and written back as it is, nothing in it is offered
    bool has_extend = true;         // 09
    const uint8_t *mkeys = nullptr; // the Horde M keys in 08: [08 23 profile] + 5 keys x 3, the same 5 with Easy-Shift, checksum
    const uint8_t *factory_08 = nullptr;
};
const model MODELS[] = {
    {0x307a, false, INPUTS, 112, KEY_SLOT, KEY_LED, LEDS, FKEY, 15, EASY_KEY, 62, 104, FACTORY_06, FACTORY_07, FACTORY_0A, FACTORY_0B, FACTORY_0D},
    {0x3098, false, INPUTS, 112, KEY_SLOT, KEY_LED, LEDS, FKEY, 15, EASY_KEY, 62, 104, FACTORY_06, FACTORY_07, FACTORY_0A, FACTORY_0B, FACTORY_0D},
    {0x2fee, false, INPUTS_TKL, INPUTS_TKL, KEY_SLOT_TKL, KEY_LED_TKL, INPUTS_TKL, FKEY_TKL, 23, EASY_TKL, 67, 116, FACTORY_TKL_06,
     FACTORY_TKL_07, FACTORY_TKL_0A, FACTORY_TKL_0B, FACTORY_TKL_0D},
    {0x305c, true, INPUTS_80, INPUTS_80, KEY_SLOT_80, nullptr, INPUTS_80, FKEY, 15, EASY_KEY, 62, 104, FACTORY_80_06, FACTORY_80_07,
     FACTORY_80_0A, FACTORY_80_0B, FACTORY_80_0D},
    // the Horde and Horde AIMO: key number = slot, Caps Lock 258, the dial 259 (no byte we know of). light: 58 bytes, not understood
    {0x300c, false, INPUTS_HORDE, 259, HORDE_SLOT, nullptr, 0, HORDE_FKEY, 15, HORDE_EASY, 258, 69, HORDE_06, HORDE_07, HORDE_0A, HORDE_0B,
     HORDE_0D, 149, 58, false, false, HORDE_MKEY, HORDE_08},
    {0x303e, false, INPUTS_HORDE, 259, HORDE_SLOT, nullptr, 0, HORDE_FKEY, 15, HORDE_EASY, 258, 69, HORDE_06, HORDE_07, HORDE_0A, HORDE_0B,
     HORDE_AIMO_0D, 149, 58, false, false, HORDE_MKEY, HORDE_AIMO_08},
};
const model &model_of(uint16_t pid)
{
    for (const model &m : MODELS)
        if (m.pid == pid)
            return m;
    return MODELS[0];
}

// the parts of a profile: report id, size, what goes into the select. sizes are what the firmware (VULCAN_AIMO_v1.36) checks,
// the Vulcan 80 firmware (V1.19) checks the same ones, only its 0d is LIGHT_80 bytes. the TKL has 07 with 23 keys instead of 15,
// size() below. here the sizes are the room we keep
// except 0b: the firmware takes 65 bytes ([0b 41 profile] + 20 keys), Swarm keeps a 294 byte one ([0b 26 01 profile] + 96 keys,
// the Ryos layout) in its default profile. maybe a newer firmware, maybe Swarm being Swarm. we keep room for the big one and
// use whichever the keyboard answers with
struct part {
    uint8_t id, request;
    uint16_t size;
};
// 08 (the Horde M keys) comes last so a backup of the other boards keeps its layout
enum { P_KEYS, P_FKEYS, P_EXTEND, P_CAPSFN, P_EASY, P_LIGHT, P_MKEYS, PARTS };
constexpr part PART[PARTS] = {{0x06, 0xa0, 149}, {0x07, 0xa1, 143}, {0x09, 0xa3, 43}, {0x0a, 0xa4, 8},
                              {0x0b, 0xa5, 294}, {0x0d, 0xb1, 443}, {0x08, 0xa2, 35}};
constexpr int MKEYS = 5;
constexpr size_t EASY_SHORT = 65;
constexpr uint16_t LIGHT_80 = 156;

// the real size of a part on a board
size_t size(const model &m, int p)
{
    switch (p) {
    case P_KEYS:   return m.keys_size;
    case P_FKEYS:  return 3 + 6 * static_cast<size_t>(m.fkey_count) + 2;
    case P_EXTEND: return m.has_extend ? PART[p].size : 0;
    case P_LIGHT:  return m.mono ? LIGHT_80 : m.light_size;
    case P_MKEYS:  return m.mkeys ? PART[p].size : 0;
    default:       return PART[p].size;
    }
}

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

// Vulcan 80 0d: [0d 9c profile ? effect speed 05 ...] then brightness at 11 (a step 0 to 9, the keyboard stores what its own keys set it
// to there too), then from 14 one byte per key: 14 + the keys slot in 06. effects are the ones the firmware draws: 0 off, 1 lit (the
// bytes per key), 7 and 8, 0x11 keys light up when pressed and fade out. Swarm calls 7 and 8 breathing and heartbeat, what the
// firmware draws for them are sweeps across the board (a band running to and fro, the board filling up and emptying). anything
// else turns into 7 on the keyboard
enum : size_t { L80_BRIGHTNESS = 11, L80_KEYS = 14 };
constexpr uint8_t BRIGHTNESS_80[10] = {0x00, 0x0f, 0x1e, 0x32, 0x46, 0x64, 0x82, 0xa0, 0xc8, 0xff}; // firmware table at 0xa682
constexpr int EFFECTS_80[][2] = {{ROCKCAT_EFFECT_OFF, 0},       {ROCKCAT_EFFECT_FULLY_LIT, 1}, {ROCKCAT_EFFECT_BREATHING, 7},
                                 {ROCKCAT_EFFECT_HEARTBEAT, 8}, {ROCKCAT_EFFECT_FADE, 0x11}};

// Swarms table for the brightness byte (index 1 to 69 -> pwm), out of ROCCAT_Swarm_Monitor.exe at 0x827780
constexpr uint8_t BRIGHTNESS[70] = {0,   0,   48,  51,  54,  57,  60,  63,  66,  69,  72,  75,  78,  81,  84,  87,  90,  93,
                                    96,  99,  102, 105, 108, 111, 114, 117, 120, 123, 126, 129, 132, 135, 138, 141, 144, 147,
                                    150, 153, 156, 159, 162, 165, 168, 171, 174, 177, 180, 183, 186, 189, 192, 195, 198, 201,
                                    204, 207, 210, 213, 216, 219, 222, 225, 228, 231, 234, 237, 240, 245, 250, 255};

int index_in(const uint8_t *list, int n, int key)
{
    for (int i = 0; i < n; ++i)
        if (list[i] == key)
            return i;
    return -1;
}

bool is_key(const model &m, int key)
{
    return key > 0 && key < m.inputs &&
           (m.slot[key] >= 0 || (m.mkeys && index_in(m.mkeys, MKEYS, key) >= 0) || key == m.caps || key == m.fn || key >= m.silent_from || index_in(m.fkeys, m.fkey_count, key) >= 0);
}

// where the byte(s) of a key on a layer are: part, offset, three bytes or one. part < 0 = nothing there
struct spot {
    int part = -1;
    size_t at = 0;
    bool wide = false;
};

spot find(const model &m, int layer, int key)
{
    spot s;
    const int f = index_in(m.fkeys, m.fkey_count, key);
    const int mk = m.mkeys ? index_in(m.mkeys, MKEYS, key) : -1;
    if (mk >= 0 && layer <= 1)
        return spot{P_MKEYS, 3 + 3 * static_cast<size_t>(layer * MKEYS + mk), true};
    if (layer == 0) {
        if (f >= 0)
            s = spot{P_FKEYS, 3 + 3 * static_cast<size_t>(f), true};
        else if (key == m.caps)
            s = spot{P_CAPSFN, 3, false};
        else if (key == m.fn)
            s = spot{P_CAPSFN, 4, false};
        else if (m.slot[key] >= 0)
            s = spot{P_KEYS, 3 + static_cast<size_t>(m.slot[key]), false};
    } else if (layer == 1) {
        int e = index_in(m.easy, EASY_KEYS, key);
        if (e >= 0)
            s = spot{P_EASY, 3 * static_cast<size_t>(e), true}; // from the first key, the header length depends on the report
    } else if (layer == 2) {
        if (f >= 0)
            s = spot{P_FKEYS, 3 + 3 * static_cast<size_t>(m.fkey_count + f), true};
    }
    return s;
}

const uint8_t *factory(const model &m, int p)
{
    switch (p) {
    case P_KEYS:   return m.factory_06;
    case P_FKEYS:  return m.factory_07;
    case P_CAPSFN: return m.factory_0a;
    case P_EASY:   return m.factory_0b;
    case P_MKEYS:  return m.factory_08;
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
    const model &board() const { return model_of(desc->pid); }
    bool mono() const { return board().mono; }
    rockcat_profile *clone() const override { return new vulcan_profile(*this); }
    uint8_t *at(int p) { return data + offset_of(p); }
    const uint8_t *at(int p) const { return data + offset_of(p); }
    size_t size_of(int p) const { return size(board(), p); }
    size_t total() const
    {
        size_t n = 0;
        for (int p = 0; p < PARTS; ++p)
            n += size_of(p);
        return n;
    }
    size_t length(int p) const { return p == P_EASY ? easy_length(at(p)) : size_of(p); }
    size_t where(const spot &s) const { return s.part == P_EASY ? easy_head(at(P_EASY)) + s.at : s.at; }

    int read(rockcat_device *dev) override
    {
        for (int p = 0; p < PARTS; ++p) {
            dirty[p] = false;
            if (!size_of(p))
                continue;
            int r = select_data(dev, 0, PART[p].request);
            if (r != ROCKCAT_OK)
                return r;
            uint8_t *b = at(p);
            std::memset(b, 0, PART[p].size);
            b[0] = PART[p].id;
            r = get_raw(dev, b, size_of(p));
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
            if (!dirty[p] || !size_of(p))
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

    // the parts back to back, each as long as it is on this board (on the 100 / 120 AIMO that is the room we keep, so a backup
    // from before the TKL came along still fits)
    void export_payload(std::vector<uint8_t> &out) const override
    {
        for (int p = 0; p < PARTS; ++p)
            out.insert(out.end(), at(p), at(p) + size_of(p));
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != total())
            return ROCKCAT_ERR_ARGUMENT;
        size_t o = 0;
        for (int p = 0; p < PARTS; o += size_of(p), ++p)
            if (size_of(p) && (buf[o] != PART[p].id || (p == P_EASY && easy_length(buf + o) == 0)))
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
        const uint8_t *l = at(P_LIGHT);
        if (!board().light_known)
            return ROCKCAT_ERR_UNSUPPORTED;
        if (mono())
            return get_int_80(l, which, value);
        switch (which) {
        case ROCKCAT_SET_EFFECT:       *value = effect_from_device(l[L_EFFECT]); break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = l[L_SPEED]; break;
        case ROCKCAT_SET_BRIGHTNESS:   *value = BRIGHTNESS[l[L_BRIGHTNESS] < 70 ? l[L_BRIGHTNESS] : 69]; break;
        case ROCKCAT_SET_COLOR_THEME:  *value = l[L_THEME] & 0x7f; break;
        default:                       return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    static int get_int_80(const uint8_t *l, rockcat_setting which, int *value)
    {
        switch (which) {
        case ROCKCAT_SET_EFFECT:
            *value = ROCKCAT_EFFECT_BREATHING; // what the firmware makes of a byte it doesnt know
            for (const auto &e : EFFECTS_80)
                if (e[1] == l[L_EFFECT])
                    *value = e[0];
            break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = l[L_SPEED]; break;
        case ROCKCAT_SET_BRIGHTNESS:   *value = BRIGHTNESS_80[l[L80_BRIGHTNESS] < 10 ? l[L80_BRIGHTNESS] : 9]; break;
        default:                       return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int_80(uint8_t *l, rockcat_setting which, int value)
    {
        switch (which) {
        case ROCKCAT_SET_EFFECT: {
            int byte = -1;
            for (const auto &e : EFFECTS_80)
                if (e[0] == value)
                    byte = e[1];
            if (byte < 0)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_EFFECT] = static_cast<uint8_t>(byte);
            break;
        }
        case ROCKCAT_SET_EFFECT_SPEED:
            // the firmware takes 0 to 11 (bigger is faster), the AIMO one drops 0, so we stay with 1 to 11 on both
            if (value < caps().effect_speed_min || value > caps().effect_speed_max)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_SPEED] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_BRIGHTNESS: {
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            int best = 0;
            for (int i = 1; i < 10; ++i)
                if (std::abs(BRIGHTNESS_80[i] - value) < std::abs(BRIGHTNESS_80[best] - value))
                    best = i;
            l[L80_BRIGHTNESS] = static_cast<uint8_t>(best);
            break;
        }
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        uint8_t *l = at(P_LIGHT);
        if (!board().light_known)
            return ROCKCAT_ERR_UNSUPPORTED;
        if (mono())
            return set_int_80(l, which, value);
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

    // Vulcan 80: led number = key number, a brightness byte per key. shown as the boards blue with that alpha
    int light_80_at(int led) const
    {
        const model &m = board();
        return led > 0 && led < m.inputs && m.slot[led] >= 0 ? static_cast<int>(L80_KEYS) + m.slot[led] : -1;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (mono()) {
            int at80 = light_80_at(led);
            if (at80 < 0)
                return ROCKCAT_ERR_ARGUMENT;
            uint8_t v = at(P_LIGHT)[at80];
            if (c)
                *c = rockcat_color{v, ONE_COLOUR_ON.r, ONE_COLOUR_ON.g, ONE_COLOUR_ON.b};
            if (enabled)
                *enabled = v != 0;
            return ROCKCAT_OK;
        }
        const model &m = board();
        if (led < 0 || led >= m.leds || m.led[led] < 0)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *p = at(P_LIGHT) + colour_at(m.led[led]);
        if (c)
            *c = rockcat_color{255, p[0], p[12], p[24]};
        if (enabled)
            *enabled = 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (mono()) {
            // the colour cant be had, how bright it is can: the brightest channel times alpha
            int at80 = light_80_at(led);
            if (at80 < 0)
                return ROCKCAT_ERR_ARGUMENT;
            int level = enabled ? std::max({c.r, c.g, c.b}) * c.a / 255 : 0;
            at(P_LIGHT)[at80] = static_cast<uint8_t>(level);
            dirty[P_LIGHT] = true;
            return ROCKCAT_OK;
        }
        const model &m = board();
        if (led < 0 || led >= m.leds || m.led[led] < 0)
            return ROCKCAT_ERR_ARGUMENT;
        // there is no "this key is off", a dark key is black. alpha goes into the colour, the brightness is for the whole board
        int a = enabled ? c.a : 0;
        uint8_t *p = at(P_LIGHT) + colour_at(m.led[led]);
        p[0] = static_cast<uint8_t>((c.r * a + 127) / 255);
        p[12] = static_cast<uint8_t>((c.g * a + 127) / 255);
        p[24] = static_cast<uint8_t>((c.b * a + 127) / 255);
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || !is_key(board(), input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = at(s.part) + where(s);
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || !is_key(board(), input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
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
        if (layer < 0 || layer >= caps().layer_count || !is_key(board(), input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
        if (s.part < 0)
            return 0;
        return s.wide ? ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS : ROCKCAT_ROOM_KEY;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || !is_key(board(), input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(board(), layer, input);
        const uint8_t *t = s.part >= 0 ? factory(board(), s.part) : nullptr;
        if (!t)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *p = t + (s.part == P_EASY ? 3 : 0) + s.at;
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        // everything after the three byte header, the profile byte stays whatever the keyboard said
        for (int p : {P_KEYS, P_FKEYS, P_CAPSFN, P_MKEYS}) {
            if (!size_of(p))
                continue;
            std::memcpy(at(p) + 3, factory(board(), p) + 3, size_of(p) - 5);
            dirty[p] = true;
        }
        // the factory table has the 20 keys, a long 0b gets those and empty entries after them
        size_t n = length(P_EASY), head = easy_head(at(P_EASY));
        std::memset(at(P_EASY) + head, 0, n - head - 2);
        std::memcpy(at(P_EASY) + head, board().factory_0b + 3, 3 * EASY_KEYS);
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

// the Vulcan 80: one colour, so no HAS_RGB (the gui and engine treat it like the blue Ryos boards). its own five effects, none drawn
// by the pc. 116 key numbers, led number = key number
#define VULCAN_80_CAPS                                                                                                              \
    {ROCKCAT_KIND_KEYBOARD, "vulcan", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_EASYSHIFT, 1, INPUTS_80, 3, INPUTS_80, 0, 0, 0, 0, {0},     \
     bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_HEARTBEAT) |        \
         bit(ROCKCAT_EFFECT_FADE),                                                                                                   \
     0, 1, 11, -1}

// the TKL: the AIMO caps with its own key numbers (1 to 123, led number = key number)
#define VULCAN_TKL_CAPS                                                                                                             \
    {ROCKCAT_KIND_KEYBOARD, "vulcan", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB, 1, INPUTS_TKL, 3, INPUTS_TKL, 0, \
     0, 0, 0, {0},                                                                                                                  \
     bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_HEARTBEAT),   \
     0, 1, 11, -1}

// the Horde boards: keys on three layers (the M keys on two), the light is kept but not offered (58 bytes nobody untangled yet)
#define HORDE_CAPS                                                                                                                   \
    {ROCKCAT_KIND_KEYBOARD, "vulcan", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_EASYSHIFT, 1, INPUTS_HORDE, 3, 0, 0, 0, 0, 0, {0}, 0, 0, 0, 0, -1}

const device_desc DEVICES[] = {
    {0x307a, "ROCCAT Vulcan 100 AIMO", &VULCAN_FAMILY, nullptr, 0x0d, VULCAN_CAPS, &VULCAN_AIMO_KEYBOARD},
    {0x3098, "ROCCAT Vulcan 120 AIMO", &VULCAN_FAMILY, nullptr, 0x0d, VULCAN_CAPS, &VULCAN_AIMO_KEYBOARD},
    {0x305c, "ROCCAT Vulcan 80", &VULCAN_FAMILY, nullptr, 0x0d, VULCAN_80_CAPS, &VULCAN_80_KEYBOARD},
    {0x300c, "ROCCAT Horde", &VULCAN_FAMILY, nullptr, 0x0d, HORDE_CAPS, &HORDE_KEYBOARD},
    {0x303e, "ROCCAT Horde AIMO", &VULCAN_FAMILY, nullptr, 0x0d, HORDE_CAPS, &HORDE_KEYBOARD},
    {0x2fee, "ROCCAT Vulcan TKL AIMO", &VULCAN_FAMILY, nullptr, 0x0d, VULCAN_TKL_CAPS, &VULCAN_TKL_KEYBOARD},
};

const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// the pretend ones, one per board and shared by every handle in the process like the Ryos one, so the gui can write and read back
vulcan_sim &shared(uint16_t pid)
{
    static vulcan_sim aimo(FACTORY_06, FACTORY_07, FACTORY_0A, FACTORY_0B, FACTORY_0D);
    static vulcan_sim v80(FACTORY_80_06, FACTORY_80_07, FACTORY_80_0A, FACTORY_80_0B, FACTORY_80_0D, LIGHT_80);
    static vulcan_sim tkl(FACTORY_TKL_06, FACTORY_TKL_07, FACTORY_TKL_0A, FACTORY_TKL_0B, FACTORY_TKL_0D, 443, 143);
    static vulcan_sim horde(HORDE_06, HORDE_07, HORDE_0A, HORDE_0B, HORDE_0D, 58, 95, 149, HORDE_08);
    static vulcan_sim horde_aimo(HORDE_06, HORDE_07, HORDE_0A, HORDE_0B, HORDE_AIMO_0D, 58, 95, 149, HORDE_AIMO_08);
    if (pid == 0x300c)
        return horde;
    if (pid == 0x303e)
        return horde_aimo;
    return pid == 0x305c ? v80 : pid == 0x2fee ? tkl : aimo;
}

template <uint16_t PID> struct door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared(PID).lock);
        return shared(PID).get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared(PID).lock);
        return shared(PID).set_feature(buf, len);
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

const sim_registration sim({"vulcan-120-aimo", 0x3098, "ROCCAT Vulcan 120 AIMO", 1, [] { return std::unique_ptr<transport>(new door<0x3098>); }});
const sim_registration sim_tkl({"vulcan-tkl-aimo", 0x2fee, "ROCCAT Vulcan TKL AIMO", 1, [] { return std::unique_ptr<transport>(new door<0x2fee>); }});
const sim_registration sim_horde({"horde", 0x300c, "ROCCAT Horde", 1, [] { return std::unique_ptr<transport>(new door<0x300c>); }});
const sim_registration sim_horde_aimo({"horde-aimo", 0x303e, "ROCCAT Horde AIMO", 1,
                                       [] { return std::unique_ptr<transport>(new door<0x303e>); }});
const sim_registration sim_80({"vulcan-80", 0x305c, "ROCCAT Vulcan 80", 1, [] { return std::unique_ptr<transport>(new door<0x305c>); }});

} // namespace
