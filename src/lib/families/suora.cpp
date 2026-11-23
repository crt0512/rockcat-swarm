// Suora and Suora FX: keyboards that dont do feature reports at all. everything goes as output reports to its vendor interface (2):
// [action payload(6) checksum], action = request, | 0x80 to ask for something, checksum = byte sum ^ 0x8b. answers come back as
// 8 byte input reports in the same shape, keys and macros as 64 byte packets after the answer. one setup, no profiles.
// the Suora out of roccat-tools 5.9.1 (suora/), the Suora FX out of ROCCAT_Swarm_Monitor.exe (suora_fx_device_c) and its firmware.
// never seen here. docs/protocol/suora.md

#include "../internal.h"
#include "../keyboards.h"
#include "../transport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {

#include "suora_keys.inc"
#include "suora_tables.inc"
#include "suora_fx_keys.inc"

// key coordinates are in the space of Swarms 1024x768 picture set, which is 676 x 270 for this board
const keyboard_layout SUORA_KEYBOARD = {SUORA_REGIONS, static_cast<int>(sizeof SUORA_REGIONS / sizeof SUORA_REGIONS[0]), 676, 270};
const keyboard_layout SUORA_FX_KEYBOARD = {SUORA_FX_REGIONS, static_cast<int>(sizeof SUORA_FX_REGIONS / sizeof SUORA_FX_REGIONS[0]), 676, 270};

// room for the bigger one (the Suora: 160 slots in 10 packets)
constexpr int KEYS = 160, PACKET = 64, KEY_BYTES = KEYS * 4, KEY_PACKETS = KEY_BYTES / PACKET;

// what differs. the Suora FX (firmware V3.00): 126 key slots in 8 packets (504 bytes and 8 spare), checksum 0xff - sum (byte sum
// xor ff), macros are type 04 there, and the light is per key rgb in a layout not worked out yet (rockcat leaves it alone).
// the firmware has no profiles either, Swarm kept five on the pc
struct model {
    uint16_t pid;
    int keys, packets;
    uint8_t xor_with;
    bool light;                 // the Suora illumination (effect, brightness) is understood
    const uint8_t *factory;     // nullptr: nobody knows the factory keys
    const keyboard_region *regions;
    int region_count;
};
const model MODELS[] = {
    {0x2fa8, 160, 10, 0x8b, true, FACTORY_KEYS, SUORA_REGIONS, static_cast<int>(sizeof SUORA_REGIONS / sizeof SUORA_REGIONS[0])},
    {0x3246, 126, 8, 0xff, false, nullptr, SUORA_FX_REGIONS, static_cast<int>(sizeof SUORA_FX_REGIONS / sizeof SUORA_FX_REGIONS[0])},
};
const model &model_of(uint16_t pid)
{
    for (const model &m : MODELS)
        if (m.pid == pid)
            return m;
    return MODELS[0];
}
const model &model_of(const rockcat_device *dev) { return model_of(dev->info.product_id); }
enum : uint8_t { REQ_INFO = 0x00, REQ_GAME_MODE = 0x02, REQ_LIGHT = 0x08, REQ_KEYS = 0x0d, REQ_MACRO = 0x11, GET = 0x80 };

// illumination: [? effect ? brightness ? ?]. brightness 0 to 10, 0x0b while breathing. roccat-tools default: breathing
enum : size_t { L_EFFECT = 1, L_BRIGHTNESS = 3, LIGHT_SIZE = 6 };
constexpr uint8_t BREATHING_LEVEL = 0x0b, LEVEL_MAX = 10;
constexpr uint8_t FACTORY_LIGHT[LIGHT_SIZE] = {0x00, 0x02, 0x00, 0x0b, 0x00, 0x00};

uint8_t checksum(const uint8_t *cmd, uint8_t xor_with) // the 7 bytes in front of the checksum
{
    unsigned sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += cmd[i];
    return static_cast<uint8_t>((sum & 0xff) ^ xor_with);
}

// one command, with the report id 0 in front for hidapi
int send(rockcat_device *dev, uint8_t action, const uint8_t *payload)
{
    uint8_t out[9] = {0, action};
    std::memcpy(out + 2, payload, 6);
    out[8] = checksum(out + 1, model_of(dev).xor_with);
    return dev->io->write(out, sizeof out);
}

// whatever the keyboard still had queued is not the answer to what we are about to ask
void drain(rockcat_device *dev)
{
    uint8_t junk[PACKET];
    for (int i = 0; i < 64 && dev->io->read(junk, sizeof junk, 0) > 0; ++i) {
    }
}

int read_exact(rockcat_device *dev, uint8_t *buf, size_t len)
{
    for (int tries = 0; tries < 20; ++tries) {
        int r = dev->io->read(buf, len, 100);
        if (r < 0)
            return r;
        if (r == static_cast<int>(len))
            return ROCKCAT_OK;
    }
    return ROCKCAT_ERR_BUSY; // nothing came back
}

// ask, wait the 100 ms roccat-tools waits, take the 8 byte answer. payload gets the 6 bytes of it
int ask(rockcat_device *dev, uint8_t request, const uint8_t *question, uint8_t *payload)
{
    drain(dev);
    int r = send(dev, GET | request, question);
    if (r != ROCKCAT_OK)
        return r;
    dev->io->sleep_ms(100);
    uint8_t in[8];
    r = read_exact(dev, in, sizeof in);
    if (r != ROCKCAT_OK)
        return r;
    if (in[0] == 0x00)
        return ROCKCAT_ERR_PROTOCOL;
    std::memcpy(payload, in + 1, 6);
    return ROCKCAT_OK;
}

// keys and macros: a declaration [? index packets ? ? ?], then that many 64 byte packets
int read_bulk(rockcat_device *dev, uint8_t request, const uint8_t *declaration, uint8_t *buf, int packets)
{
    uint8_t answer[6];
    int r = ask(dev, request, declaration, answer);
    if (r != ROCKCAT_OK)
        return r;
    if (answer[2] != packets)
        return ROCKCAT_ERR_PROTOCOL;
    for (int i = 0; i < packets; ++i) {
        r = read_exact(dev, buf + PACKET * i, PACKET);
        if (r != ROCKCAT_OK)
            return r;
    }
    return ROCKCAT_OK;
}

int write_bulk(rockcat_device *dev, uint8_t request, const uint8_t *declaration, const uint8_t *buf, int packets)
{
    uint8_t decl[6];
    std::memcpy(decl, declaration, 6);
    decl[2] = static_cast<uint8_t>(packets);
    int r = send(dev, request, decl);
    for (int i = 0; r == ROCKCAT_OK && i < packets; ++i) {
        uint8_t out[PACKET + 1] = {0};
        std::memcpy(out + 1, buf + PACKET * i, PACKET);
        r = dev->io->write(out, sizeof out);
    }
    return r;
}

constexpr uint8_t KEYS_DECLARATION[6] = {0x01, 0, 0, 0, 0, 0};

// a key slot: [type modifier1 key modifier2]
enum : uint8_t { T_KEY = 0x00, T_SYSTEM = 0x02, T_CONSUMER = 0x03, T_MACRO = 0x0d, T_OWN = 0x10 };

// type 10 is "the key itself" for the F keys, the six above the arrows, two arrows, right Alt and right Ctrl, by number.
// those slots take nothing else (roccat-tools: "does not support NORMAL"), except right Alt and right Ctrl
constexpr uint8_t OWN_USAGE[25] = {0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
                                   0x47, 0x00, 0x48, 0x4c, 0x4a, 0x4d, 0x4b, 0x4e, 0x4f, 0x50, 0xe6, 0xe4};
constexpr int RIGHT_ALT_SLOT = 105, RIGHT_CTRL_SLOT = 113;

// consumer usages <-> Ryos function codes (the names rockcat uses for keyboards)
constexpr uint16_t CONSUMER[][2] = {{0x18a, 0xc0}, {0x192, 0xc1}, {0x194, 0xc2}, {0x221, 0xc4}, {0x223, 0xc5}, {0x224, 0xc6},
                                    {0x225, 0xc7}, {0x226, 0xc8}, {0x227, 0xc9}, {0x22a, 0xca}, {0x183, 0xcb}, {0x0cd, 0xcc}, {0x0b5, 0xcd},
                                    {0x0b6, 0xce}, {0x0b7, 0xcf}, {0x0e9, 0xd0}, {0x0ea, 0xd1}, {0x0e2, 0xd2}};
constexpr uint8_t SYSTEM[][2] = {{1, 0x81}, {2, 0x82}, {4, 0x83}};
// modifiers as HID usages in the slot <-> Ryos shortcut mask (bit 0 ctrl, 1 shift, 2 alt, 3 win)
constexpr uint8_t MODIFIER[][2] = {{0xe0, 0x01}, {0xe1, 0x02}, {0xe2, 0x04}, {0xe3, 0x08}};
constexpr uint8_t KEPT = 0xfe; // anything else: [KEPT, type, key], stays as it is

uint8_t mask_of(uint8_t usage)
{
    for (const auto &m : MODIFIER)
        if (m[0] == usage)
            return m[1];
    return 0;
}

// the slots that are a key in some layout of the picture, the rest are empty places in the keyboard matrix
bool on_board(const model &m, int slot)
{
    if (slot < 0 || slot >= m.keys)
        return false;
    for (int r = 0; r < m.region_count; ++r)
        for (int i = 0; i < m.regions[r].count; ++i)
            if (m.regions[r].keys[i].led == slot)
                return true;
    return false;
}

bool own_slot(const model &m, int slot)
{
    return m.factory && m.factory[4 * slot] == T_OWN && slot != RIGHT_ALT_SLOT && slot != RIGHT_CTRL_SLOT;
}

bool is_function(uint8_t t) { return RYOS_FAMILY.type_is_function(t); }

rockcat_assignment decode(const model &m, const uint8_t *k)
{
    // on the Suora FX only plain keys, shortcuts and consumer usages are taken for what they are on the Suora. its type 01 goes
    // through a table Swarm loads from its config, its macros are 04. both are kept
    if (m.pid != 0x2fa8 && k[0] != T_KEY && k[0] != T_CONSUMER)
        return rockcat_assignment{KEPT, k[0], k[2]};
    switch (k[0]) {
    case T_KEY:
        if (!k[1] && !k[3])
            return rockcat_assignment{k[2], 0, 0}; // 00 = nothing, Ryos calls that disabled too
        if (mask_of(k[1]) && (!k[3] || mask_of(k[3])))
            return rockcat_assignment{ROCKCAT_KF_SHORTCUT, static_cast<uint8_t>(mask_of(k[1]) | mask_of(k[3])), k[2]};
        break;
    case T_OWN:
        if (k[1] == 0x00 && k[2] == 0x00 && k[3] == 0x00)
            return rockcat_assignment{0xf1, 0, 0}; // the Fn key
        if (k[1] == 0x01 && k[2] < sizeof OWN_USAGE && OWN_USAGE[k[2]])
            return rockcat_assignment{OWN_USAGE[k[2]], 0, 0};
        break;
    case T_SYSTEM:
        for (const auto &s : SYSTEM)
            if (s[0] == k[2])
                return rockcat_assignment{s[1], 0, 0};
        break;
    case T_CONSUMER:
        for (const auto &c : CONSUMER)
            if (c[0] == (k[2] | k[3] << 8))
                return rockcat_assignment{static_cast<uint8_t>(c[1]), 0, 0};
        break;
    default:
        break;
    }
    return rockcat_assignment{KEPT, k[0], k[2]};
}

// false = cant be written into that slot
bool encode(const model &m, int slot, const rockcat_assignment &a, uint8_t *k)
{
    if (own_slot(m, slot))
        return false;
    uint8_t out[4] = {T_KEY, 0, 0, 0};
    if (a.type == ROCKCAT_KF_SHORTCUT) {
        uint8_t mods[2] = {0, 0};
        int n = 0;
        for (const auto &m : MODIFIER)
            if (a.param1 & m[1]) {
                if (n == 2)
                    return false; // room for two modifiers
                mods[n++] = m[0];
            }
        out[1] = mods[0];
        out[2] = a.param2;
        out[3] = mods[1];
    } else if (a.param1 || a.param2) {
        return false;
    } else if (!is_function(a.type) || a.type == 0x00) {
        out[2] = a.type;
        // right Alt and right Ctrl come as type 10 after a reset, Swarm writes them plain. plain it is
    } else {
        bool found = false;
        for (const auto &s : SYSTEM)
            if (s[1] == a.type && m.pid == 0x2fa8) {
                out[0] = T_SYSTEM;
                out[2] = s[0];
                found = true;
            }
        for (const auto &c : CONSUMER)
            if (c[1] == a.type) {
                out[0] = T_CONSUMER;
                out[2] = c[0] & 0xff;
                out[3] = static_cast<uint8_t>(c[0] >> 8);
                found = true;
            }
        if (!found)
            return false;
    }
    std::memcpy(k, out, 4);
    return true;
}

struct suora_profile final : rockcat_profile {
    uint8_t keys[KEY_BYTES] = {};
    uint8_t light[LIGHT_SIZE] = {};
    bool keys_dirty = false, light_dirty = false;

    using rockcat_profile::rockcat_profile;
    rockcat_profile *clone() const override { return new suora_profile(*this); }
    const model &board() const { return model_of(desc->pid); }
    int key_bytes() const { return board().packets * PACKET; }

    int read_light(rockcat_device *dev) override
    {
        const uint8_t zero[6] = {};
        light_dirty = false;
        return board().light ? ask(dev, REQ_LIGHT, zero, light) : ROCKCAT_OK;
    }

    int read(rockcat_device *dev) override
    {
        int r = read_light(dev);
        if (r != ROCKCAT_OK)
            return r;
        keys_dirty = false;
        return read_bulk(dev, REQ_KEYS, KEYS_DECLARATION, keys, board().packets);
    }

    int write(rockcat_device *dev) override
    {
        if (light_dirty && board().light) {
            int r = send(dev, REQ_LIGHT, light);
            if (r != ROCKCAT_OK)
                return r;
            dev->io->sleep_ms(100);
            light_dirty = false;
        }
        if (keys_dirty) {
            int r = write_bulk(dev, REQ_KEYS, KEYS_DECLARATION, keys, board().packets);
            if (r != ROCKCAT_OK)
                return r;
            dev->io->sleep_ms(100);
            keys_dirty = false;
        }
        return ROCKCAT_OK;
    }

    int set_index(int i) override
    {
        if (i != 0)
            return ROCKCAT_ERR_ARGUMENT;
        keys_dirty = light_dirty = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        out.insert(out.end(), keys, keys + key_bytes());
        out.insert(out.end(), light, light + LIGHT_SIZE);
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        const size_t kb = static_cast<size_t>(key_bytes());
        if (len != kb + LIGHT_SIZE)
            return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(keys, buf, kb);
        std::memcpy(light, buf + kb, LIGHT_SIZE);
        return set_index(index);
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        if (!board().light)
            return ROCKCAT_ERR_UNSUPPORTED;
        switch (which) {
        case ROCKCAT_SET_EFFECT:
            *value = light[L_EFFECT] == 2 ? ROCKCAT_EFFECT_BREATHING : light[L_EFFECT] == 1 ? ROCKCAT_EFFECT_FULLY_LIT : ROCKCAT_EFFECT_OFF;
            break;
        case ROCKCAT_SET_BRIGHTNESS:
            *value = light[L_BRIGHTNESS] >= LEVEL_MAX ? 255 : light[L_BRIGHTNESS] * 255 / LEVEL_MAX;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        if (!board().light)
            return ROCKCAT_ERR_UNSUPPORTED;
        switch (which) {
        case ROCKCAT_SET_EFFECT:
            if (value == ROCKCAT_EFFECT_OFF)
                light[L_EFFECT] = 0;
            else if (value == ROCKCAT_EFFECT_FULLY_LIT) {
                light[L_EFFECT] = 1;
                if (light[L_BRIGHTNESS] > LEVEL_MAX)
                    light[L_BRIGHTNESS] = LEVEL_MAX;
            } else if (value == ROCKCAT_EFFECT_BREATHING) {
                light[L_EFFECT] = 2;
                light[L_BRIGHTNESS] = BREATHING_LEVEL;
            } else
                return ROCKCAT_ERR_ARGUMENT;
            break;
        case ROCKCAT_SET_BRIGHTNESS:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            // breathing has no brightness of its own, the byte says "breathing" there
            if (light[L_EFFECT] != 2)
                light[L_BRIGHTNESS] = static_cast<uint8_t>((value * LEVEL_MAX + 127) / 255);
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        light_dirty = true;
        return ROCKCAT_OK;
    }

    bool known(int layer, int input) const { return layer == 0 && on_board(board(), input); }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        *a = decode(board(), keys + 4 * input);
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *k = keys + 4 * input;
        rockcat_assignment now = decode(board(), k);
        if (now.type == a->type && now.param1 == a->param1 && now.param2 == a->param2)
            return ROCKCAT_OK;
        if (a->type == KEPT || !encode(board(), input, *a, k))
            return ROCKCAT_ERR_ARGUMENT;
        keys_dirty = true;
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (!known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        return own_slot(board(), input) ? 0 : ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || !known(layer, input))
            return ROCKCAT_ERR_ARGUMENT;
        if (!board().factory)
            return ROCKCAT_ERR_UNSUPPORTED;
        *a = decode(board(), board().factory + 4 * input);
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        if (!board().factory)
            return ROCKCAT_ERR_UNSUPPORTED;
        std::memcpy(keys, board().factory, static_cast<size_t>(key_bytes()));
        keys_dirty = true;
        return ROCKCAT_OK;
    }
};

rockcat_profile *new_profile(const device_desc *desc, int index) { return new suora_profile(desc, index); }

int firmware(rockcat_device *dev, char *buf, size_t len)
{
    // the Suora FX has no info request anybody found, bcdDevice like on the Vulcans
    if (model_of(dev).pid != 0x2fa8) {
        std::snprintf(buf, len, "%x.%02x", dev->info.release >> 8, dev->info.release & 0xff);
        return ROCKCAT_OK;
    }
    const uint8_t zero[6] = {};
    uint8_t info[6];
    int r = ask(dev, REQ_INFO, zero, info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[0], info[1]);
    return ROCKCAT_OK;
}

// one setup and nothing to switch. asking for the game mode proves somebody answers
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    const uint8_t zero[6] = {};
    uint8_t game[6];
    // the Suora FX gets asked for its light, which Swarm reads too
    int r = ask(dev, model_of(dev).pid == 0x2fa8 ? REQ_GAME_MODE : REQ_LIGHT, zero, game);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = 0;
    if (count)
        *count = 1;
    return ROCKCAT_OK;
}

int set_active(rockcat_device *, uint8_t profile) { return profile == 0 ? ROCKCAT_OK : ROCKCAT_ERR_ARGUMENT; }

int direct_begin(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_frame(rockcat_device *, const rockcat_color *, int) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_end(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_switch(rockcat_device *, bool) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_active(rockcat_device *) { return 0; }
bool decode_event(const uint8_t *, int, rockcat_event *) { return false; }
const char *input_label(int) { return "?"; }
const char *led_label(int) { return "?"; }
const char *layer_label(int layer) { return layer == 0 ? "Normal" : "?"; }
const char *type_label(uint8_t t) { return t == KEPT ? "kept" : RYOS_FAMILY.type_label(t); }
bool type_is_function(uint8_t t) { return t == KEPT || is_function(t); }

const family_ops SUORA_FAMILY = {firmware,     get_active,   set_active,    new_profile, direct_begin, direct_frame, direct_end,
                                 direct_switch, direct_active, decode_event, input_label, led_label,    layer_label,  type_label,
                                 type_is_function};

constexpr uint32_t bit(int n) { return 1u << n; }

// one layer, 160 key slots, no leds of its own (one backlight), its three effects
const device_desc DEVICES[] = {
    {0x2fa8, "ROCCAT Suora", &SUORA_FAMILY, nullptr, 0x00,
     {ROCKCAT_KIND_KEYBOARD, "suora", ROCKCAT_TESTED_NO, "", 0, 1, KEYS, 1, 0, 0, 0, 0, 0, {0},
      bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BREATHING), 0, 0, 0, -1},
     &SUORA_KEYBOARD, 150, 2},
    {0x3246, "ROCCAT Suora FX", &SUORA_FAMILY, nullptr, 0x00,
     {ROCKCAT_KIND_KEYBOARD, "suora", ROCKCAT_TESTED_NO, "", 0, 1, 126, 1, 0, 0, 0, 0, 0, {0}, 0, 0, 0, 0, -1}, &SUORA_FX_KEYBOARD, 150, 2},
};

const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// the pretend one: answers commands with 8 byte input reports, keys as ten 64 byte packets, takes them back the same way
struct suora_sim final : transport {
    const model &m;
    uint8_t keys[KEY_BYTES] = {};
    uint8_t light[LIGHT_SIZE];
    uint8_t game = 0;
    std::deque<std::vector<uint8_t>> queue;
    int incoming = 0; // packets of a key write still to come
    std::vector<uint8_t> collected;
    std::mutex lock;

    // the Suora FX pretends every key is itself where the picture has a key (nobody knows its factory table): A as 04 and so on
    // would need a usage per slot, so it gets plain 00 entries except a few with something to read back
    explicit suora_sim(const model &mm) : m(mm)
    {
        if (m.factory)
            std::memcpy(keys, m.factory, static_cast<size_t>(m.keys) * 4);
        std::memcpy(light, FACTORY_LIGHT, LIGHT_SIZE);
    }

    void answer(uint8_t action, const uint8_t *payload)
    {
        std::vector<uint8_t> a(8, 0);
        a[0] = action;
        std::memcpy(a.data() + 1, payload, 6);
        a[7] = checksum(a.data(), m.xor_with);
        queue.push_back(a);
    }

    int get_feature(uint8_t *, size_t) override { return ROCKCAT_ERR_IO; }
    int set_feature(const uint8_t *, size_t) override { return ROCKCAT_ERR_IO; }

    int write(const uint8_t *buf, size_t len) override
    {
        if (buf[0] != 0)
            return ROCKCAT_ERR_IO;
        if (incoming > 0) {
            if (len != PACKET + 1)
                return ROCKCAT_ERR_IO;
            collected.insert(collected.end(), buf + 1, buf + 1 + PACKET);
            if (--incoming == 0 && collected.size() == static_cast<size_t>(m.packets * PACKET))
                std::memcpy(keys, collected.data(), collected.size());
            return ROCKCAT_OK;
        }
        if (len != 9 || buf[8] != checksum(buf + 1, m.xor_with))
            return ROCKCAT_ERR_IO; // a real one would probably just ignore it, this way a test notices
        const uint8_t action = buf[1], *p = buf + 2;
        const uint8_t request = action & 0x7f;
        if (action & GET) {
            switch (request) {
            case REQ_INFO: {
                const uint8_t info[6] = {1, 24, 0, 0, 0, 0};
                answer(action, info);
                break;
            }
            case REQ_GAME_MODE: {
                const uint8_t g[6] = {game, 0, 0, 0, 0, 0};
                answer(action, g);
                break;
            }
            case REQ_LIGHT:
                answer(action, light);
                break;
            case REQ_KEYS: {
                uint8_t decl[6];
                std::memcpy(decl, p, 6);
                decl[2] = static_cast<uint8_t>(m.packets);
                answer(action, decl);
                for (int i = 0; i < m.packets; ++i)
                    queue.emplace_back(keys + PACKET * i, keys + PACKET * (i + 1));
                break;
            }
            default:
                break;
            }
            return ROCKCAT_OK;
        }
        switch (request) {
        case REQ_LIGHT:
            std::memcpy(light, p, LIGHT_SIZE);
            break;
        case REQ_GAME_MODE:
            game = p[0];
            break;
        case REQ_KEYS:
            incoming = p[2];
            collected.clear();
            break;
        default:
            break;
        }
        return ROCKCAT_OK;
    }

    int read(uint8_t *buf, size_t len, int timeout_ms) override
    {
        if (queue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeout_ms, 5)));
            return 0;
        }
        std::vector<uint8_t> r = queue.front();
        queue.pop_front();
        size_t n = std::min(len, r.size());
        std::memcpy(buf, r.data(), n);
        return static_cast<int>(n);
    }
    void sleep_ms(int) override {}
};

suora_sim &shared(uint16_t pid)
{
    static suora_sim suora(MODELS[0]), fx(MODELS[1]);
    return pid == 0x3246 ? fx : suora;
}

template <uint16_t PID> struct door final : transport {
    static suora_sim &shared() { return ::shared(PID); }
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
    int write(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(shared().lock);
        return shared().write(buf, len);
    }
    int read(uint8_t *buf, size_t len, int timeout_ms) override
    {
        std::lock_guard<std::mutex> g(shared().lock);
        return shared().read(buf, len, timeout_ms);
    }
    void sleep_ms(int) override {}
};

const sim_registration sim({"suora", 0x2fa8, "ROCCAT Suora", 2, [] { return std::unique_ptr<transport>(new door<0x2fa8>); }});
const sim_registration sim_fx({"suora-fx", 0x3246, "ROCCAT Suora FX", 2, [] { return std::unique_ptr<transport>(new door<0x3246>); }});

} // namespace
