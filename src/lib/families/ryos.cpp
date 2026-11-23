// Ryos MK FX. the same conversation as the mice (select on 04, wait for 04 to say ready) with a lot more reports behind it:
// six for what the keys do, one for the light settings, one for the colour of every key. docs/protocol/ryos.md has the layouts
// structs and names come from roccat-tools (ryosmk/libroccatryosmk and ryosmkfx/libroccatryosmkfx), checked against a real
// Ryos MK FX with firmware 1.31. written as "ryos" and not "ryos mk fx" because MK, Glow, Pro and TKL sit on the same reports,
// what differs for them is the lighting

#include "../internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

#include "ryos_tables.inc"

constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_MACRO = 0x0e, REPORT_INFO = 0x0f, REPORT_LIGHT_CONTROL = 0x13, REPORT_CUSTOM_LIGHTS = 0x18;
// inputs are numbered the way Swarm numbers the keys in its key matrix, which is also the place of the key in the primary
// report and of its led in a lighting frame. 0 to 119 are the matrix (a few slots have no key), 121 to 123 the thumb keys
constexpr int INPUTS = 124, LEDS = 120, PROFILES = 5;

// the reports that make up a profile, in the order they get read. request = what goes into the select in front of a read
struct part {
    uint8_t id, request;
    uint16_t size;
    uint8_t profile_at;
};
enum { P_PRIMARY, P_FUNCTION, P_MACROKEYS, P_THUMB, P_EXTRA, P_EASYZONE, P_KEYMASK, P_LIGHT, P_STORED, PARTS };
constexpr part PART[PARTS] = {
    {0x06, 0xa0, 125, 2}, // one byte per key: what it is when nothing is held
    {0x07, 0xa1, 95, 2},  // F1 to F12, Print, Scroll, Pause: 15 keys, 3 bytes each. first what they are, then what they are with
                          // Fn held (out of the box the one printed on the key: mute, volume, calculator...)
    {0x08, 0xa2, 35, 2},  // M1 to M5, same idea
    {0x09, 0xa3, 23, 2},  // T1 to T3
    {0x0a, 0xa4, 8, 2},   // Caps Lock and Fn
    {0x0b, 0xa5, 294, 3}, // 96 keys with Easy-Shift held, 3 bytes each. the only one with a two byte size
    {0x0c, 0xb0, 6, 2},   // which keys the game mode switches off, passed through untouched
    {0x0d, 0xb1, 32, 2},  // light settings
    {0x17, 0xb3, 239, 2}, // colour of every key
};

constexpr size_t offset_of(int p)
{
    size_t at = 0;
    for (int i = 0; i < p; ++i)
        at += PART[i].size;
    return at;
}
constexpr size_t TOTAL = offset_of(PARTS);

// light settings report
enum : size_t {
    L_BRIGHTNESS = 3, // 0 to 5
    L_IDLE_BRIGHTNESS = 4,
    L_IDLE_MINUTES = 5,
    L_MODE = 6, // 0 = every key the colour below, 1 = the colours of report 17
    L_EFFECT = 7,
    L_SPEED = 9, // 1 to 3
    L_FEEDBACK = 11,
    L_IDLE_STYLE = 12,
    L_RED = 14, // 16 bit each, see COLOUR_TO_HARDWARE
};
constexpr int BRIGHTNESS_STEPS = 5;

// the colours of all keys as report 17 (stored) and report 18 (from the pc) carry them. no rgb per key in there: per channel
// there is a palette of 7 values and every key says which of the 7 it takes. so a frame with more than 7 different reds
// gets rounded to the 7 that fit best
enum : size_t {
    Y_STATE = 0,     // 15 bytes, bit per key: lit or not
    Y_NUMBER = 15,   // 3 x 60 bytes, a nibble per key: 0 = nothing of this channel, 1 to 7 = palette entry
    Y_PWM = 195,     // 3 x 7 bytes
    Y_STEP = 216,    // 3 x 4 bytes, a nibble per palette entry
    LAYER_SIZE = 228,
};
constexpr int PALETTE = 7;

struct lit_key {
    uint8_t r, g, b;
    bool on;
};

int level_of(uint16_t hardware)
{
    return (hardware & 0xff) * ((hardware >> 8) + 1);
}

// the way back: the colour value whose brightness is the nearest one below
uint8_t hardware_to_colour(uint16_t hardware)
{
    int level = level_of(hardware), v = 0;
    while (v < 255 && level_of(COLOUR_TO_HARDWARE[v + 1]) <= level)
        ++v;
    return static_cast<uint8_t>(v);
}

uint8_t nibble(const uint8_t *field, int i)
{
    return i & 1 ? field[i >> 1] >> 4 : field[i >> 1] & 0x0f;
}

void set_nibble(uint8_t *field, int i, uint8_t v)
{
    uint8_t &b = field[i >> 1];
    b = static_cast<uint8_t>(i & 1 ? (b & 0x0f) | (v << 4) : (b & 0xf0) | (v & 0x0f));
}

void layer_decode(const uint8_t *layer, lit_key *keys)
{
    for (int k = 0; k < LEDS; ++k) {
        uint8_t rgb[3];
        for (int ch = 0; ch < 3; ++ch) {
            int n = nibble(layer + Y_NUMBER + 60 * ch, k);
            rgb[ch] = n < 1 || n > PALETTE ? 0 : hardware_to_colour(static_cast<uint16_t>(layer[Y_PWM + PALETTE * ch + n - 1] | (nibble(layer + Y_STEP + 4 * ch, n - 1) << 8)));
        }
        keys[k] = lit_key{rgb[0], rgb[1], rgb[2], ((layer[Y_STATE + (k >> 3)] >> (k & 7)) & 1) != 0};
    }
}

// picks the 7 values of one channel and says which key takes which. as long as the lit keys use 7 values or less thats exact
// (black counts as one of them, and there always is black: the slots without a key),
// beyond that its plain k-means on a line: start spread over the values there are, then move every palette entry
// to the middle of the keys that sit nearest to it until nothing moves anymore
void pick_palette(const uint8_t *value, const lit_key *keys, uint8_t *palette, uint8_t *number)
{
    bool seen[256] = {};
    for (int k = 0; k < LEDS; ++k)
        if (keys[k].on)
            seen[value[k]] = true;
    int unique[256], n = 0;
    for (int v = 0; v < 256; ++v)
        if (seen[v])
            unique[n++] = v;
    int mean[PALETTE] = {};
    for (int i = 0; i < PALETTE; ++i)
        mean[i] = n == 0 ? 0 : n <= PALETTE ? unique[std::min(i, n - 1)] : unique[i * (n - 1) / (PALETTE - 1)];
    int used = std::max(1, std::min(n, PALETTE));
    const bool pinned = n > 0 && unique[0] == 0;
    for (int round = 0; round < 64; ++round) {
        bool moved = false;
        long sum[PALETTE] = {};
        int count[PALETTE] = {};
        for (int k = 0; k < LEDS; ++k) {
            if (!keys[k].on) {
                number[k] = 0;
                continue;
            }
            int best = 0;
            for (int i = 1; i < used; ++i)
                if (std::abs(value[k] - mean[i]) < std::abs(value[k] - mean[best]))
                    best = i;
            if (number[k] != best + 1) {
                number[k] = static_cast<uint8_t>(best + 1);
                moved = true;
            }
            sum[best] += value[k];
            ++count[best];
        }
        if (!moved)
            break;
        // black stays black. the first entry starts on the lowest value there is, and when that is 0 it doesnt get to wander
        // off: the slots without a key have to be dark exactly, a bit of glow on them lights up other keys
        for (int i = pinned ? 1 : 0; i < used; ++i)
            if (count[i])
                mean[i] = static_cast<int>((sum[i] + count[i] / 2) / count[i]);
    }
    for (int i = 0; i < PALETTE; ++i)
        palette[i] = static_cast<uint8_t>(mean[i]);
}

// true for the slots of the light matrix that have a key on them (on the boards we have key tables for). 9 of the 120 dont
bool has_key(int led);

void layer_encode(const lit_key *given, uint8_t *layer)
{
    std::memset(layer, 0, LAYER_SIZE);
    // the slots without a key must stay dark, always: on a real keyboard lighting them made half a dozen OTHER keys
    // light up bright and more of them glow dimly (the led matrix is wired through them somehow). dark means black and
    // not "off", every key keeps its state bit
    lit_key keys[LEDS];
    for (int k = 0; k < LEDS; ++k)
        keys[k] = has_key(k) ? given[k] : lit_key{0, 0, 0, true};
    for (int ch = 0; ch < 3; ++ch) {
        uint8_t value[LEDS], number[LEDS], palette[PALETTE];
        for (int k = 0; k < LEDS; ++k) {
            value[k] = ch == 0 ? keys[k].r : ch == 1 ? keys[k].g : keys[k].b;
            number[k] = 0xff;
        }
        pick_palette(value, keys, palette, number);
        for (int i = 0; i < PALETTE; ++i) {
            uint16_t hw = COLOUR_TO_HARDWARE[palette[i]];
            layer[Y_PWM + PALETTE * ch + i] = hw & 0xff;
            set_nibble(layer + Y_STEP + 4 * ch, i, static_cast<uint8_t>(hw >> 8));
        }
        for (int k = 0; k < LEDS; ++k)
            set_nibble(layer + Y_NUMBER + 60 * ch, k, number[k]);
    }
    for (int k = 0; k < LEDS; ++k)
        if (keys[k].on)
            layer[Y_STATE + (k >> 3)] = static_cast<uint8_t>(layer[Y_STATE + (k >> 3)] | (1 << (k & 7)));
}

// rockcat_effect <-> the effect byte of the keyboard. 0 to 4 are the same numbers, after that the keyboard counts differently
constexpr int EFFECTS[][2] = {
    {ROCKCAT_EFFECT_OFF, 0},       {ROCKCAT_EFFECT_FULLY_LIT, 1}, {ROCKCAT_EFFECT_BLINKING, 2}, {ROCKCAT_EFFECT_BREATHING, 3},
    {ROCKCAT_EFFECT_HEARTBEAT, 4}, {ROCKCAT_EFFECT_EQUALIZER, 5}, {ROCKCAT_EFFECT_RIPPLE, 6},   {ROCKCAT_EFFECT_WAVE, 7},
    {ROCKCAT_EFFECT_HEATMAP, 8},   {ROCKCAT_EFFECT_GAME_PRESET, 9}, {ROCKCAT_EFFECT_FADE, 10},
};

// which report a key lives in. the matrix slots nobody listed have no key under them
enum key_kind : uint8_t { K_NONE, K_PLAIN, K_ZONE, K_FUNCTION, K_MACRO, K_THUMB, K_CAPS, K_FN };
struct key_place {
    uint8_t kind = K_NONE, at = 0;
};
constexpr int FUNCTION_KEYS = 15, MACRO_KEYS = 5, THUMB_KEYS = 3, ZONE_KEYS = 96;
// where key number n of the Easy-Shift report (0b) sits in the matrix. out of roccat-tools (ryos_key_relations.c), ff = no key
constexpr uint8_t ZONE_INPUT[ZONE_KEYS] = {
    12,  13,  25,  26,  27,  39,  28,  40,  29,  41,  18,  30,  19,   32,  44,   36,  37,  49,  38,  50,  51,  52,  64, 53,
    42,  54,  31,  43,  56,  61,  73,  62,  74,  63,  75,  76,  65,   66,  78,   55,  67,  68,  84,  85,  97,  86,  98, 87,
    99,  88,  77,  89,  90,  79,  91,  80,  108, 109, 110, 100, 0xff, 112, 0xff, 101, 113, 114, 103, 92,  45,  57,  69, 81,
    93,  105, 117, 104, 116, 118, 10,  22,  11,  23,  34,  46,  35,   47,  58,   70,  59,  0xff, 82, 94,  83,  95,  106, 107,
};
constexpr uint8_t FUNCTION_INPUT[FUNCTION_KEYS] = {2, 14, 3, 15, 4, 16, 5, 17, 6, 7, 8, 20, 9, 21, 33};
constexpr uint8_t MACRO_INPUT[MACRO_KEYS] = {0, 24, 48, 72, 96};
constexpr uint8_t THUMB_INPUT[THUMB_KEYS] = {121, 122, 123};
constexpr int ESC_INPUT = 1, CAPS_INPUT = 60, FN_INPUT = 115;

const key_place &place(int input)
{
    static const struct table {
        key_place key[INPUTS];
        table()
        {
            for (int i = 0; i < ZONE_KEYS; ++i)
                if (ZONE_INPUT[i] != 0xff)
                    key[ZONE_INPUT[i]] = key_place{K_ZONE, static_cast<uint8_t>(i)};
            for (int i = 0; i < FUNCTION_KEYS; ++i)
                key[FUNCTION_INPUT[i]] = key_place{K_FUNCTION, static_cast<uint8_t>(i)};
            for (int i = 0; i < MACRO_KEYS; ++i)
                key[MACRO_INPUT[i]] = key_place{K_MACRO, static_cast<uint8_t>(i)};
            for (int i = 0; i < THUMB_KEYS; ++i)
                key[THUMB_INPUT[i]] = key_place{K_THUMB, static_cast<uint8_t>(i)};
            // Esc is the one key that cant do anything else with Easy-Shift held
            key[ESC_INPUT] = key_place{K_PLAIN, 0};
            key[CAPS_INPUT] = key_place{K_CAPS, 0};
            key[FN_INPUT] = key_place{K_FN, 0};
        }
    } T;
    static const key_place none;
    return input >= 0 && input < INPUTS ? T.key[input] : none;
}

// the nine slots no key of an ISO or ANSI board sits on. five of them are keys on japanese / korean / brazilian boards
// (roccat-tools knows those), the other four are nothing at all. tried on a real ISO keyboard: lighting slot 71 made
// M3, M5, right Ctrl, Y, C and B light up bright, the others made more keys glow dimly
bool has_key(int led)
{
    for (int dark : {32, 71, 91, 100, 101, 102, 111, 113, 119})
        if (led == dark)
            return false;
    return led >= 0 && led < LEDS;
}

struct ryos_profile final : rockcat_profile {
    uint8_t raw[TOTAL] = {};
    bool dirty[PARTS] = {};
    // the colours as the api hands them out. filled from the reports after a read, written back into them before a write
    lit_key keys[LEDS] = {};
    bool keys_changed = false;
    std::vector<rockcat_macro> macros; // parked until the next write

    using rockcat_profile::rockcat_profile;

    uint8_t *at(int p) { return raw + offset_of(p); }
    const uint8_t *at(int p) const { return raw + offset_of(p); }
    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new ryos_profile(*this); }

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

    // report id and size where they belong
    static bool shaped(const uint8_t *b, int p)
    {
        return b[0] == PART[p].id && b[1] == (PART[p].size & 0xff) && (PART[p].profile_at == 2 || b[2] == PART[p].size >> 8);
    }

    void lights_from_reports()
    {
        const uint8_t *l = at(P_LIGHT);
        if (l[L_MODE]) {
            layer_decode(at(P_STORED) + 3, keys);
        } else {
            lit_key one{hardware_to_colour(get16(l + L_RED)), hardware_to_colour(get16(l + L_RED + 2)), hardware_to_colour(get16(l + L_RED + 4)), true};
            std::fill(keys, keys + LEDS, one);
        }
        keys_changed = false;
    }

    void lights_to_reports()
    {
        // every key the same colour is what the keyboard calls plain mode, one colour in the settings and thats it.
        // the per key report gets the same picture either way, so whoever looks there sees no stale colours
        // (only the slots with a key count, the others are dark whatever was asked for. slot 0 is M1)
        bool plain = true;
        for (int k = 1; k < LEDS && plain; ++k)
            plain = !has_key(k) || (keys[k].on && keys[0].on && keys[k].r == keys[0].r && keys[k].g == keys[0].g && keys[k].b == keys[0].b);
        uint8_t *l = at(P_LIGHT);
        l[L_MODE] = plain ? 0 : 1;
        if (plain) {
            put16(l + L_RED, COLOUR_TO_HARDWARE[keys[0].r]);
            put16(l + L_RED + 2, COLOUR_TO_HARDWARE[keys[0].g]);
            put16(l + L_RED + 4, COLOUR_TO_HARDWARE[keys[0].b]);
        }
        layer_encode(keys, at(P_STORED) + 3);
        dirty[P_LIGHT] = dirty[P_STORED] = true;
        keys_changed = false;
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
        lights_from_reports();
        return ROCKCAT_OK;
    }

    int read_light(rockcat_device *dev) override
    {
        for (int p : {P_LIGHT, P_STORED}) {
            int r = fetch(dev, p);
            if (r != ROCKCAT_OK)
                return r;
        }
        lights_from_reports();
        return ROCKCAT_OK;
    }

    int write(rockcat_device *dev) override
    {
        if (keys_changed)
            lights_to_reports();
        // macros go before the keys that point at them
        while (!macros.empty()) {
            int r = macro_halves_write(dev, REPORT_MACRO, macros.front());
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
        ryos_profile copy(*this);
        if (copy.keys_changed)
            copy.lights_to_reports();
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
        lights_from_reports();
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = at(P_LIGHT);
        switch (which) {
        case ROCKCAT_SET_BRIGHTNESS:      *value = std::min<int>(l[L_BRIGHTNESS], BRIGHTNESS_STEPS) * 255 / BRIGHTNESS_STEPS; break;
        case ROCKCAT_SET_IDLE_BRIGHTNESS: *value = std::min<int>(l[L_IDLE_BRIGHTNESS], BRIGHTNESS_STEPS) * 255 / BRIGHTNESS_STEPS; break;
        case ROCKCAT_SET_IDLE_MINUTES:    *value = l[L_IDLE_MINUTES]; break;
        case ROCKCAT_SET_IDLE_STYLE:      *value = l[L_IDLE_STYLE]; break;
        case ROCKCAT_SET_MACRO_FEEDBACK:  *value = l[L_FEEDBACK] ? 1 : 0; break;
        case ROCKCAT_SET_EFFECT_SPEED:    *value = l[L_SPEED]; break;
        case ROCKCAT_SET_EFFECT:
            // a number the keyboard has and we dont know reads as "fully lit", thats what it would look like to us anyway
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
        case ROCKCAT_SET_IDLE_BRIGHTNESS:
            // the keyboard knows 6 steps, 0 to 5. whatever comes in goes to the nearest one
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            l[which == ROCKCAT_SET_BRIGHTNESS ? L_BRIGHTNESS : L_IDLE_BRIGHTNESS] = static_cast<uint8_t>((value * BRIGHTNESS_STEPS + 127) / 255);
            break;
        case ROCKCAT_SET_IDLE_MINUTES:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_IDLE_MINUTES] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_IDLE_STYLE:
            if (value != ROCKCAT_IDLE_DIM && value != ROCKCAT_IDLE_STARLIT_SKY && value != ROCKCAT_IDLE_FALL_ASLEEP)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_IDLE_STYLE] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_MACRO_FEEDBACK:
            l[L_FEEDBACK] = value ? 1 : 0;
            break;
        case ROCKCAT_SET_EFFECT_SPEED:
            if (value < 1 || value > 3)
                return ROCKCAT_ERR_ARGUMENT;
            l[L_SPEED] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT: {
            if (value < 0 || value > 31 || (!(caps().effects & (1u << value)) && value != caps().carrier_effect))
                return ROCKCAT_ERR_ARGUMENT;
            bool known = false;
            for (const auto &e : EFFECTS)
                if (e[0] == value) {
                    l[L_EFFECT] = static_cast<uint8_t>(e[1]);
                    known = true;
                }
            // an effect of ours that the keyboard has no number for. whoever asked gets told and stores the carrier instead
            if (!known)
                return ROCKCAT_ERR_UNSUPPORTED;
            break;
        }
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= LEDS)
            return ROCKCAT_ERR_ARGUMENT;
        if (c)
            *c = rockcat_color{255, keys[led].r, keys[led].g, keys[led].b};
        if (enabled)
            *enabled = keys[led].on;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= LEDS)
            return ROCKCAT_ERR_ARGUMENT;
        // no brightness per key, there is one for the whole board. so alpha gets folded into the colour
        keys[led] = lit_key{static_cast<uint8_t>((c.r * c.a + 127) / 255), static_cast<uint8_t>((c.g * c.a + 127) / 255),
                            static_cast<uint8_t>((c.b * c.a + 127) / 255), enabled != 0};
        keys_changed = true;
        return ROCKCAT_OK;
    }

    // where the three bytes (or the one byte) of a key on a layer are. part < 0 = this key has nothing on that layer
    struct spot {
        int part = -1;
        size_t at = 0;
        bool wide = false; // three bytes and not one
    };

    static spot find(int layer, int input)
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
            case K_FN:       return {P_EXTRA, 4, false};
            default:         return {};
            }
        }
        if (layer == 1) {
            switch (k.kind) {
            case K_ZONE:     return {P_EASYZONE, static_cast<size_t>(4 + 3 * k.at), true};
            case K_MACRO:    return {P_MACROKEYS, static_cast<size_t>(3 + 3 * (MACRO_KEYS + k.at)), true};
            case K_THUMB:    return {P_THUMB, static_cast<size_t>(3 + 3 * (THUMB_KEYS + k.at)), true};
            default:         return {};
            }
        }
        // the second half of the function key report is what the key does with Fn held, not with Easy-Shift. roccat-tools files
        // it under Easy-Shift, a real keyboard says otherwise: Fn + F1 mutes, Easy-Shift + F1 does not
        if (layer == 2 && k.kind == K_FUNCTION)
            return {P_FUNCTION, static_cast<size_t>(3 + 3 * (FUNCTION_KEYS + k.at)), true};
        return {};
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer >= caps().layer_count || place(input).kind == K_NONE)
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
        if (!a || layer < 0 || layer >= caps().layer_count || place(input).kind == K_NONE)
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        // a one byte key has no room for parameters, so no shortcut with modifiers on those
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
        std::memcpy(at(P_PRIMARY) + 3, FACTORY_PRIMARY, sizeof FACTORY_PRIMARY);
        std::memcpy(at(P_FUNCTION) + 3, FACTORY_FUNCTION, sizeof FACTORY_FUNCTION);
        std::memcpy(at(P_MACROKEYS) + 3, FACTORY_MACROKEYS, sizeof FACTORY_MACROKEYS);
        std::memcpy(at(P_THUMB) + 3, FACTORY_THUMB, sizeof FACTORY_THUMB);
        std::memcpy(at(P_EXTRA) + 3, FACTORY_EXTRA, sizeof FACTORY_EXTRA);
        std::memcpy(at(P_EASYZONE) + 4, FACTORY_EASYZONE, sizeof FACTORY_EASYZONE);
        for (int p : {P_PRIMARY, P_FUNCTION, P_MACROKEYS, P_THUMB, P_EXTRA, P_EASYZONE})
            dirty[p] = true;
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (layer < 0 || layer >= caps().layer_count || place(input).kind == K_NONE)
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return 0;
        // a one byte key can only be another key or a function without parameters
        if (!s.wide)
            return ROCKCAT_ROOM_KEY;
        return ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS | (macro_slot(layer, input) >= 0 ? ROCKCAT_ROOM_MACRO : 0);
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (layer < 0 || layer >= caps().layer_count || place(input).kind == K_NONE)
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        // the factory tables are the reports without their header (three bytes, four for the Easy-Shift one)
        const uint8_t *table = nullptr;
        size_t head = 3;
        switch (s.part) {
        case P_PRIMARY:   table = FACTORY_PRIMARY; break;
        case P_FUNCTION:  table = FACTORY_FUNCTION; break;
        case P_MACROKEYS: table = FACTORY_MACROKEYS; break;
        case P_THUMB:     table = FACTORY_THUMB; break;
        case P_EXTRA:     table = FACTORY_EXTRA; break;
        case P_EASYZONE:  table = FACTORY_EASYZONE; head = 4; break;
        default:          return ROCKCAT_ERR_UNSUPPORTED;
        }
        const uint8_t *p = table + (s.at - head);
        *a = s.wide ? rockcat_assignment{p[0], p[1], p[2]} : rockcat_assignment{p[0], 0, 0};
        return ROCKCAT_OK;
    }

    // macros are numbered on their own: M keys 0 to 9 (normal, then with Easy-Shift), thumb keys 10 to 15, and from 16 on
    // the keys of the Easy-Shift report. everything else cant hold one
    int macro_slot(int layer, int input) const
    {
        const key_place &k = place(input);
        if (layer < 0 || layer >= caps().layer_count)
            return -1;
        switch (k.kind) {
        case K_MACRO: return layer * MACRO_KEYS + k.at;
        case K_THUMB: return 2 * MACRO_KEYS + layer * THUMB_KEYS + k.at;
        case K_ZONE:  return layer == 1 ? 16 + k.at : -1;
        default:      return -1;
        }
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        if (!m || layer < 0 || layer >= caps().layer_count || place(input).kind == K_NONE)
            return ROCKCAT_ERR_ARGUMENT;
        int slot = macro_slot(layer, input);
        return slot < 0 ? ROCKCAT_ERR_UNSUPPORTED : macro_halves_read(dev, REPORT_MACRO, index, slot, m);
    }

    int set_macro(int layer, int input, const rockcat_macro *m) override
    {
        if (!m || m->count > ROCKCAT_MACRO_KEYSTROKES || layer < 0 || layer >= caps().layer_count || place(input).kind == K_NONE)
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

static_assert(sizeof FACTORY_PRIMARY == 120 && sizeof FACTORY_FUNCTION == 90 && sizeof FACTORY_MACROKEYS == 30 && sizeof FACTORY_THUMB == 18 &&
                  sizeof FACTORY_EXTRA == 3 && sizeof FACTORY_EASYZONE == 288,
              "the factory tables have to fill their reports");
static_assert(3 + LAYER_SIZE + 6 + 2 == 239, "report 17 is header, layer, six loose bytes, checksum");

rockcat_profile *new_profile(const device_desc *desc, int index)
{
    return new ryos_profile(desc, index);
}

// info report is [0f 08 firmware ...], 0x83 = 131 = what the vendor calls 1.31
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[8] = {REPORT_INFO};
    int r = get_raw(dev, info, sizeof info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

// [05 03 xy]: y is the active profile, x how many profiles are in use (5 on every keyboard seen so far)
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t rep[3] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = rep[2] & 0x0f;
    if (count)
        *count = rep[2] >> 4;
    return ROCKCAT_OK;
}

int set_active(rockcat_device *dev, uint8_t profile)
{
    if (profile >= PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    // the count goes back the way it came, we only move the other half of the byte
    uint8_t rep[3] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    rep[1] = 0x03;
    rep[2] = static_cast<uint8_t>((rep[2] & 0xf0) | profile);
    return set_raw(dev, rep, sizeof rep, true);
}

// lighting from the pc: report 13 says who owns the leds, report 18 is a frame. after a frame the keyboard wants to be asked
// through report 13 whether it took it (byte 6: 1 yes, 3 still busy), thats quicker than the usual wait on 04
int light_control(rockcat_device *dev, bool host, bool wait)
{
    const uint8_t rep[8] = {REPORT_LIGHT_CONTROL, 0x08, static_cast<uint8_t>(host ? 1 : 0)};
    return set_raw(dev, rep, sizeof rep, wait);
}

int direct_begin(rockcat_device *dev)
{
    return light_control(dev, true, true);
}

int direct_end(rockcat_device *dev)
{
    return light_control(dev, false, true);
}

int direct_switch_fast(rockcat_device *dev, bool host)
{
    return light_control(dev, host, false);
}

int direct_frame(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!colors || count != LEDS)
        return ROCKCAT_ERR_ARGUMENT;
    lit_key keys[LEDS];
    for (int k = 0; k < LEDS; ++k) {
        const rockcat_color &c = colors[k];
        keys[k] = lit_key{static_cast<uint8_t>((c.r * c.a + 127) / 255), static_cast<uint8_t>((c.g * c.a + 127) / 255),
                          static_cast<uint8_t>((c.b * c.a + 127) / 255), true};
        // every key stays "on" and a dark key is simply black. the first version switched black keys off (state bit 0) to
        // save a palette entry, and a frame with most keys off made a real keyboard show garbage on all of them
    }
    uint8_t rep[2 + LAYER_SIZE + 2] = {REPORT_CUSTOM_LIGHTS, sizeof rep};
    layer_encode(keys, rep + 2);
    put_checksum(rep, sizeof rep);
    int r = set_raw(dev, rep, sizeof rep, false);
    if (r != ROCKCAT_OK)
        return r;
    dev->io->sleep_ms(10);
    for (int i = 0; i < 200; ++i) {
        uint8_t check[8] = {REPORT_LIGHT_CONTROL};
        r = get_raw(dev, check, sizeof check);
        if (r != ROCKCAT_OK)
            return r;
        if (check[6] == 1)
            return ROCKCAT_OK;
        if (check[6] != 3)
            return ROCKCAT_ERR_REJECTED;
        dev->io->sleep_ms(2);
    }
    return ROCKCAT_ERR_BUSY;
}

// nobody has seen this keyboard take its leds back by itself yet
int direct_active(rockcat_device *)
{
    return 1;
}

// the keyboard talks on report 03: [03 subtype type data action]. names out of roccat-tools. Easy-Shift and open driver were
// watched on a real keyboard, the profile ones not yet. Fn and the M keys say nothing here
bool decode_event(const uint8_t *in, int n, rockcat_event *ev)
{
    if (in[0] != 0x03 || n < 5)
        return false;
    *ev = rockcat_event{ROCKCAT_EVENT_NONE, 0, 0};
    switch (in[2]) {
    case 0x01: // profile at power up
    case 0x02: // profile switched on the keyboard
        *ev = rockcat_event{ROCKCAT_EVENT_CHANGED, in[3], 0};
        break;
    // Easy-Shift comes twice and each one says it its own way: [03 00 0a ff 00] down / [.. 0a ff 01] up,
    // and [03 00 fd 01 00] on / [.. fd 00 00] off. whoever listens gets the same event two times, which hurts nobody
    case 0x0a:
        *ev = rockcat_event{ROCKCAT_EVENT_EASYSHIFT, 0, in[4] == 0};
        break;
    case 0xfd:
        *ev = rockcat_event{ROCKCAT_EVENT_EASYSHIFT, 0, in[3] != 0};
        break;
    // only while the active profile has one of the effects the pc draws: every key says when it goes down and up,
    // [03 00 fb led+16 01] down, [.. 00] up. seen on a real keyboard (Esc = 11, A = 4d, Space = 80)
    case 0xfb:
        if (in[3] >= 16)
            *ev = rockcat_event{ROCKCAT_EVENT_KEY, in[3] - 16, in[4] == 1};
        break;
    case 0x10: // a key set to "open driver" (T2 out of the box): [03 00 10 01 00] down, [.. 10 00 00] up
        *ev = rockcat_event{ROCKCAT_EVENT_HOST_ACTION, 0x10, in[3] != 0};
        break;
    default:
        break;
    }
    return true;
}

// keys get their names from the key table of the device (profile.cpp looks there first), these are for whatever isnt in it
const char *input_label(int)
{
    return "?";
}

const char *led_label(int)
{
    return "?";
}

const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : layer == 2 ? "Fn" : "?";
}

// what a key can be set to: another key (its HID usage) or one of the functions of the keyboard in the list below
struct type_names {
    std::string text[256];
    bool function[256] = {};
    type_names()
    {
            for (int i = 1; i < 256; ++i) {
                char buf[32] = "";
                rockcat_format_shortcut(0, static_cast<uint8_t>(i), buf, sizeof buf);
                text[i] = buf;
            }
            const struct {
                int code;
                const char *name;
            } known[] = {
                {0x00, "disabled"},     {0x81, "shutdown"},      {0x82, "sleep"},          {0x83, "wake"},
                {0xa3, "light"},        {0xa7, "record"},        {0xaa, "m1"},             {0xab, "m2"},
                {0xac, "m3"},           {0xad, "m4"},            {0xae, "m5"},             {0xb0, "profile-up"},
                {0xb1, "profile-down"}, {0xb2, "profile-cycle"}, {0xb3, "profile-1"},      {0xb4, "profile-2"},
                {0xb5, "profile-3"},    {0xb6, "profile-4"},     {0xb7, "profile-5"},      {0xb8, "shortcut"},
                {0xb9, "quicklaunch"},  {0xba, "macro-pause"},   {0xbc, "timer"},          {0xbd, "timer-stop"},
                {0xbe, "macro"},        {0xbf, "led-macro"},     {0xc0, "email"},          {0xc1, "calculator"},
                {0xc2, "my-computer"},  {0xc3, "browser"},       {0xc4, "www-search"},     {0xc5, "www-home"},
                {0xc6, "www-back"},     {0xc7, "www-forward"},   {0xc8, "www-stop"},       {0xc9, "www-refresh"},
                {0xca, "www-favorites"}, {0xcb, "open-player"},  {0xcc, "play-pause"},     {0xcd, "next-track"},
                {0xce, "prev-track"},   {0xcf, "stop"},          {0xd0, "volume-up"},      {0xd1, "volume-down"},
                {0xd2, "mute"},         {0xd3, "shift-1"},       {0xd4, "shift-2"},        {0xd5, "shift-3"},
                {0xd6, "shift-4"},      {0xd7, "shift-5"},       {0xd8, "ctrl-1"},         {0xd9, "ctrl-2"},
                {0xda, "ctrl-3"},       {0xdb, "ctrl-4"},        {0xdc, "ctrl-5"},         {0xdd, "open-driver"},
                {0xf1, "fn"},           {0xf5, "talk-easyshift"}, {0xf6, "talk-easyshift-lock"}, {0xff, "easyshift"},
            };
            for (const auto &k : known) {
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
    return names().text[type].empty() ? "?" : names().text[type].c_str();
}

bool type_is_function(uint8_t type)
{
    return names().function[type];
}

} // namespace

const family_ops RYOS_FAMILY = {firmware,     get_active,   set_active,         new_profile,   direct_begin, direct_frame, direct_end,
                                direct_switch_fast, direct_active, decode_event, input_label, led_label,    layer_label,  type_label,
                                type_is_function};
