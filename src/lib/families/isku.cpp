// Isku and Isku FX, the keyboards from before the Ryos. no layer of every key here: only the keys around the edges can be
// set (M keys, the 20 keys of the Easy-Shift zone, thumb keys, F keys, media keys, Caps Lock), each group in a report of its
// own and stored column wise (all types, then all modifiers, then all keys). the FX adds a light with a colour. roccat-tools
// (isku/libroccatisku, iskufx/libroccatiskufx), never on hardware. docs/protocol/isku.md

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

#include "../keyboards/isku.inc"

const keyboard_layout ISKU_KEYBOARD = {ISKU_REGIONS, static_cast<int>(sizeof ISKU_REGIONS / sizeof ISKU_REGIONS[0]), 680, 370};

constexpr uint16_t PID_ISKU = 0x319c, PID_ISKU_FX = 0x3264;
constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_MACRO = 0x0e, REPORT_INFO = 0x0f;
// inputs are roccat-tools key indices (M1 0 to Caps Lock 0x38), the keys nothing can be set on are 100 + Swarms number
constexpr int INPUTS = 359, PROFILES = 5;

struct part {
    uint8_t id, request;
    uint16_t size;
    uint8_t count; // keys in a column report, 0 = not one of those
};
enum { P_KEYMASK, P_FUNCTION, P_LIGHT, P_CAPS, P_MACROKEYS, P_EASYZONE, P_MEDIA, P_THUMB, PARTS };
constexpr part PARTS_ISKU[PARTS] = {{0x07, 0x50, 6, 0},   {0x08, 0x60, 41, 12}, {0x10, 0x70, 10, 0},  {0x13, 0x90, 6, 0},
                                    {0x0d, 0xa0, 35, 10}, {0x09, 0xb0, 65, 20}, {0x0a, 0xc0, 29, 8}, {0x0b, 0xd0, 23, 6}};
// the FX: same, only the light report is longer
constexpr part LIGHT_FX = {0x10, 0x70, 16, 0};

const part &pt(uint16_t pid, int p)
{
    return p == P_LIGHT && pid == PID_ISKU_FX ? LIGHT_FX : PARTS_ISKU[p];
}

// key indices
constexpr int K_M = 0x00, K_ZONE = 0x0a, K_THUMB = 0x1e, K_F = 0x24, K_MEDIA = 0x30, K_CAPS = 0x38;
constexpr uint8_t STANDARD = 0xef; // "does what is printed on it", what every key has out of the box

// light. Isku: [10 0a profile ? ? brightness dim minutes sum]. FX: [10 10 profile on breathing colourtype index r g b
// brightness dim minutes talkfx sum]
constexpr size_t L_BRIGHTNESS = 5, L_DIM = 6, L_MINUTES = 7;
constexpr size_t FX_ON = 3, FX_BREATHING = 4, FX_TYPE = 5, FX_LIGHT = 6, FX_BRIGHTNESS = 10, FX_DIM = 11, FX_MINUTES = 12;
enum : uint8_t { TYPE_PALETTE = 0, TYPE_OWN = 1, TYPE_FLOW = 3 };

struct spot {
    int part = -1, col = 0;
};

// where a key sits on a layer. layer 1 is Easy-Shift, the M and thumb keys have their second half in the same report
spot find(int layer, int input)
{
    if (layer == 0) {
        if (input >= K_M && input < K_M + 5)
            return {P_MACROKEYS, input - K_M};
        if (input >= K_THUMB && input < K_THUMB + 3)
            return {P_THUMB, input - K_THUMB};
        if (input >= K_F && input < K_F + 12)
            return {P_FUNCTION, input - K_F};
        if (input >= K_MEDIA && input < K_MEDIA + 8)
            return {P_MEDIA, input - K_MEDIA};
        if (input == K_CAPS)
            return {P_CAPS, 0};
    } else if (layer == 1) {
        if (input >= K_M && input < K_M + 5)
            return {P_MACROKEYS, 5 + input - K_M};
        if (input >= K_THUMB && input < K_THUMB + 3)
            return {P_THUMB, 3 + input - K_THUMB};
        if (input >= K_ZONE && input < K_ZONE + 20)
            return {P_EASYZONE, input - K_ZONE};
    }
    return {};
}

// the key index a macro is filed under: the one of the key on that layer, which for the second halves is a number of its own
// (shift M1 is 5, shift T1 is 0x21). only below F1
int macro_index(int layer, int input)
{
    spot s = find(layer, input);
    switch (s.part) {
    case P_MACROKEYS: return K_M + s.col;
    case P_THUMB:     return K_THUMB + s.col;
    case P_EASYZONE:  return K_ZONE + s.col;
    default:          return -1;
    }
}

bool known(int input)
{
    return find(0, input).part >= 0 || find(1, input).part >= 0;
}

// the Isku macro report: [0e size16 profile index type loop talk[24] set[24] name[24] count16 keys[500 x 4] sum]
constexpr size_t MACRO_SIZE = 0x823;
enum : size_t { MM_PROFILE = 3, MM_INDEX = 4, MM_LOOP = 6, MM_SET = 31, MM_NAME = 55, MM_COUNT = 79, MM_KEYS = 81 };
static_assert(MM_KEYS + 500 * 4 + 2 == MACRO_SIZE, "isku macro layout");

int macro_read(rockcat_device *dev, int profile, int index, rockcat_macro *m)
{
    int r = select_data(dev, static_cast<uint8_t>(profile), static_cast<uint8_t>(index));
    if (r != ROCKCAT_OK)
        return r;
    std::vector<uint8_t> b(MACRO_SIZE, 0);
    b[0] = REPORT_MACRO;
    r = get_raw(dev, b.data(), b.size());
    if (r != ROCKCAT_OK)
        return r;
    std::memset(m, 0, sizeof *m);
    m->profile = static_cast<uint8_t>(profile);
    m->button = static_cast<uint8_t>(index);
    // erased flash: no macro
    if (b[MM_PROFILE] == 0xff && b[MM_INDEX] == 0xff)
        return ROCKCAT_OK;
    if (!checksum_ok(b.data(), b.size()))
        return ROCKCAT_ERR_CHECKSUM;
    m->loop = b[MM_LOOP];
    std::memcpy(m->set_name, b.data() + MM_SET, 24);
    std::memcpy(m->name, b.data() + MM_NAME, 24);
    m->count = std::min<uint16_t>(get16(b.data() + MM_COUNT), ROCKCAT_MACRO_KEYSTROKES);
    for (int k = 0; k < m->count; ++k) {
        const uint8_t *e = b.data() + MM_KEYS + 4 * k;
        m->keys[k] = rockcat_keystroke{e[0], e[1], get16(e + 2)};
    }
    return ROCKCAT_OK;
}

int macro_write(rockcat_device *dev, const rockcat_macro &m)
{
    std::vector<uint8_t> b(MACRO_SIZE, 0);
    b[0] = REPORT_MACRO;
    put16(b.data() + 1, static_cast<uint16_t>(MACRO_SIZE));
    b[MM_PROFILE] = m.profile;
    b[MM_INDEX] = m.button;
    b[MM_LOOP] = m.loop;
    std::memcpy(b.data() + MM_SET, m.set_name, strnlen(m.set_name, 24));
    std::memcpy(b.data() + MM_NAME, m.name, strnlen(m.name, 24));
    put16(b.data() + MM_COUNT, m.count);
    for (int k = 0; k < m.count; ++k) {
        uint8_t *e = b.data() + MM_KEYS + 4 * k;
        e[0] = m.keys[k].key;
        e[1] = m.keys[k].action;
        put16(e + 2, m.keys[k].delay_ms);
    }
    put_checksum(b.data(), b.size());
    return set_raw(dev, b.data(), b.size(), true);
}

struct isku_profile final : rockcat_profile {
    const bool fx;
    std::vector<uint8_t> raw[PARTS];
    bool dirty[PARTS] = {};
    std::vector<rockcat_macro> macros;

    isku_profile(const device_desc *d, int i) : rockcat_profile(d, i), fx(d->pid == PID_ISKU_FX)
    {
        for (int p = 0; p < PARTS; ++p)
            raw[p].assign(pt(d->pid, p).size, 0);
    }
    rockcat_profile *clone() const override { return new isku_profile(*this); }
    const part &P(int p) const { return pt(desc->pid, p); }

    int read(rockcat_device *dev) override
    {
        std::fill(dirty, dirty + PARTS, false);
        macros.clear();
        for (int p = 0; p < PARTS; ++p) {
            int r = select_data(dev, static_cast<uint8_t>(index), P(p).request);
            if (r != ROCKCAT_OK)
                return r;
            std::vector<uint8_t> &b = raw[p];
            std::fill(b.begin(), b.end(), 0);
            b[0] = P(p).id;
            r = get_raw(dev, b.data(), b.size());
            if (r != ROCKCAT_OK)
                return r;
            if (b[1] != P(p).size || b[2] != index)
                return ROCKCAT_ERR_PROTOCOL;
            if (!checksum_ok(b.data(), b.size()))
                return ROCKCAT_ERR_CHECKSUM;
        }
        return ROCKCAT_OK;
    }

    int read_light(rockcat_device *dev) override
    {
        int r = select_data(dev, static_cast<uint8_t>(index), P(P_LIGHT).request);
        if (r != ROCKCAT_OK)
            return r;
        raw[P_LIGHT][0] = P(P_LIGHT).id;
        return get_raw(dev, raw[P_LIGHT].data(), raw[P_LIGHT].size());
    }

    int write(rockcat_device *dev) override
    {
        while (!macros.empty()) {
            int r = macro_write(dev, macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        for (int p = 0; p < PARTS; ++p) {
            if (!dirty[p])
                continue;
            put_checksum(raw[p].data(), raw[p].size());
            int r = set_raw(dev, raw[p].data(), raw[p].size(), true);
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
            raw[p][2] = static_cast<uint8_t>(i);
            dirty[p] = true;
        }
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        for (int p = 0; p < PARTS; ++p) {
            std::vector<uint8_t> b = raw[p];
            put_checksum(b.data(), b.size());
            out.insert(out.end(), b.begin(), b.end());
        }
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        size_t total = 0;
        for (int p = 0; p < PARTS; ++p)
            total += P(p).size;
        if (len != total)
            return ROCKCAT_ERR_ARGUMENT;
        size_t at = 0;
        for (int p = 0; p < PARTS; ++p) {
            if (buf[at] != P(p).id || buf[at + 1] != P(p).size)
                return ROCKCAT_ERR_ARGUMENT;
            at += P(p).size;
        }
        at = 0;
        for (int p = 0; p < PARTS; ++p) {
            raw[p].assign(buf + at, buf + at + P(p).size);
            at += P(p).size;
        }
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = raw[P_LIGHT].data();
        switch (which) {
        case ROCKCAT_SET_BRIGHTNESS:      *value = std::min<int>(l[fx ? FX_BRIGHTNESS : L_BRIGHTNESS], 5) * 51; break;
        case ROCKCAT_SET_IDLE_BRIGHTNESS: *value = std::min<int>(l[fx ? FX_DIM : L_DIM], 5) * 51; break;
        case ROCKCAT_SET_IDLE_MINUTES:    *value = l[fx ? FX_MINUTES : L_MINUTES]; break;
        case ROCKCAT_SET_EFFECT:
            if (!fx)
                return ROCKCAT_ERR_UNSUPPORTED;
            *value = !l[FX_ON] ? ROCKCAT_EFFECT_OFF : l[FX_BREATHING] ? ROCKCAT_EFFECT_BREATHING : ROCKCAT_EFFECT_FULLY_LIT;
            break;
        case ROCKCAT_SET_COLOR_FLOW:
            if (!fx)
                return ROCKCAT_ERR_UNSUPPORTED;
            *value = l[FX_TYPE] == TYPE_FLOW;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        uint8_t *l = raw[P_LIGHT].data();
        switch (which) {
        case ROCKCAT_SET_BRIGHTNESS:
        case ROCKCAT_SET_IDLE_BRIGHTNESS:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            l[which == ROCKCAT_SET_BRIGHTNESS ? (fx ? FX_BRIGHTNESS : L_BRIGHTNESS) : (fx ? FX_DIM : L_DIM)] =
                static_cast<uint8_t>((value * 5 + 127) / 255);
            break;
        case ROCKCAT_SET_IDLE_MINUTES:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            l[fx ? FX_MINUTES : L_MINUTES] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT:
            if (!fx)
                return ROCKCAT_ERR_UNSUPPORTED;
            if (value != ROCKCAT_EFFECT_OFF && value != ROCKCAT_EFFECT_FULLY_LIT && value != ROCKCAT_EFFECT_BREATHING)
                return ROCKCAT_ERR_ARGUMENT;
            l[FX_ON] = value != ROCKCAT_EFFECT_OFF;
            l[FX_BREATHING] = value == ROCKCAT_EFFECT_BREATHING;
            break;
        case ROCKCAT_SET_COLOR_FLOW:
            if (!fx)
                return ROCKCAT_ERR_UNSUPPORTED;
            // flow off goes back to our own colour, the palette entry would be a surprise
            l[FX_TYPE] = value ? TYPE_FLOW : TYPE_OWN;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (!fx || led != 0)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = raw[P_LIGHT].data() + FX_LIGHT;
        if (c)
            *c = rockcat_color{255, l[1], l[2], l[3]};
        if (enabled)
            *enabled = raw[P_LIGHT][FX_ON] != 0;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (!fx || led != 0)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *l = raw[P_LIGHT].data();
        l[FX_LIGHT + 1] = static_cast<uint8_t>((c.r * c.a + 127) / 255);
        l[FX_LIGHT + 2] = static_cast<uint8_t>((c.g * c.a + 127) / 255);
        l[FX_LIGHT + 3] = static_cast<uint8_t>((c.b * c.a + 127) / 255);
        if (l[FX_TYPE] != TYPE_FLOW)
            l[FX_TYPE] = TYPE_OWN;
        l[FX_ON] = enabled != 0;
        dirty[P_LIGHT] = true;
        return ROCKCAT_OK;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer > 1 || !known(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        const uint8_t *b = raw[s.part].data();
        const int n = P(s.part).count;
        *a = n ? rockcat_assignment{b[3 + s.col], b[3 + n + s.col], b[3 + 2 * n + s.col]} : rockcat_assignment{b[3], 0, 0};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        if (!a || layer < 0 || layer > 1 || !known(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        uint8_t *b = raw[s.part].data();
        const int n = P(s.part).count;
        if (!n && (a->param1 || a->param2))
            return ROCKCAT_ERR_ARGUMENT;
        b[3 + s.col] = a->type;
        if (n) {
            b[3 + n + s.col] = a->param1;
            b[3 + 2 * n + s.col] = a->param2;
        }
        dirty[s.part] = true;
        return ROCKCAT_OK;
    }

    static rockcat_assignment factory(int part)
    {
        return part == P_CAPS ? rockcat_assignment{0xff, 0, 0} : rockcat_assignment{STANDARD, 0, 0};
    }

    int reset_inputs() override
    {
        for (int p : {P_FUNCTION, P_CAPS, P_MACROKEYS, P_EASYZONE, P_MEDIA, P_THUMB}) {
            const int n = P(p).count;
            uint8_t *b = raw[p].data();
            if (!n) {
                b[3] = factory(p).type;
            } else {
                std::fill(b + 3, b + 3 + n, STANDARD);
                std::fill(b + 3 + n, b + 3 + 3 * n, 0);
            }
            dirty[p] = true;
        }
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (layer < 0 || layer > 1 || !known(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return 0;
        if (!P(s.part).count)
            return ROCKCAT_ROOM_KEY;
        return ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS | (macro_index(layer, input) >= 0 ? ROCKCAT_ROOM_MACRO : 0);
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer < 0 || layer > 1 || !known(input))
            return ROCKCAT_ERR_ARGUMENT;
        spot s = find(layer, input);
        if (s.part < 0)
            return ROCKCAT_ERR_UNSUPPORTED;
        *a = factory(s.part);
        return ROCKCAT_OK;
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        if (!m || layer < 0 || layer > 1 || !known(input))
            return ROCKCAT_ERR_ARGUMENT;
        int i = macro_index(layer, input);
        return i < 0 ? ROCKCAT_ERR_UNSUPPORTED : macro_read(dev, index, i, m);
    }

    int set_macro(int layer, int input, const rockcat_macro *m) override
    {
        if (!m || m->count > 500 || layer < 0 || layer > 1 || !known(input))
            return ROCKCAT_ERR_ARGUMENT;
        int i = macro_index(layer, input);
        if (i < 0)
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
    return new isku_profile(desc, index);
}

int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[6] = {REPORT_INFO};
    int r = get_raw(dev, info, sizeof info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t rep[3] = {REPORT_PROFILE};
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
    const uint8_t rep[3] = {REPORT_PROFILE, 0x03, profile};
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
// [03 rad event data data], not worked out
bool decode_event(const uint8_t *, int, rockcat_event *)
{
    return false;
}

// the media keys have no place on the picture (it is the Isku+ one, which has none), so they get their names from here
const char *input_label(int input)
{
    static const char *const media[8] = {"Mute", "Volume down", "Volume up", "Play/Pause", "Previous", "Next", "Browser", "My Computer"};
    if (input >= K_MEDIA && input < K_MEDIA + 8)
        return media[input - K_MEDIA];
    return input == K_CAPS ? "Caps Lock" : "?";
}
const char *led_label(int led)
{
    return led == 0 ? "Backlight" : "?";
}
const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift" : "?";
}

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
        } own[] = {{0xa0, "mute"},        {0xa1, "volume-up"},   {0xa2, "volume-down"}, {0xa4, "play-pause"},
                   {0xa5, "prev-track"},  {0xa6, "next-track"},  {0xa8, "browser"},     {0xa9, "my-computer"},
                   {0xdf, "unused"},      {0xef, "standard"}};
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
const char *type_label(uint8_t t)
{
    return names().text[t].c_str();
}
bool type_is_function(uint8_t t)
{
    return names().function[t];
}

const family_ops ISKU_FAMILY = {firmware,  get_active,    set_active,   new_profile, no_direct, no_frame,  no_direct,
                                no_switch, direct_active, decode_event, input_label, led_label, layer_label, type_label,
                                type_is_function};

constexpr uint32_t bit(int n)
{
    return 1u << n;
}

const device_desc DEVICES[] = {
    {PID_ISKU, "ROCCAT Isku", &ISKU_FAMILY, nullptr, 0x08,
     {ROCKCAT_KIND_KEYBOARD, "isku", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT, PROFILES, INPUTS, 2, 0, 0, 0,
      0, 0, {0}, 0, 0, 0, 0, -1},
     &ISKU_KEYBOARD},
    {PID_ISKU_FX, "ROCCAT Isku FX", &ISKU_FAMILY, nullptr, 0x08,
     {ROCKCAT_KIND_KEYBOARD, "isku", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB, PROFILES,
      INPUTS, 2, 1, 0, 0, 0, 0, {0}, bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BREATHING), 0, 0, 0,
      -1},
     &ISKU_KEYBOARD},
};
const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// pretend Isku / Isku FX: every key standard, Caps Lock Easy-Shift, brightness 3, 15 minutes, the FX in roccat blue
struct isku_sim final : transport {
    const uint16_t pid;
    uint8_t active = 0, select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> data[PROFILES][PARTS];
    std::map<int, std::vector<uint8_t>> macros;
    std::mutex lock;

    explicit isku_sim(uint16_t p) : pid(p)
    {
        for (uint8_t prof = 0; prof < PROFILES; ++prof)
            for (int i = 0; i < PARTS; ++i) {
                const part &q = pt(pid, i);
                std::vector<uint8_t> &b = data[prof][i];
                b.assign(q.size, 0);
                b[0] = q.id;
                b[1] = static_cast<uint8_t>(q.size);
                b[2] = prof;
                if (q.count)
                    std::fill(b.begin() + 3, b.begin() + 3 + q.count, STANDARD);
                if (i == P_CAPS)
                    b[3] = 0xff;
                if (i == P_KEYMASK)
                    b[3] = 0x3f;
                if (i == P_LIGHT && pid == PID_ISKU_FX) {
                    const uint8_t fxl[] = {1, 0, TYPE_OWN, 0x01, 0x00, 0x64, 0xfa, 3, 1, 15, 0};
                    std::copy(fxl, fxl + sizeof fxl, b.begin() + 3);
                } else if (i == P_LIGHT) {
                    b[3] = 3;
                    b[L_BRIGHTNESS] = 3;
                    b[L_DIM] = 1;
                    b[L_MINUTES] = 15;
                }
                put_checksum(b.data(), b.size());
            }
    }

    int part_of(uint8_t id) const
    {
        for (int i = 0; i < PARTS; ++i)
            if (pt(pid, i).id == id)
                return i;
        return -1;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        int p = part_of(buf[0]);
        if (p >= 0) {
            if (select_b != pt(pid, p).request || select_a >= PROFILES)
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
                out = {REPORT_PROFILE, 0x03, active};
                break;
            case REPORT_MACRO: {
                auto it = macros.find(select_a * 256 + select_b);
                if (it != macros.end()) {
                    out = it->second;
                } else {
                    out.assign(MACRO_SIZE, 0xff);
                    out[0] = REPORT_MACRO;
                }
                break;
            }
            case REPORT_INFO:
                out = {REPORT_INFO, 0x06, 0x6a, 0x01, 0, 0};
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
            const size_t want = p >= 0 ? pt(pid, p).size : MACRO_SIZE;
            const uint8_t profile = p >= 0 ? buf[2] : buf[MM_PROFILE];
            if (len != want || profile >= PROFILES || get16(buf + len - 2) != rockcat_checksum(buf, len - 2)) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            if (p >= 0)
                data[profile][p].assign(buf, buf + len);
            else
                macros[profile * 256 + buf[MM_INDEX]].assign(buf, buf + len);
            status_queue = {3};
            return ROCKCAT_OK;
        }
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            return ROCKCAT_OK;
        case REPORT_PROFILE:
            if (len != 3 || buf[2] >= PROFILES)
                return ROCKCAT_ERR_IO;
            active = buf[2];
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

isku_sim &shared_isku(uint16_t pid)
{
    static std::map<uint16_t, isku_sim> sims;
    static std::mutex guard;
    std::lock_guard<std::mutex> g(guard);
    auto it = sims.find(pid);
    if (it == sims.end())
        it = sims.emplace(std::piecewise_construct, std::forward_as_tuple(pid), std::forward_as_tuple(pid)).first;
    return it->second;
}

template <uint16_t PID> struct isku_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        isku_sim &s = shared_isku(PID);
        std::lock_guard<std::mutex> g(s.lock);
        return s.get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        isku_sim &s = shared_isku(PID);
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

template <uint16_t PID> std::unique_ptr<transport> make_isku_door()
{
    return std::unique_ptr<transport>(new isku_door<PID>);
}

const sim_registration sim_isku({"isku", PID_ISKU, "ROCCAT Isku", 0, make_isku_door<PID_ISKU>});
const sim_registration sim_isku_fx({"isku-fx", PID_ISKU_FX, "ROCCAT Isku FX", 0, make_isku_door<PID_ISKU_FX>});

} // namespace
