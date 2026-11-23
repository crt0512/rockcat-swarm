// Kova 2016 (gray, white, black). same conversation as the Kone AIMO (select on 04, settings in 06, buttons in 07, macros in 08)
// with a much smaller settings report: one byte per dpi level, two lights, no brightness. docs/protocol/kova.md has the layout
// structs and names come from roccat-tools (kova2016/libroccatkova2016), checked against a real gray one with firmware 1.18

#include "../internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t REPORT_SETTINGS = 0x06, REPORT_BUTTONS = 0x07, REPORT_MACRO = 0x08, REPORT_INFO = 0x09;
constexpr uint8_t SELECT_SETTINGS = 0x80, SELECT_BUTTONS = 0x90;
constexpr size_t SETTINGS_SIZE = 0x1c, BUTTONS_SIZE = 0x4b;
constexpr int INPUTS = 12, SLOTS = 2 * INPUTS, LEDS = 2;

// where things sit in the settings report
enum : size_t {
    S_PROFILE = 2,
    S_HANDED = 5,      // ff right, fe left
    S_DPI_ENABLED = 6, // bit n = level n
    S_DPI = 7,         // 5 levels, one byte each, times 50
    S_DPI_ACTIVE = 12,
    S_POLLING = 13,
    S_LIGHTS = 14,     // bit 0/1 = light on, bit 4/5 = light uses its own rgb instead of the palette entry
    S_FLOW = 15,
    S_EFFECT = 16,     // 1 fully lit, 2 blinking, 3 breathing. same numbers as rockcat_effect, how convenient
    S_SPEED = 17,
    S_LIGHT = 18,      // 2 lights, 4 bytes each: palette index, r, g, b
};

// (the macro format, two halves of 1024 bytes, is shared with the Ryos MK FX and lives in reports.cpp)

// factory layout out of roccat-tools (default_profile_buttons), type byte only, the parameters are all zero
constexpr uint8_t FACTORY[SLOTS] = {0x01, 0x02, 0x03, 0x64, 0xd0, 0x41, 0x65, 0x67, 0x66, 0x14, 0x0d, 0x0e,
                                    0x08, 0x07, 0x25, 0x21, 0x23, 0x00, 0x22, 0x62, 0x63, 0x10, 0x26, 0x27};

struct kova_profile final : rockcat_profile {
    uint8_t settings[SETTINGS_SIZE] = {};
    uint8_t buttons[BUTTONS_SIZE] = {};
    bool settings_dirty = false, buttons_dirty = false;
    std::vector<rockcat_macro> macros; // parked until the next write

    using rockcat_profile::rockcat_profile;

    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new kova_profile(*this); }

    int read(rockcat_device *dev) override
    {
        settings_dirty = buttons_dirty = false;
        macros.clear();
        int r = select_data(dev, static_cast<uint8_t>(index), SELECT_SETTINGS);
        if (r != ROCKCAT_OK)
            return r;
        std::memset(settings, 0, sizeof settings);
        settings[0] = REPORT_SETTINGS;
        r = get_raw(dev, settings, sizeof settings);
        if (r != ROCKCAT_OK)
            return r;
        if (settings[1] != SETTINGS_SIZE || settings[S_PROFILE] != index)
            return ROCKCAT_ERR_PROTOCOL;
        if (!checksum_ok(settings, sizeof settings))
            return ROCKCAT_ERR_CHECKSUM;

        r = select_data(dev, static_cast<uint8_t>(index), SELECT_BUTTONS);
        if (r != ROCKCAT_OK)
            return r;
        std::memset(buttons, 0, sizeof buttons);
        buttons[0] = REPORT_BUTTONS;
        r = get_raw(dev, buttons, sizeof buttons);
        if (r != ROCKCAT_OK)
            return r;
        // like on the Kone, this report has no checksum
        return buttons[1] == BUTTONS_SIZE && buttons[2] == index ? ROCKCAT_OK : ROCKCAT_ERR_PROTOCOL;
    }

    int write(rockcat_device *dev) override
    {
        if (settings_dirty) {
            put_checksum(settings, sizeof settings);
            int r = set_raw(dev, settings, sizeof settings, true);
            if (r != ROCKCAT_OK)
                return r;
            settings_dirty = false;
        }
        // macros go before the buttons that point at them
        while (!macros.empty()) {
            int r = macro_halves_write(dev, REPORT_MACRO, macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        if (buttons_dirty) {
            int r = set_raw(dev, buttons, sizeof buttons, true);
            if (r != ROCKCAT_OK)
                return r;
            buttons_dirty = false;
        }
        return ROCKCAT_OK;
    }

    int set_index(int i) override
    {
        if (i < 0 || i >= caps().profile_count)
            return ROCKCAT_ERR_ARGUMENT;
        index = i;
        settings[S_PROFILE] = buttons[2] = static_cast<uint8_t>(i);
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        settings_dirty = buttons_dirty = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        out.insert(out.end(), settings, settings + sizeof settings);
        out.insert(out.end(), buttons, buttons + sizeof buttons);
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != SETTINGS_SIZE + BUTTONS_SIZE || buf[0] != REPORT_SETTINGS || buf[1] != SETTINGS_SIZE || buf[SETTINGS_SIZE] != REPORT_BUTTONS)
            return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(settings, buf, sizeof settings);
        std::memcpy(buttons, buf + SETTINGS_SIZE, sizeof buttons);
        return set_index(index);
    }

    int get_dpi(int level, int *x, int *y, int *enabled) const override
    {
        if (level < 0 || level >= caps().dpi_levels)
            return ROCKCAT_ERR_ARGUMENT;
        int dpi = settings[S_DPI + level] * caps().dpi_step;
        if (x)
            *x = dpi;
        if (y)
            *y = dpi;
        if (enabled)
            *enabled = (settings[S_DPI_ENABLED] >> level) & 1;
        return ROCKCAT_OK;
    }

    int set_dpi(int level, int x, int y, int enabled) override
    {
        const rockcat_caps &c = caps();
        // one sensor, one number. asking for different x and y is asking for something this mouse cant do
        if (level < 0 || level >= c.dpi_levels || x != y || x < c.dpi_min || x > c.dpi_max)
            return ROCKCAT_ERR_ARGUMENT;
        settings[S_DPI + level] = static_cast<uint8_t>(x / c.dpi_step);
        settings[S_DPI_ENABLED] = static_cast<uint8_t>(enabled ? settings[S_DPI_ENABLED] | (1 << level) : settings[S_DPI_ENABLED] & ~(1 << level));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:   *value = settings[S_DPI_ACTIVE]; break;
        case ROCKCAT_SET_POLLING_HZ:   *value = rockcat_polling_rate_hz(settings[S_POLLING]); break;
        case ROCKCAT_SET_EFFECT:       *value = settings[S_EFFECT]; break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = settings[S_SPEED]; break;
        case ROCKCAT_SET_COLOR_FLOW:   *value = settings[S_FLOW] ? 1 : 0; break;
        case ROCKCAT_SET_LEFT_HANDED:  *value = settings[S_HANDED] == 0xfe; break;
        default:                       return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        const rockcat_caps &c = caps();
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:
            if (value < 0 || value >= c.dpi_levels)
                return ROCKCAT_ERR_ARGUMENT;
            settings[S_DPI_ACTIVE] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_POLLING_HZ: {
            int rate = -1;
            for (int i = 0; i < 4; ++i)
                if (rockcat_polling_rate_hz(static_cast<uint8_t>(i)) == value)
                    rate = i;
            if (rate < 0)
                return ROCKCAT_ERR_ARGUMENT;
            settings[S_POLLING] = static_cast<uint8_t>(rate);
            break;
        }
        case ROCKCAT_SET_EFFECT:
            if (value < 0 || value > 31 || !(c.effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            // the mouse knows 1 to 3. an effect the pc draws has no number here, whoever asked stores the carrier instead
            if (value > 3)
                return ROCKCAT_ERR_UNSUPPORTED;
            settings[S_EFFECT] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT_SPEED:
            // the vendor tools offer 1 to 3, a real mouse had 0 stored in an untouched looking profile, so 0 gets through as well
            if (value < 0 || value > 3)
                return ROCKCAT_ERR_ARGUMENT;
            settings[S_SPEED] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_COLOR_FLOW:
            settings[S_FLOW] = value ? 1 : 0;
            break;
        case ROCKCAT_SET_LEFT_HANDED:
            settings[S_HANDED] = value ? 0xfe : 0xff;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= LEDS)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = settings + S_LIGHT + 4 * led;
        if (c)
            *c = rockcat_color{255, l[1], l[2], l[3]};
        if (enabled)
            *enabled = (settings[S_LIGHTS] >> led) & 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= LEDS)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *l = settings + S_LIGHT + 4 * led;
        // no brightness per light on this one, so whatever alpha says gets folded into the colour
        l[1] = static_cast<uint8_t>((c.r * c.a + 127) / 255);
        l[2] = static_cast<uint8_t>((c.g * c.a + 127) / 255);
        l[3] = static_cast<uint8_t>((c.b * c.a + 127) / 255);
        uint8_t bits = settings[S_LIGHTS];
        // the "custom colour" bit makes the firmware use our rgb and not look up the palette index in front of it
        bits = static_cast<uint8_t>(bits | (0x10 << led));
        bits = static_cast<uint8_t>(enabled ? bits | (1 << led) : bits & ~(1 << led));
        settings[S_LIGHTS] = bits;
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int slot(int layer, int input) const
    {
        return layer < 0 || layer >= caps().layer_count || input < 0 || input >= caps().input_count ? -1 : layer * INPUTS + input;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *p = buttons + 3 + 3 * i;
        *a = rockcat_assignment{p[0], p[1], p[2]};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *p = buttons + 3 + 3 * i;
        p[0] = a->type;
        p[1] = a->param1;
        p[2] = a->param2;
        buttons_dirty = true;
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        std::memset(buttons + 3, 0, sizeof buttons - 3);
        for (int i = 0; i < SLOTS; ++i)
            buttons[3 + 3 * i] = FACTORY[i];
        buttons_dirty = true;
        return ROCKCAT_OK;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0)
            return ROCKCAT_ERR_ARGUMENT;
        *a = rockcat_assignment{FACTORY[i], 0, 0};
        return ROCKCAT_OK;
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !m)
            return ROCKCAT_ERR_ARGUMENT;
        return macro_halves_read(dev, REPORT_MACRO, index, i, m);
    }

    int set_macro(int layer, int input, const rockcat_macro *m) override
    {
        int i = slot(layer, input);
        if (i < 0 || !m || m->count > ROCKCAT_MACRO_KEYSTROKES)
            return ROCKCAT_ERR_ARGUMENT;
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
    return new kova_profile(desc, index);
}

// info report is [09 08 firmware ...], 0x76 = 118 = what the vendor calls 1.18
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[8] = {REPORT_INFO};
    int r = get_raw(dev, info, sizeof info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

// lighting from the pc goes through the report ROCCAT made for "Talk FX" (10), laid out the way roccat-tools does its
// "gfx" colours: [10 10 ff ff ff 01 03 01 01 02 r g b r g b]. the three ff are "leave Easy-Shift and Easy-Aim alone"
// (roccat-tools sends 0 there), 01 03 01 01 02 is what roccat-tools always sends for a colour. one colour for both
// lights. all zero after the three ff hands the lights back to the profile. like every other write to this mouse it
// gets its status looked at afterwards (roccat-tools waits too), without that the mouse can end up dark
constexpr uint8_t REPORT_TALK = 0x10;

int talk(rockcat_device *dev, bool on, rockcat_color c)
{
    const uint8_t rep[16] = {REPORT_TALK, 0x10, 0xff, 0xff, 0xff,
                             static_cast<uint8_t>(on ? 1 : 0), static_cast<uint8_t>(on ? 3 : 0), static_cast<uint8_t>(on ? 1 : 0),
                             static_cast<uint8_t>(on ? 1 : 0), static_cast<uint8_t>(on ? 2 : 0),
                             c.r, c.g, c.b, c.r, c.g, c.b};
    int r = set_raw(dev, rep, sizeof rep, false);
    return r == ROCKCAT_OK ? wait_ready_after(dev, 0) : r;
}

int direct_begin(rockcat_device *)
{
    // nothing to announce, the first colour that gets sent takes the lights
    return ROCKCAT_OK;
}

int direct_frame(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!colors || count != LEDS)
        return ROCKCAT_ERR_ARGUMENT;
    // both lights can only be the same colour this way, the first one speaks for both. no brightness, so alpha goes in
    const rockcat_color &c = colors[0];
    return talk(dev, true, rockcat_color{255, static_cast<uint8_t>((c.r * c.a + 127) / 255), static_cast<uint8_t>((c.g * c.a + 127) / 255),
                                          static_cast<uint8_t>((c.b * c.a + 127) / 255)});
}

int direct_end(rockcat_device *dev)
{
    return talk(dev, false, rockcat_color{0, 0, 0, 0});
}

int direct_switch(rockcat_device *dev, bool host)
{
    return host ? ROCKCAT_OK : direct_end(dev);
}

int direct_active(rockcat_device *)
{
    return 1;
}

bool decode_event(const uint8_t *in, int n, rockcat_event *ev)
{
    // only the plain mouse report so far. holding Easy-Shift sends nothing on this node, a real mouse was watched for that.
    // the one 03 report that did show up while trying, [03 00 e5 0a 00], came once and nobody knows what set it off
    if (in[0] != 0x01 || n < 2)
        return false;
    *ev = rockcat_event{ROCKCAT_EVENT_ACTIVITY, in[1], 0};
    return true;
}

const char *input_label(int input)
{
    static const char *names[INPUTS] = {"Left button",   "Right button",       "Middle button",  "Left Smart Cast",
                                        "Left forward",  "Left backward",      "Right Smart Cast", "Right forward",
                                        "Right backward", "Top button",        "Wheel up",       "Wheel down"};
    return input >= 0 && input < INPUTS ? names[input] : "?";
}

const char *led_label(int led)
{
    return led == 0 ? "Light pipe" : led == 1 ? "Wheel" : "?";
}

const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : "?";
}

} // namespace

// the profile report (05) is byte for byte the Kone AIMO one, so those two calls are simply shared
const family_ops KOVA_FAMILY = {firmware,     kone_get_active_profile, kone_set_active_profile, new_profile,   direct_begin,
                                direct_frame, direct_end,              direct_switch,           direct_active, decode_event,
                                input_label,  led_label,               layer_label};
