// Lua, the little ambidextrous one from before Swarm. it can be told things but never asked: every setting goes out as an
// 8 byte command on feature report 03, nothing comes back (report 03 reads as zeros). so the settings live on the pc,
// in the config dir, and a write sends all of them again. docs/protocol/lua.md

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

constexpr uint16_t PID = 0x2c2e;
constexpr uint8_t REPORT_CONTROL = 0x03;
enum : uint8_t { CMD_CPI = 0xa0, CMD_KEYS = 0xb0, CMD_POLLING = 0xc0, CMD_LIGHT = 0xd0, CMD_END = 0xee };

constexpr int INPUTS = 6, CPIS = 7;
constexpr int CPI_VALUES[CPIS] = {250, 500, 1000, 1250, 1500, 1750, 2000};
// the polling byte counts down: 0 = 1000 Hz
constexpr int POLLING_VALUES[4] = {1000, 500, 250, 125};

// what the mouse does with a button, roccat-tools names
enum : uint8_t {
    K_CLICK = 0x01, K_MENU = 0x02, K_SCROLL = 0x04, K_FORWARD = 0x08, K_BACK = 0x10,
    K_CPI_CYCLE = 0x81, K_WHEEL_UP = 0x82, K_WHEEL_DOWN = 0x83, K_DISABLED = 0x85,
};
// buttons in roccat-tools order: left, right, middle, wheel up, wheel down, the one behind the wheel
constexpr uint8_t FACTORY_KEYS[INPUTS] = {K_CLICK, K_MENU, K_SCROLL, K_WHEEL_UP, K_WHEEL_DOWN, K_CPI_CYCLE};

// light byte as the mouse wants it, and as we keep it
enum : uint8_t { LIGHT_ON = 0x11, LIGHT_BREATHING = 0x20, LIGHT_OFF = 0x10 };

// our own picture of a profile, also what a backup holds. no report of the mouse looks like this, it has none
enum : size_t { S_VERSION, S_CPI_MASK, S_CPI, S_POLLING, S_LIGHT, S_LEFT_HANDED, S_KEYS, S_SIZE = S_KEYS + INPUTS };
constexpr uint8_t FORMAT = 1;

struct lua_profile final : rockcat_profile {
    uint8_t s[S_SIZE] = {FORMAT, 0x7f, 2, 0, LIGHT_ON, 0, FACTORY_KEYS[0], FACTORY_KEYS[1], FACTORY_KEYS[2],
                         FACTORY_KEYS[3], FACTORY_KEYS[4], FACTORY_KEYS[5]};

    using rockcat_profile::rockcat_profile;
    rockcat_profile *clone() const override { return new lua_profile(*this); }
    const rockcat_caps &caps() const { return desc->caps; }

    bool sane(const uint8_t *b) const
    {
        if (b[S_VERSION] != FORMAT || !(b[S_CPI_MASK] & 0x7f) || b[S_CPI] >= CPIS || b[S_POLLING] > 3 || b[S_LEFT_HANDED] > 1)
            return false;
        return b[S_LIGHT] == LIGHT_ON || b[S_LIGHT] == LIGHT_BREATHING || b[S_LIGHT] == LIGHT_OFF;
    }

    // "what we last sent", from the config dir. nothing there yet = the factory settings, which is what a Lua nobody touched has
    int read(rockcat_device *) override
    {
        std::string hex = config_get(desc->pid, 0, "lua_settings");
        uint8_t b[S_SIZE] = {};
        if (hex.size() == 2 * S_SIZE) {
            for (size_t i = 0; i < S_SIZE; ++i)
                b[i] = static_cast<uint8_t>(std::stoi(hex.substr(2 * i, 2), nullptr, 16));
            if (sane(b))
                std::memcpy(s, b, S_SIZE);
        }
        return ROCKCAT_OK;
    }

    int send(rockcat_device *dev, uint8_t cmd, std::initializer_list<uint8_t> data)
    {
        uint8_t rep[8] = {REPORT_CONTROL, cmd};
        size_t i = 2;
        for (uint8_t d : data)
            rep[i++] = d;
        uint8_t sum = 0;
        for (int k = 1; k < 7; ++k)
            sum = static_cast<uint8_t>(sum + rep[k]);
        rep[7] = sum;
        // no status report to wait on, roccat-tools just gives it 100 ms between commands
        int r = set_raw(dev, rep, sizeof rep, false);
        dev->io->sleep_ms(100);
        return r;
    }

    // everything, every time, in the order and with the pauses roccat-tools uses. a second or so
    int write(rockcat_device *dev) override
    {
        uint8_t list[CPIS] = {};
        uint8_t n = 0;
        for (int i = 0; i < CPIS; ++i)
            if ((s[S_CPI_MASK] >> i) & 1)
                list[n++] = static_cast<uint8_t>(i + 1);
        const uint8_t left = s[S_LEFT_HANDED] ? s[S_KEYS + 1] : s[S_KEYS], right = s[S_LEFT_HANDED] ? s[S_KEYS] : s[S_KEYS + 1];
        int r = ROCKCAT_OK;
        auto go = [&](uint8_t cmd, std::initializer_list<uint8_t> data) {
            if (r == ROCKCAT_OK)
                r = send(dev, cmd, data);
        };
        go(CMD_CPI, {list[0], list[1], list[2], list[3], list[4]});
        go(CMD_CPI | 1, {list[5], list[6], n});
        go(CMD_CPI | 2, {static_cast<uint8_t>(s[S_CPI] + 1)});
        go(CMD_KEYS, {left, right, s[S_KEYS + 2], s[S_KEYS + 5], s[S_KEYS + 3]});
        go(CMD_KEYS | 1, {s[S_KEYS + 4]});
        go(CMD_POLLING, {s[S_POLLING]});
        go(CMD_LIGHT, {s[S_LIGHT]});
        if (r != ROCKCAT_OK)
            return r;
        dev->io->sleep_ms(400);
        go(CMD_END, {});
        if (r != ROCKCAT_OK)
            return r;
        char hex[2 * S_SIZE + 1] = "";
        for (size_t i = 0; i < S_SIZE; ++i)
            std::snprintf(hex + 2 * i, 3, "%02x", s[i]);
        config_set(desc->pid, 0, "lua_settings", hex);
        return ROCKCAT_OK;
    }

    int set_index(int i) override { return i == 0 ? ROCKCAT_OK : ROCKCAT_ERR_ARGUMENT; }
    void export_payload(std::vector<uint8_t> &out) const override { out.insert(out.end(), s, s + S_SIZE); }
    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != S_SIZE || !sane(buf))
            return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(s, buf, S_SIZE);
        return ROCKCAT_OK;
    }

    // seven fixed steps, each on or off. the number of a step cant change, only whether the button behind the wheel visits it
    int get_dpi(int level, int *x, int *y, int *enabled) const override
    {
        if (level < 0 || level >= CPIS)
            return ROCKCAT_ERR_ARGUMENT;
        if (x)
            *x = CPI_VALUES[level];
        if (y)
            *y = CPI_VALUES[level];
        if (enabled)
            *enabled = (s[S_CPI_MASK] >> level) & 1;
        return ROCKCAT_OK;
    }

    int set_dpi(int level, int x, int y, int enabled) override
    {
        if (level < 0 || level >= CPIS || x != CPI_VALUES[level] || y != x)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t mask = static_cast<uint8_t>(enabled ? s[S_CPI_MASK] | (1 << level) : s[S_CPI_MASK] & ~(1 << level));
        // one has to stay, a mouse with no dpi at all is not a thing
        if (!mask)
            return ROCKCAT_ERR_ARGUMENT;
        s[S_CPI_MASK] = mask;
        return ROCKCAT_OK;
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:  *value = s[S_CPI]; break;
        case ROCKCAT_SET_POLLING_HZ:  *value = POLLING_VALUES[s[S_POLLING]]; break;
        case ROCKCAT_SET_LEFT_HANDED: *value = s[S_LEFT_HANDED]; break;
        case ROCKCAT_SET_EFFECT:
            *value = s[S_LIGHT] == LIGHT_OFF ? ROCKCAT_EFFECT_OFF : s[S_LIGHT] == LIGHT_BREATHING ? ROCKCAT_EFFECT_BREATHING : ROCKCAT_EFFECT_FULLY_LIT;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:
            if (value < 0 || value >= CPIS)
                return ROCKCAT_ERR_ARGUMENT;
            s[S_CPI] = static_cast<uint8_t>(value);
            return ROCKCAT_OK;
        case ROCKCAT_SET_POLLING_HZ:
            for (int i = 0; i < 4; ++i)
                if (POLLING_VALUES[i] == value) {
                    s[S_POLLING] = static_cast<uint8_t>(i);
                    return ROCKCAT_OK;
                }
            return ROCKCAT_ERR_ARGUMENT;
        case ROCKCAT_SET_LEFT_HANDED:
            s[S_LEFT_HANDED] = value ? 1 : 0;
            return ROCKCAT_OK;
        case ROCKCAT_SET_EFFECT:
            if (value != ROCKCAT_EFFECT_OFF && value != ROCKCAT_EFFECT_FULLY_LIT && value != ROCKCAT_EFFECT_BREATHING)
                return ROCKCAT_ERR_ARGUMENT;
            s[S_LIGHT] = value == ROCKCAT_EFFECT_OFF ? LIGHT_OFF : value == ROCKCAT_EFFECT_BREATHING ? LIGHT_BREATHING : LIGHT_ON;
            return ROCKCAT_OK;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
    }

    // the one light is the logo on the back, blue and nothing else. lit = the light setting says on or breathing
    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led != 0)
            return ROCKCAT_ERR_ARGUMENT;
        const bool on = s[S_LIGHT] != LIGHT_OFF;
        if (c)
            *c = on ? ONE_COLOUR_ON : rockcat_color{255, 0, 0, 0};
        if (enabled)
            *enabled = on;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led != 0)
            return ROCKCAT_ERR_ARGUMENT;
        // only the switch counts, the logo has its colour and that is that (a gui without a colour picker hands in black)
        (void)c;
        const bool on = enabled != 0;
        // switching it on keeps breathing if it was breathing, only off really changes something
        if (!on)
            s[S_LIGHT] = LIGHT_OFF;
        else if (s[S_LIGHT] == LIGHT_OFF)
            s[S_LIGHT] = LIGHT_ON;
        return ROCKCAT_OK;
    }

    static bool known_type(uint8_t t)
    {
        for (uint8_t k : {K_CLICK, K_MENU, K_SCROLL, K_FORWARD, K_BACK, K_CPI_CYCLE, K_WHEEL_UP, K_WHEEL_DOWN, K_DISABLED})
            if (t == k)
                return true;
        return false;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer != 0 || input < 0 || input >= INPUTS)
            return ROCKCAT_ERR_ARGUMENT;
        *a = rockcat_assignment{s[S_KEYS + input], 0, 0};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        // the mouse knows nine things a button can be and nothing else, no keys, no shortcuts
        if (!a || layer != 0 || input < 0 || input >= INPUTS || a->param1 || a->param2 || !known_type(a->type))
            return ROCKCAT_ERR_ARGUMENT;
        s[S_KEYS + input] = a->type;
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        std::memcpy(s + S_KEYS, FACTORY_KEYS, INPUTS);
        return ROCKCAT_OK;
    }

    int input_room(int layer, int input) const override
    {
        if (layer != 0 || input < 0 || input >= INPUTS)
            return ROCKCAT_ERR_ARGUMENT;
        return ROCKCAT_ROOM_KEY;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        if (!a || layer != 0 || input < 0 || input >= INPUTS)
            return ROCKCAT_ERR_ARGUMENT;
        *a = rockcat_assignment{FACTORY_KEYS[input], 0, 0};
        return ROCKCAT_OK;
    }
};

rockcat_profile *new_profile(const device_desc *desc, int index)
{
    return new lua_profile(desc, index);
}

// the mouse doesnt say. better no answer than a made up one
int firmware(rockcat_device *, char *, size_t)
{
    return ROCKCAT_ERR_UNSUPPORTED;
}

int get_active(rockcat_device *, uint8_t *profile, uint8_t *count)
{
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
bool decode_event(const uint8_t *, int, rockcat_event *)
{
    return false;
}

const char *input_label(int input)
{
    static const char *const names[INPUTS] = {"Left", "Right", "Middle", "Wheel up", "Wheel down", "Button behind the wheel"};
    return input >= 0 && input < INPUTS ? names[input] : "?";
}
const char *led_label(int led)
{
    return led == 0 ? "Logo" : "?";
}
const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : "?";
}

const char *type_label(uint8_t t)
{
    switch (t) {
    case K_CLICK:      return "click";
    case K_MENU:       return "menu";
    case K_SCROLL:     return "universal-scroll";
    case K_FORWARD:    return "browser-forward";
    case K_BACK:       return "browser-back";
    case K_CPI_CYCLE:  return "dpi-cycle";
    case K_WHEEL_UP:   return "scroll-up";
    case K_WHEEL_DOWN: return "scroll-down";
    case K_DISABLED:   return "disabled";
    default:           return "?";
    }
}

bool type_is_function(uint8_t)
{
    return true;
}

const family_ops LUA_FAMILY = {firmware,  get_active,    set_active,   new_profile, no_direct, no_frame,  no_direct,
                               no_switch, direct_active, decode_event, input_label, led_label, layer_label, type_label,
                               type_is_function};

constexpr uint32_t bit(int n)
{
    return 1u << n;
}

const device_desc DEVICES[] = {
    {PID, "ROCCAT Lua", &LUA_FAMILY, nullptr, 0x03,
     {ROCKCAT_KIND_MOUSE, "lua", ROCKCAT_TESTED_NO, "", ROCKCAT_HAS_DPI, 1, INPUTS, 1, 1, CPIS, 250, 2000, 250, {125, 250, 500, 1000, 0},
      bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BREATHING), 0, 0, 0, -1}},
};
const device_registration reg(DEVICES, 1);

// the pretend one checks what a real one would choke on (sizes, checksums, commands it doesnt know) and remembers the
// last of each, so a test can see what went out
struct lua_sim final : transport {
    std::map<uint8_t, std::vector<uint8_t>> last;
    int bad = 0;
    std::mutex lock;

    int get_feature(uint8_t *buf, size_t len) override
    {
        if (buf[0] != REPORT_CONTROL)
            return ROCKCAT_ERR_IO;
        std::memset(buf + 1, 0, len - 1);
        return static_cast<int>(std::min<size_t>(len, 8));
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        uint8_t sum = 0;
        for (size_t k = 1; k + 1 < len; ++k)
            sum = static_cast<uint8_t>(sum + buf[k]);
        const uint8_t cmd = len > 1 ? buf[1] : 0;
        const bool known = (cmd >= CMD_CPI && cmd <= (CMD_CPI | 2)) || cmd == CMD_KEYS || cmd == (CMD_KEYS | 1) || cmd == CMD_POLLING ||
                           cmd == CMD_LIGHT || cmd == CMD_END;
        if (len != 8 || buf[0] != REPORT_CONTROL || buf[7] != sum || !known) {
            ++bad;
            return ROCKCAT_ERR_IO;
        }
        last[cmd].assign(buf, buf + len);
        return ROCKCAT_OK;
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

struct lua_door final : transport {
    static lua_sim &shared()
    {
        static lua_sim sim;
        return sim;
    }
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

const sim_registration sim({"lua", PID, "ROCCAT Lua", 0, [] { return std::unique_ptr<transport>(new lua_door); }});

} // namespace
