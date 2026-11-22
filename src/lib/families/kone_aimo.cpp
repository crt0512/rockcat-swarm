// Kone AIMO family (only the Remastered so far): a profile is the settings report 0x06 plus the buttons report 0x07
// for now this sits on top of the old struct api in rockcat.cpp, which keeps the bytes on the wire exactly as they were.
// the report code moves in here for real once nothing uses the old api anymore

#include "../internal.h"

#include <cstring>

namespace {

constexpr int INPUTS = ROCKCAT_BUTTON_COUNT / 2;

struct kone_profile final : rockcat_profile {
    rockcat_settings s{};
    rockcat_buttons b{};
    bool settings_dirty = false, buttons_dirty = false;
    std::vector<rockcat_macro> macros; // parked until the next write

    using rockcat_profile::rockcat_profile;

    const rockcat_caps &caps() const { return desc->caps; }

    int read(rockcat_device *dev) override
    {
        int r = rockcat_read_settings(dev, static_cast<uint8_t>(index), &s);
        if (r == ROCKCAT_OK)
            r = rockcat_read_buttons(dev, static_cast<uint8_t>(index), &b);
        settings_dirty = buttons_dirty = false;
        macros.clear();
        return r;
    }

    int write(rockcat_device *dev) override
    {
        if (settings_dirty) {
            int r = rockcat_write_settings(dev, &s);
            if (r != ROCKCAT_OK)
                return r;
            settings_dirty = false;
        }
        if (buttons_dirty) {
            int r = rockcat_write_buttons(dev, &b);
            if (r != ROCKCAT_OK)
                return r;
            buttons_dirty = false;
        }
        while (!macros.empty()) {
            int r = rockcat_write_macro(dev, &macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        return ROCKCAT_OK;
    }

    int set_index(int i) override
    {
        if (i < 0 || i >= caps().profile_count)
            return ROCKCAT_ERR_ARGUMENT;
        index = i;
        s.profile = b.profile = static_cast<uint8_t>(i);
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        settings_dirty = buttons_dirty = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        uint8_t raw[ROCKCAT_SETTINGS_SIZE];
        rockcat_settings_encode(&s, raw);
        out.insert(out.end(), raw, raw + sizeof raw);
        for (const rockcat_button &bt : b.buttons)
            out.insert(out.end(), {bt.type, bt.param1, bt.param2});
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != ROCKCAT_SETTINGS_SIZE + 3 * ROCKCAT_BUTTON_COUNT)
            return ROCKCAT_ERR_ARGUMENT;
        rockcat_settings_decode(buf, &s);
        for (int i = 0; i < ROCKCAT_BUTTON_COUNT; ++i) {
            const uint8_t *p = buf + ROCKCAT_SETTINGS_SIZE + 3 * i;
            b.buttons[i] = rockcat_button{p[0], p[1], p[2]};
        }
        return set_index(index);
    }

    int get_dpi(int level, int *x, int *y, int *enabled) const override
    {
        if (level < 0 || level >= caps().dpi_levels)
            return ROCKCAT_ERR_ARGUMENT;
        if (x)
            *x = s.dpi_x[level];
        if (y)
            *y = s.dpi_y[level];
        if (enabled)
            *enabled = (s.dpi_enabled_mask >> level) & 1;
        return ROCKCAT_OK;
    }

    int set_dpi(int level, int x, int y, int enabled) override
    {
        const rockcat_caps &c = caps();
        if (level < 0 || level >= c.dpi_levels || x < c.dpi_min || x > c.dpi_max || y < c.dpi_min || y > c.dpi_max)
            return ROCKCAT_ERR_ARGUMENT;
        s.dpi_x[level] = static_cast<uint16_t>(x - x % c.dpi_step);
        s.dpi_y[level] = static_cast<uint16_t>(y - y % c.dpi_step);
        s.dpi_enabled_mask = static_cast<uint8_t>(enabled ? s.dpi_enabled_mask | (1 << level) : s.dpi_enabled_mask & ~(1 << level));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:     *value = s.dpi_active; break;
        case ROCKCAT_SET_POLLING_HZ:     *value = rockcat_polling_rate_hz(s.polling_rate); break;
        case ROCKCAT_SET_ANGLE_SNAPPING: *value = s.angle_snapping ? 1 : 0; break;
        case ROCKCAT_SET_DEBOUNCE_MS:    *value = s.debounce_ms; break;
        case ROCKCAT_SET_EFFECT:         *value = s.effect; break;
        case ROCKCAT_SET_EFFECT_SPEED:   *value = s.effect_speed; break;
        case ROCKCAT_SET_BRIGHTNESS:     *value = s.brightness; break;
        // bit 7 is a flag the factory profiles all have set, the theme is whats below it
        case ROCKCAT_SET_COLOR_THEME:    *value = s.color_theme & 0x7f; break;
        default:                         return ROCKCAT_ERR_UNSUPPORTED;
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
            s.dpi_active = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_POLLING_HZ: {
            int rate = -1;
            for (int i = 0; i <= ROCKCAT_POLLING_1000HZ; ++i)
                if (rockcat_polling_rate_hz(static_cast<uint8_t>(i)) == value)
                    rate = i;
            if (rate < 0)
                return ROCKCAT_ERR_ARGUMENT;
            s.polling_rate = static_cast<uint8_t>(rate);
            break;
        }
        case ROCKCAT_SET_ANGLE_SNAPPING:
            s.angle_snapping = value ? 1 : 0;
            break;
        case ROCKCAT_SET_DEBOUNCE_MS:
        case ROCKCAT_SET_EFFECT_SPEED:
        case ROCKCAT_SET_BRIGHTNESS:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            (which == ROCKCAT_SET_DEBOUNCE_MS ? s.debounce_ms : which == ROCKCAT_SET_EFFECT_SPEED ? s.effect_speed : s.brightness) =
                static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT:
            if (value < 0 || value > 31 || !(c.effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            s.effect = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_COLOR_THEME:
            if (value < 0 || value >= ROCKCAT_AIMO_THEMES)
                return ROCKCAT_ERR_ARGUMENT;
            s.color_theme = static_cast<uint8_t>(0x80 | value);
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= caps().led_count)
            return ROCKCAT_ERR_ARGUMENT;
        if (c)
            *c = s.leds[led];
        if (enabled)
            *enabled = (s.led_enable_mask >> led) & 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= caps().led_count)
            return ROCKCAT_ERR_ARGUMENT;
        s.leds[led] = c;
        s.led_enable_mask = static_cast<uint16_t>(enabled ? s.led_enable_mask | (1 << led) : s.led_enable_mask & ~(1 << led));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    // the mouse stores both layers in one table, Easy-Shift[+] is simply the second half
    int slot(int layer, int input) const
    {
        return layer < 0 || layer >= caps().layer_count || input < 0 || input >= caps().input_count ? -1 : layer * INPUTS + input;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        *a = b.buttons[i];
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        b.buttons[i] = *a;
        buttons_dirty = true;
        return ROCKCAT_OK;
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !m)
            return ROCKCAT_ERR_ARGUMENT;
        return rockcat_read_macro(dev, static_cast<uint8_t>(index), static_cast<uint8_t>(i), m);
    }

    int set_macro(int layer, int input, const rockcat_macro *m) override
    {
        int i = slot(layer, input);
        if (i < 0 || !m || m->count > ROCKCAT_MACRO_KEYSTROKES || m->mode > ROCKCAT_MACRO_INFINITE)
            return ROCKCAT_ERR_ARGUMENT;
        rockcat_macro copy = *m;
        copy.profile = static_cast<uint8_t>(index);
        copy.button = static_cast<uint8_t>(i);
        // setting the same slot twice before a write should send it once, with what was set last
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
    return new kone_profile(desc, index);
}

int direct_begin(rockcat_device *dev)
{
    return rockcat_set_direct_lighting(dev, 1);
}

int direct_frame(rockcat_device *dev, const rockcat_color *colors, int count)
{
    return count == ROCKCAT_LED_COUNT ? rockcat_set_direct_colors(dev, colors) : ROCKCAT_ERR_ARGUMENT;
}

int direct_end(rockcat_device *dev)
{
    return rockcat_set_direct_lighting(dev, 0);
}

const char *input_label(int input)
{
    return input >= 0 && input < INPUTS ? rockcat_button_name(input) : "?";
}

const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : "?";
}

} // namespace

const family_ops KONE_AIMO_FAMILY = {new_profile, direct_begin, direct_frame, direct_end, input_label, rockcat_led_name, layer_label};
