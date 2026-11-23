// the original Kone AIMO (2e27, 2018). talks exactly like the Remastered, only the settings report is shorter: 0x69 bytes with one byte
// per dpi level instead of two words. Swarm drives both with one class (kone_aimo_base_device_c in ROCCAT_Swarm_Monitor.exe), so buttons,
// macros, direct lighting and the rest are borrowed from the Remastered family as they are. docs/protocol/kone-aimo-original.md

#include "../internal.h"
#include "../sim_kone.h"

#include <cstring>

namespace {

constexpr uint8_t REPORT_SETTINGS = 0x06, SELECT_SETTINGS = 0x80;
constexpr size_t SIZE = 0x69;
constexpr int INPUTS = ROCKCAT_BUTTON_COUNT / 2, LEDS = ROCKCAT_LED_COUNT;

// the Remastered offsets minus the 15 bytes the dpi words took. every one of these is a pair in KONE_AIMO.dll v1.0135, where the
// same setter writes either 0x7e + x or 0x69 + y depending on the model (FUN_6a412d40, FUN_6a41e890, FUN_6a450fe0, ...)
enum : size_t {
    S_PROFILE = 2,
    S_DPI_ENABLED = 4, // bit n = level n
    S_DPI = 5,         // 5 levels, one byte each, times 50
    S_DPI_ACTIVE = 10,
    S_POLLING = 0x0b,
    S_ANGLE_SNAP = 0x0c,
    S_DEBOUNCE = 0x0e,
    S_LED_ENABLE = 0x0f, // 16 bit, bit n = led n. this one is not a pair in the module, it follows from the others (guess)
    S_EFFECT = 0x12,
    S_SPEED = 0x13,
    S_BRIGHTNESS = 0x14,
    S_LEDS = 0x2e, // 11 x [a r g b amber]
    S_THEME = 0x65,
};

struct original_profile final : rockcat_profile {
    uint8_t s[SIZE] = {};
    rockcat_buttons b{};
    bool settings_dirty = false, buttons_dirty = false;
    std::vector<rockcat_macro> macros;

    using rockcat_profile::rockcat_profile;

    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new original_profile(*this); }

    int read_settings(rockcat_device *dev)
    {
        int r = select_data(dev, static_cast<uint8_t>(index), SELECT_SETTINGS);
        if (r != ROCKCAT_OK)
            return r;
        std::memset(s, 0, sizeof s);
        s[0] = REPORT_SETTINGS;
        r = get_raw(dev, s, sizeof s);
        if (r != ROCKCAT_OK)
            return r;
        if (s[1] != SIZE || s[S_PROFILE] != index)
            return ROCKCAT_ERR_PROTOCOL;
        return checksum_ok(s, sizeof s) ? ROCKCAT_OK : ROCKCAT_ERR_CHECKSUM;
    }

    int read_light(rockcat_device *dev) override
    {
        settings_dirty = buttons_dirty = false;
        macros.clear();
        return read_settings(dev);
    }

    int read(rockcat_device *dev) override
    {
        int r = read_light(dev);
        return r == ROCKCAT_OK ? rockcat_read_buttons(dev, static_cast<uint8_t>(index), &b) : r;
    }

    int write(rockcat_device *dev) override
    {
        if (settings_dirty) {
            put_checksum(s, sizeof s);
            int r = set_raw(dev, s, sizeof s, true);
            if (r != ROCKCAT_OK)
                return r;
            settings_dirty = false;
        }
        while (!macros.empty()) {
            int r = rockcat_write_macro(dev, &macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        if (buttons_dirty) {
            int r = rockcat_write_buttons(dev, &b);
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
        s[S_PROFILE] = b.profile = static_cast<uint8_t>(i);
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        settings_dirty = buttons_dirty = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        out.insert(out.end(), s, s + sizeof s);
        for (const rockcat_button &bt : b.buttons)
            out.insert(out.end(), {bt.type, bt.param1, bt.param2});
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        if (len != SIZE + 3 * ROCKCAT_BUTTON_COUNT || buf[0] != REPORT_SETTINGS || buf[1] != SIZE)
            return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(s, buf, SIZE);
        for (int i = 0; i < ROCKCAT_BUTTON_COUNT; ++i) {
            const uint8_t *p = buf + SIZE + 3 * i;
            b.buttons[i] = rockcat_button{p[0], p[1], p[2]};
        }
        return set_index(index);
    }

    int get_dpi(int level, int *x, int *y, int *enabled) const override
    {
        if (level < 0 || level >= caps().dpi_levels)
            return ROCKCAT_ERR_ARGUMENT;
        int dpi = s[S_DPI + level] * caps().dpi_step;
        if (x)
            *x = dpi;
        if (y)
            *y = dpi;
        if (enabled)
            *enabled = (s[S_DPI_ENABLED] >> level) & 1;
        return ROCKCAT_OK;
    }

    int set_dpi(int level, int x, int y, int enabled) override
    {
        const rockcat_caps &c = caps();
        if (level < 0 || level >= c.dpi_levels || x != y || x < c.dpi_min || x > c.dpi_max)
            return ROCKCAT_ERR_ARGUMENT;
        s[S_DPI + level] = static_cast<uint8_t>(x / c.dpi_step);
        s[S_DPI_ENABLED] = static_cast<uint8_t>(enabled ? s[S_DPI_ENABLED] | (1 << level) : s[S_DPI_ENABLED] & ~(1 << level));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:     *value = s[S_DPI_ACTIVE]; break;
        case ROCKCAT_SET_POLLING_HZ:     *value = rockcat_polling_rate_hz(s[S_POLLING]); break;
        case ROCKCAT_SET_ANGLE_SNAPPING: *value = s[S_ANGLE_SNAP] ? 1 : 0; break;
        case ROCKCAT_SET_DEBOUNCE_MS:    *value = s[S_DEBOUNCE]; break;
        case ROCKCAT_SET_EFFECT:         *value = s[S_EFFECT]; break;
        case ROCKCAT_SET_EFFECT_SPEED:   *value = s[S_SPEED]; break;
        case ROCKCAT_SET_BRIGHTNESS:     *value = s[S_BRIGHTNESS]; break;
        case ROCKCAT_SET_COLOR_THEME:    *value = s[S_THEME] & 0x7f; break;
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
            s[S_DPI_ACTIVE] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_POLLING_HZ: {
            int rate = -1;
            for (int i = 0; i <= ROCKCAT_POLLING_1000HZ; ++i)
                if (rockcat_polling_rate_hz(static_cast<uint8_t>(i)) == value)
                    rate = i;
            if (rate < 0)
                return ROCKCAT_ERR_ARGUMENT;
            s[S_POLLING] = static_cast<uint8_t>(rate);
            break;
        }
        case ROCKCAT_SET_ANGLE_SNAPPING:
            s[S_ANGLE_SNAP] = value ? 1 : 0;
            break;
        case ROCKCAT_SET_DEBOUNCE_MS:
        case ROCKCAT_SET_EFFECT_SPEED:
        case ROCKCAT_SET_BRIGHTNESS:
            if (value < 0 || value > 255)
                return ROCKCAT_ERR_ARGUMENT;
            s[which == ROCKCAT_SET_DEBOUNCE_MS ? S_DEBOUNCE : which == ROCKCAT_SET_EFFECT_SPEED ? S_SPEED : S_BRIGHTNESS] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_EFFECT:
            if (value < 0 || value > 31 || !(c.effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            s[S_EFFECT] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_COLOR_THEME:
            if (value < 0 || value >= ROCKCAT_AIMO_THEMES)
                return ROCKCAT_ERR_ARGUMENT;
            s[S_THEME] = static_cast<uint8_t>(0x80 | value);
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
        const uint8_t *p = s + S_LEDS + 5 * led;
        if (c)
            *c = rockcat_color{p[0], p[1], p[2], p[3]};
        if (enabled)
            *enabled = (get16(s + S_LED_ENABLE) >> led) & 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= LEDS)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *p = s + S_LEDS + 5 * led;
        // the fifth byte (amber) is left alone, the firmware mixes its own white into it anyway
        p[0] = c.a;
        p[1] = c.r;
        p[2] = c.g;
        p[3] = c.b;
        uint16_t mask = get16(s + S_LED_ENABLE);
        put16(s + S_LED_ENABLE, static_cast<uint16_t>(enabled ? mask | (1 << led) : mask & ~(1 << led)));
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

    int reset_inputs() override
    {
        // same shell, same buttons, same factory layout as the Remastered (KONE_AIMO.dll has one table for both)
        rockcat_buttons_default(static_cast<uint8_t>(index), &b);
        buttons_dirty = true;
        return ROCKCAT_OK;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        rockcat_buttons f;
        rockcat_buttons_default(static_cast<uint8_t>(index), &f);
        *a = f.buttons[i];
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
    return new original_profile(desc, index);
}

// everything but the profile object is the Remastered one. copied at startup, the other family is constant initialised by then
family_ops make_ops()
{
    family_ops f = KONE_AIMO_FAMILY;
    f.new_profile = new_profile;
    return f;
}
const family_ops ORIGINAL_FAMILY = make_ops();

constexpr uint32_t bit(int n) { return 1u << n; }

const device_desc DEVICES[] = {
    {0x2e27, "ROCCAT Kone AIMO", &ORIGINAL_FAMILY, KONE_AIMO_LEDS, 0x06,
     {ROCKCAT_KIND_MOUSE, "kone-aimo-original", ROCKCAT_TESTED_NO, "",
      ROCKCAT_HAS_DPI | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_DIRECT_LIGHT | ROCKCAT_HAS_LED_BRIGHTNESS |
          ROCKCAT_HAS_RGB | ROCKCAT_HAS_DEBOUNCE | ROCKCAT_HAS_ANGLE_SNAP,
      // dpi: the module caps the spin box at 12000 for this one and 16000 for the Remastered, one byte x 50 couldnt go past 12750 anyway
      5, 12, 2, 11, 5, 100, 12000, 50, {125, 250, 500, 1000, 0},
      bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) | bit(ROCKCAT_EFFECT_BREATHING) |
          bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_COLOR_CYCLE) | bit(ROCKCAT_EFFECT_SNAKE) | bit(ROCKCAT_EFFECT_BREATHING_2) |
          bit(ROCKCAT_EFFECT_HEARTBEAT_2) | bit(ROCKCAT_EFFECT_AIMO) | bit(ROCKCAT_EFFECT_COLORWAVE),
      // which ones the firmware draws by itself was only ever looked at on the Remastered, taken over as a guess
      bit(ROCKCAT_EFFECT_HEARTBEAT) | bit(ROCKCAT_EFFECT_BREATHING_2) | bit(ROCKCAT_EFFECT_HEARTBEAT_2) | bit(ROCKCAT_EFFECT_AIMO),
      1, 11, ROCKCAT_EFFECT_AIMO}},
};

const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// the pretend one: a pretend Remastered with the settings report squeezed into the short layout on the way out and blown up again
// on the way in, which is also a check that the two layouts line up the way the doc says
struct original_sim final : transport {
    // through the base class on purpose, gcc inlines kone_sim otherwise and then sees ghosts in its macro branch
    std::unique_ptr<transport> door{new kone_sim};
    kone_sim &inner = static_cast<kone_sim &>(*door);

    static void squeeze(const uint8_t *big, uint8_t *small)
    {
        std::memset(small, 0, SIZE);
        std::memcpy(small, big, 5);
        small[1] = SIZE;
        for (int i = 0; i < 5; ++i)
            small[S_DPI + i] = static_cast<uint8_t>(get16(big + 6 + 2 * i));
        small[S_DPI_ACTIVE] = big[5];
        std::memcpy(small + 0x0b, big + 0x1a, 0x74 - 0x1a + 2); // polling up to and including the byte after the theme
        put_checksum(small, SIZE);
    }

    static void blow_up(const uint8_t *small, uint8_t *big)
    {
        std::memcpy(big, small, 5);
        big[1] = ROCKCAT_SETTINGS_SIZE;
        big[5] = small[S_DPI_ACTIVE];
        for (int i = 0; i < 5; ++i) {
            put16(big + 6 + 2 * i, small[S_DPI + i]);
            put16(big + 0x10 + 2 * i, small[S_DPI + i]);
        }
        std::memcpy(big + 0x1a, small + 0x0b, 0x74 - 0x1a + 2);
        put_checksum(big, ROCKCAT_SETTINGS_SIZE);
    }

    original_sim()
    {
        // the pretend Remastered has dpi values past 12000 in its last levels, this mouse cant hold them
        for (int p = 0; p < 5; ++p)
            for (int i = 0; i < 5; ++i) {
                uint16_t v = get16(inner.settings[p] + 6 + 2 * i);
                if (v > 240)
                    put16(inner.settings[p] + 6 + 2 * i, 240);
            }
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        if (buf[0] != REPORT_SETTINGS)
            return door->get_feature(buf, len);
        uint8_t big[ROCKCAT_SETTINGS_SIZE] = {REPORT_SETTINGS};
        int r = door->get_feature(big, sizeof big);
        if (r < 0)
            return r;
        uint8_t small[SIZE];
        squeeze(big, small);
        size_t n = len < SIZE ? len : SIZE;
        std::memcpy(buf, small, n);
        return static_cast<int>(n);
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        if (buf[0] != REPORT_SETTINGS)
            return door->set_feature(buf, len);
        if (len != SIZE || !checksum_ok(buf, SIZE)) {
            inner.status_queue = {2};
            return ROCKCAT_OK;
        }
        // the bytes only the long layout has stay what the pretend Remastered had there
        std::vector<uint8_t> big(inner.settings[buf[2] < 5 ? buf[2] : 0], inner.settings[buf[2] < 5 ? buf[2] : 0] + ROCKCAT_SETTINGS_SIZE);
        blow_up(buf, big.data());
        return door->set_feature(big.data(), big.size());
    }

    int read(uint8_t *buf, size_t len, int timeout_ms) override { return door->read(buf, len, timeout_ms); }
    void sleep_ms(int) override {}
};

const sim_registration sim({"kone-aimo", 0x2e27, "ROCCAT Kone AIMO", 0, [] { return std::unique_ptr<transport>(new original_sim); }});

} // namespace
