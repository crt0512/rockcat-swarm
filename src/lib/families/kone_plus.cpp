// the Kone[+] and the mice that grew out of it: Kone XTD, Kone XTD Optical, Kone Pure, Kone Pure Optical, Kone Pure Military
// and Kone Pure Optical Black. all of them talk the same way (select on 04, settings 06, buttons 07, one big macro report 08,
// info 09, Talk FX 10), what differs is the settings report and how many buttons there are. docs/protocol/kone-plus.md has it all
// every number in here is from roccat-tools 5.9.1 (koneplus, konextd, konextdoptical, konepure*), nobody here has held one of these

#include "../internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint8_t REPORT_PROFILE = 0x05, REPORT_SETTINGS = 0x06, REPORT_BUTTONS = 0x07, REPORT_MACRO = 0x08, REPORT_INFO = 0x09,
                  REPORT_TALK = 0x10;
constexpr uint8_t SELECT_SETTINGS = 0x80, SELECT_BUTTONS = 0x90;
constexpr int PROFILES = 5, DPI_LEVELS = 5;

// the macro report, same on every one of them: [08 size16 profile button loop] 24 unknown bytes, 24 set name, 24 name, count16,
// 500 keystrokes of 4 bytes, checksum16
enum : size_t { M_SIZE = 0x822, M_PROFILE = 3, M_BUTTON = 4, M_LOOP = 5, M_SET_NAME = 30, M_NAME = 54, M_COUNT = 78, M_KEYS = 80,
                M_KEYSTROKES = 500 };
static_assert(M_KEYS + 4 * M_KEYSTROKES + 2 == M_SIZE, "macro report adds up");

// how the effect byte is numbered. the 4 light ones go 0 off, 1 fully lit, 2 blinking, 3 breathing, 4 heartbeat (same as
// rockcat_effect, nice), the one light Pures only have 0 off, 1 fully lit, 2 breathing
enum effect_style { EFFECTS_FOUR_LIGHTS, EFFECTS_PURE };
// what the pc can do with the lights: one colour for all through the Talk FX report, or the Kone XTD's own "gfx" mode with a
// colour per light
enum direct_style { DIRECT_TALK, DIRECT_XTD };

// one settings layout. -1 = this mouse doesnt have that byte
struct variant {
    uint16_t pid_any; // only for finding it again, the first pid that uses it
    size_t settings_size;
    bool settings_checksum; // the Military (and the Optical Black) has none, the settings report is 31 bytes flat
    int inputs;             // per layer
    int leds;
    int o_cpi_x, o_cpi_y, o_startup, o_polling, o_lights_on, o_custom, o_flow, o_effect, o_speed, o_light;
    int o_advanced, o_advanced2, o_snap, o_align, o_talkfx;
    int dpi_unit;         // what one step of the cpi bytes is worth
    bool cpi_table;       // Kone Pure Optical: the bytes are codes out of a table, not a number
    bool blue_green;      // Military: the light is stored index, red, blue, green. no idea why, roccat-tools says so
    bool palette_only;    // Kone[+]: roccat-tools only ever writes colours out of the 33 colour table
    effect_style effects;
    direct_style direct;
    const uint8_t *factory; // type byte of every input, normal layer then Easy-Shift
};

// factory layout out of the roccat-tools default rmp files, type byte only, parameters all zero
// order: left, right, middle, forward, backward, tilt left, tilt right, wheel up, wheel down, plus, minus, top
constexpr uint8_t FACTORY_12[24] = {0x01, 0x02, 0x03, 0x07, 0x41, 0x09, 0x0a, 0x0d, 0x0e, 0x15, 0x16, 0x1a,
                                    0x08, 0x07, 0x25, 0x06, 0x00, 0x21, 0x22, 0x26, 0x27, 0x11, 0x12, 0x1b};
// the Pures have no tilt and no top button, the same defaults with those taken out
constexpr uint8_t FACTORY_9[18] = {0x01, 0x02, 0x03, 0x07, 0x41, 0x0d, 0x0e, 0x15, 0x16,
                                   0x08, 0x07, 0x25, 0x06, 0x00, 0x26, 0x27, 0x11, 0x12};

//                                  pid   size chk  in leds cpix cpiy st  poll on  cust flow eff spd light adv adv2 snap algn tfx unit table  bgr    palette
const variant KONE_PLUS_V        = {0x2d51, 43, true, 12, 4, 7, 13, 12, 19, 20, -1, 22, 23, 24, 25, -1, -1, -1, -1, -1, 100, false, false, true,
                                    EFFECTS_FOUR_LIGHTS, DIRECT_TALK, FACTORY_12};
const variant KONE_XTD_V         = {0x2e22, 43, true, 12, 4, 7, 13, 12, 19, 20, -1, 22, 23, 24, 25, -1, -1, -1, -1, 18, 50, false, false, false,
                                    EFFECTS_FOUR_LIGHTS, DIRECT_XTD, FACTORY_12};
// cpi x and y swapped places compared to the others (y first, startup in between, x after). roccat-tools writes the same into both
const variant KONE_XTD_OPTICAL_V = {0x2e23, 46, true, 12, 4, 13, 7, 12, 19, 20, -1, 22, 23, 24, 25, 18, 41, 42, 43, -1, 50, false, false, false,
                                    EFFECTS_FOUR_LIGHTS, DIRECT_TALK, FACTORY_12};
const variant KONE_PURE_V        = {0x2dbe, 31, true, 9, 1, 7, 13, 12, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, 50, false, false, false,
                                    EFFECTS_PURE, DIRECT_TALK, FACTORY_9};
// five cpi codes, five bytes nobody uses, and only then the startup level
const variant KONE_PURE_OPTICAL_V = {0x2db4, 31, true, 9, 1, 7, -1, 17, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, 0, true, false, false,
                                     EFFECTS_PURE, DIRECT_TALK, FACTORY_9};
const variant KONE_PURE_MILITARY_V = {0x2dbf, 31, false, 9, 1, 7, 13, 12, 19, 20, 21, 22, 23, 24, 25, 18, -1, 29, 30, -1, 50, false, true, false,
                                      EFFECTS_PURE, DIRECT_TALK, FACTORY_9};

// Kone Pure Optical cpi codes, in the order of the dpi they stand for
constexpr int PURE_OPTICAL_DPI[7] = {400, 800, 1200, 1600, 2000, 3200, 4000};
constexpr uint8_t PURE_OPTICAL_CODE[7] = {0x10, 0x00, 0x11, 0x12, 0x13, 0x02, 0x03};

// the 33 colours the Kone[+] knows by index (koneplus_rmp_light_info.c)
constexpr uint8_t PALETTE[33][3] = {
    {0x64, 0x00, 0x00}, {0xfa, 0x00, 0x00}, {0xfa, 0x0f, 0x00}, {0xfa, 0x19, 0x00}, {0xfa, 0x28, 0x00}, {0xfa, 0x3c, 0x00},
    {0xfa, 0x50, 0x00}, {0xfa, 0x64, 0x00}, {0xfa, 0x7d, 0x00}, {0xfa, 0xaf, 0x00}, {0xfa, 0xfa, 0x00}, {0x7d, 0xfa, 0x00},
    {0x00, 0xfa, 0x00}, {0x00, 0x32, 0x00}, {0x00, 0xfa, 0x32}, {0x00, 0xfa, 0x96}, {0x00, 0xfa, 0xfa}, {0xc8, 0xfa, 0xfa},
    {0x00, 0x96, 0xfa}, {0x00, 0x64, 0xfa}, {0x64, 0x64, 0xfa}, {0x00, 0x32, 0xfa}, {0x00, 0x19, 0xfa}, {0x00, 0x00, 0xfa},
    {0x00, 0x00, 0x7d}, {0x23, 0x00, 0xfa}, {0x64, 0x00, 0xfa}, {0xfa, 0x00, 0xfa}, {0x9b, 0x32, 0xfa}, {0x9b, 0x00, 0x64},
    {0xfa, 0x00, 0x64}, {0xfa, 0x00, 0x32}, {0xfa, 0x00, 0x19}};
constexpr uint8_t CUSTOM_COLOUR = 0xff;

const variant *variant_of(uint16_t pid)
{
    switch (pid) {
    case 0x2d51: return &KONE_PLUS_V;
    case 0x2e22: return &KONE_XTD_V;
    case 0x2e23: return &KONE_XTD_OPTICAL_V;
    case 0x2dbe: return &KONE_PURE_V;
    case 0x2db4: return &KONE_PURE_OPTICAL_V;
    case 0x2dbf:
    case 0x2dc2: return &KONE_PURE_MILITARY_V;
    default:     return nullptr;
    }
}

size_t buttons_size(const variant &v) { return 3 + 3 * 2 * static_cast<size_t>(v.inputs) + 2; }

uint8_t scale(uint8_t c, uint8_t a) { return static_cast<uint8_t>((c * a + 127) / 255); }

struct kone_plus_profile final : rockcat_profile {
    const variant &v;
    std::vector<uint8_t> settings, buttons;
    bool settings_dirty = false, buttons_dirty = false;
    std::vector<rockcat_macro> macros; // parked until the next write

    kone_plus_profile(const device_desc *d, int i, const variant &var)
        : rockcat_profile(d, i), v(var), settings(var.settings_size, 0), buttons(buttons_size(var), 0)
    {
    }

    const rockcat_caps &caps() const { return desc->caps; }
    rockcat_profile *clone() const override { return new kone_plus_profile(*this); }

    int fetch(rockcat_device *dev, uint8_t select, std::vector<uint8_t> &out, uint8_t id)
    {
        int r = select_data(dev, static_cast<uint8_t>(index), select);
        if (r != ROCKCAT_OK)
            return r;
        std::fill(out.begin(), out.end(), 0);
        out[0] = id;
        return get_raw(dev, out.data(), out.size());
    }

    int read(rockcat_device *dev) override
    {
        settings_dirty = buttons_dirty = false;
        macros.clear();
        int r = fetch(dev, SELECT_SETTINGS, settings, REPORT_SETTINGS);
        if (r != ROCKCAT_OK)
            return r;
        if (settings[1] != v.settings_size || settings[2] != index)
            return ROCKCAT_ERR_PROTOCOL;
        if (v.settings_checksum && !checksum_ok(settings.data(), settings.size()))
            return ROCKCAT_ERR_CHECKSUM;
        r = fetch(dev, SELECT_BUTTONS, buttons, REPORT_BUTTONS);
        if (r != ROCKCAT_OK)
            return r;
        if (buttons[1] != buttons.size() || buttons[2] != index)
            return ROCKCAT_ERR_PROTOCOL;
        // unlike the Kova and the Kone AIMO this one does carry a checksum
        return checksum_ok(buttons.data(), buttons.size()) ? ROCKCAT_OK : ROCKCAT_ERR_CHECKSUM;
    }

    int write(rockcat_device *dev) override
    {
        if (settings_dirty) {
            if (v.settings_checksum)
                put_checksum(settings.data(), settings.size());
            int r = set_raw(dev, settings.data(), settings.size(), true);
            if (r != ROCKCAT_OK)
                return r;
            settings_dirty = false;
        }
        // macros before the buttons that point at them
        while (!macros.empty()) {
            int r = write_macro(dev, macros.front());
            if (r != ROCKCAT_OK)
                return r;
            macros.erase(macros.begin());
        }
        if (buttons_dirty) {
            put_checksum(buttons.data(), buttons.size());
            int r = set_raw(dev, buttons.data(), buttons.size(), true);
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
        settings[2] = buttons[2] = static_cast<uint8_t>(i);
        for (rockcat_macro &m : macros)
            m.profile = static_cast<uint8_t>(i);
        settings_dirty = buttons_dirty = true;
        return ROCKCAT_OK;
    }

    void export_payload(std::vector<uint8_t> &out) const override
    {
        out.insert(out.end(), settings.begin(), settings.end());
        out.insert(out.end(), buttons.begin(), buttons.end());
    }

    int import_payload(const uint8_t *buf, size_t len) override
    {
        size_t s = settings.size();
        if (len != s + buttons.size() || buf[0] != REPORT_SETTINGS || buf[1] != s || buf[s] != REPORT_BUTTONS || buf[s + 1] != buttons.size())
            return ROCKCAT_ERR_ARGUMENT;
        std::memcpy(settings.data(), buf, s);
        std::memcpy(buttons.data(), buf + s, buttons.size());
        return set_index(index);
    }

    // dpi as the mouse stores it and back. 0 = the code means nothing we know
    int dpi_from(uint8_t raw) const
    {
        if (v.cpi_table) {
            for (int i = 0; i < 7; ++i)
                if (PURE_OPTICAL_CODE[i] == raw)
                    return PURE_OPTICAL_DPI[i];
            return 0;
        }
        return raw * v.dpi_unit;
    }

    int dpi_to(int dpi) const
    {
        if (v.cpi_table) {
            for (int i = 0; i < 7; ++i)
                if (PURE_OPTICAL_DPI[i] == dpi)
                    return PURE_OPTICAL_CODE[i];
            return -1;
        }
        return dpi / v.dpi_unit;
    }

    int get_dpi(int level, int *x, int *y, int *enabled) const override
    {
        if (level < 0 || level >= DPI_LEVELS)
            return ROCKCAT_ERR_ARGUMENT;
        int dx = dpi_from(settings[static_cast<size_t>(v.o_cpi_x + level)]);
        // on the Kone[+], XTD and Pure the y bytes are "reported in events but not used" (roccat-tools), so x is the truth there
        int dy = (caps().features & ROCKCAT_HAS_DPI_XY) ? dpi_from(settings[static_cast<size_t>(v.o_cpi_y + level)]) : dx;
        if (x)
            *x = dx;
        if (y)
            *y = dy;
        if (enabled)
            *enabled = (settings[6] >> level) & 1;
        return ROCKCAT_OK;
    }

    int set_dpi(int level, int x, int y, int enabled) override
    {
        const rockcat_caps &c = caps();
        bool xy = c.features & ROCKCAT_HAS_DPI_XY;
        if (level < 0 || level >= DPI_LEVELS || (!xy && x != y))
            return ROCKCAT_ERR_ARGUMENT;
        if (x < c.dpi_min || x > c.dpi_max || y < c.dpi_min || y > c.dpi_max)
            return ROCKCAT_ERR_ARGUMENT;
        x -= x % c.dpi_step;
        y -= y % c.dpi_step;
        int rx = dpi_to(x), ry = dpi_to(y);
        // the Pure Optical only has its seven, 2400 is a multiple of the step and still not one of them
        if (rx < 0 || ry < 0)
            return ROCKCAT_ERR_ARGUMENT;
        settings[static_cast<size_t>(v.o_cpi_x + level)] = static_cast<uint8_t>(rx);
        if (v.o_cpi_y >= 0)
            settings[static_cast<size_t>(v.o_cpi_y + level)] = static_cast<uint8_t>(xy ? ry : rx);
        settings[6] = static_cast<uint8_t>(enabled ? settings[6] | (1 << level) : settings[6] & ~(1 << level));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int effect_get() const
    {
        int raw = settings[static_cast<size_t>(v.o_effect)];
        if (v.effects == EFFECTS_PURE)
            return raw == 2 ? ROCKCAT_EFFECT_BREATHING : raw;
        return raw;
    }

    int get_int(rockcat_setting which, int *value) const override
    {
        if (!value)
            return ROCKCAT_ERR_ARGUMENT;
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:   *value = settings[static_cast<size_t>(v.o_startup)]; break;
        case ROCKCAT_SET_POLLING_HZ:   *value = rockcat_polling_rate_hz(settings[static_cast<size_t>(v.o_polling)]); break;
        case ROCKCAT_SET_EFFECT:       *value = effect_get(); break;
        case ROCKCAT_SET_EFFECT_SPEED: *value = settings[static_cast<size_t>(v.o_speed)]; break;
        // 0 off, 1 all at once, 2-5 up, down, left, right. the api only knows on and off
        case ROCKCAT_SET_COLOR_FLOW:   *value = settings[static_cast<size_t>(v.o_flow)] ? 1 : 0; break;
        case ROCKCAT_SET_ANGLE_SNAPPING:
            if (v.o_advanced < 0)
                return ROCKCAT_ERR_UNSUPPORTED;
            *value = settings[static_cast<size_t>(v.o_advanced2 >= 0 ? v.o_advanced2 : v.o_advanced)] & 1;
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        return ROCKCAT_OK;
    }

    int set_int(rockcat_setting which, int value) override
    {
        const rockcat_caps &c = caps();
        switch (which) {
        case ROCKCAT_SET_DPI_ACTIVE:
            if (value < 0 || value >= DPI_LEVELS)
                return ROCKCAT_ERR_ARGUMENT;
            settings[static_cast<size_t>(v.o_startup)] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_POLLING_HZ: {
            int rate = -1;
            for (int i = 0; i < 4; ++i)
                if (rockcat_polling_rate_hz(static_cast<uint8_t>(i)) == value)
                    rate = i;
            if (rate < 0)
                return ROCKCAT_ERR_ARGUMENT;
            settings[static_cast<size_t>(v.o_polling)] = static_cast<uint8_t>(rate);
            break;
        }
        case ROCKCAT_SET_EFFECT: {
            if (value < 0 || value > 31 || !(c.effects & (1u << value)))
                return ROCKCAT_ERR_ARGUMENT;
            int raw = value;
            if (v.effects == EFFECTS_PURE)
                raw = value == ROCKCAT_EFFECT_BREATHING ? 2 : value;
            settings[static_cast<size_t>(v.o_effect)] = static_cast<uint8_t>(raw);
            break;
        }
        case ROCKCAT_SET_EFFECT_SPEED:
            if (value < c.effect_speed_min || value > c.effect_speed_max)
                return ROCKCAT_ERR_ARGUMENT;
            settings[static_cast<size_t>(v.o_speed)] = static_cast<uint8_t>(value);
            break;
        case ROCKCAT_SET_COLOR_FLOW: {
            // keeps the direction it had, "on" from off means all at once
            uint8_t &flow = settings[static_cast<size_t>(v.o_flow)];
            flow = value ? (flow ? flow : 1) : 0;
            break;
        }
        case ROCKCAT_SET_ANGLE_SNAPPING:
            if (v.o_advanced < 0)
                return ROCKCAT_ERR_UNSUPPORTED;
            // bit 0 is angle snap, bit 1 sensor alignment. the XTD Optical has the byte twice, roccat-tools writes both the same
            for (int o : {v.o_advanced, v.o_advanced2})
                if (o >= 0)
                    settings[static_cast<size_t>(o)] = static_cast<uint8_t>(value ? settings[static_cast<size_t>(o)] | 1 : settings[static_cast<size_t>(o)] & ~1);
            break;
        default:
            return ROCKCAT_ERR_UNSUPPORTED;
        }
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    uint8_t *light(int led) { return settings.data() + v.o_light + 4 * led; }
    const uint8_t *light(int led) const { return settings.data() + v.o_light + 4 * led; }

    int get_led(int led, rockcat_color *c, int *enabled) const override
    {
        if (led < 0 || led >= v.leds)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *l = light(led);
        if (c)
            *c = v.blue_green ? rockcat_color{255, l[1], l[3], l[2]} : rockcat_color{255, l[1], l[2], l[3]};
        if (enabled)
            *enabled = (settings[static_cast<size_t>(v.o_lights_on)] >> led) & 1;
        return ROCKCAT_OK;
    }

    int set_led(int led, rockcat_color c, int enabled) override
    {
        if (led < 0 || led >= v.leds)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t rgb[3] = {scale(c.r, c.a), scale(c.g, c.a), scale(c.b, c.a)};
        uint8_t *l = light(led);
        if (v.palette_only) {
            // the Kone[+] never got a custom colour from roccat-tools, only table entries. so it gets the closest one of
            // the 33 and not a guess about what index ff does on it
            int best = 0;
            long best_d = -1;
            for (int i = 0; i < 33; ++i) {
                long d = 0;
                for (int k = 0; k < 3; ++k)
                    d += (static_cast<long>(rgb[k]) - PALETTE[i][k]) * (static_cast<long>(rgb[k]) - PALETTE[i][k]);
                if (best_d < 0 || d < best_d) {
                    best_d = d;
                    best = i;
                }
            }
            l[0] = static_cast<uint8_t>(best);
            std::memcpy(l + 1, PALETTE[best], 3);
        } else {
            if (v.o_custom >= 0) {
                settings[static_cast<size_t>(v.o_custom)] = 1;
                l[0] = CUSTOM_COLOUR;
            } else {
                // the XTDs decide table or custom for all four lights by looking at the first one, so all of them go custom.
                // their rgb bytes already hold what the table entry looks like, so the others dont change colour
                for (int i = 0; i < v.leds; ++i)
                    light(i)[0] = CUSTOM_COLOUR;
            }
            l[1] = rgb[0];
            l[2] = v.blue_green ? rgb[2] : rgb[1];
            l[3] = v.blue_green ? rgb[1] : rgb[2];
        }
        uint8_t &on = settings[static_cast<size_t>(v.o_lights_on)];
        on = static_cast<uint8_t>(enabled ? on | (1 << led) : on & ~(1 << led));
        settings_dirty = true;
        return ROCKCAT_OK;
    }

    int slot(int layer, int input) const
    {
        return layer < 0 || layer >= caps().layer_count || input < 0 || input >= v.inputs ? -1 : layer * v.inputs + input;
    }

    int get_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        const uint8_t *p = buttons.data() + 3 + 3 * i;
        *a = rockcat_assignment{p[0], p[1], p[2]};
        return ROCKCAT_OK;
    }

    int set_input(int layer, int input, const rockcat_assignment *a) override
    {
        int i = slot(layer, input);
        if (i < 0 || !a)
            return ROCKCAT_ERR_ARGUMENT;
        uint8_t *p = buttons.data() + 3 + 3 * i;
        p[0] = a->type;
        p[1] = a->param1;
        p[2] = a->param2;
        buttons_dirty = true;
        return ROCKCAT_OK;
    }

    int reset_inputs() override
    {
        for (int i = 0; i < 2 * v.inputs; ++i) {
            uint8_t *p = buttons.data() + 3 + 3 * i;
            p[0] = v.factory[i];
            p[1] = p[2] = 0;
        }
        buttons_dirty = true;
        return ROCKCAT_OK;
    }

    int factory_input(int layer, int input, rockcat_assignment *a) const override
    {
        int i = slot(layer, input);
        if (i < 0)
            return ROCKCAT_ERR_ARGUMENT;
        *a = rockcat_assignment{v.factory[i], 0, 0};
        return ROCKCAT_OK;
    }

    int get_macro(rockcat_device *dev, int layer, int input, rockcat_macro *m) const override
    {
        int i = slot(layer, input);
        if (i < 0 || !m)
            return ROCKCAT_ERR_ARGUMENT;
        // the select for a macro is [04 profile button], the button being the index in the buttons report
        int r = select_data(dev, static_cast<uint8_t>(index), static_cast<uint8_t>(i));
        if (r != ROCKCAT_OK)
            return r;
        std::vector<uint8_t> rep(M_SIZE, 0);
        rep[0] = REPORT_MACRO;
        r = get_raw(dev, rep.data(), rep.size());
        if (r != ROCKCAT_OK)
            return r;
        std::memset(m, 0, sizeof *m);
        m->profile = static_cast<uint8_t>(index);
        m->button = static_cast<uint8_t>(i);
        // a slot that never got a macro doesnt say its size. thats "no macro", not an error
        if (get16(rep.data() + 1) != M_SIZE)
            return ROCKCAT_OK;
        if (!checksum_ok(rep.data(), rep.size()))
            return ROCKCAT_ERR_CHECKSUM;
        m->loop = rep[M_LOOP];
        std::memcpy(m->set_name, rep.data() + M_SET_NAME, 24);
        std::memcpy(m->name, rep.data() + M_NAME, 24);
        m->count = std::min<uint16_t>(get16(rep.data() + M_COUNT), ROCKCAT_MACRO_KEYSTROKES);
        for (int k = 0; k < m->count; ++k) {
            const uint8_t *e = rep.data() + M_KEYS + 4 * k;
            m->keys[k] = rockcat_keystroke{e[0], e[1], get16(e + 2)};
        }
        return ROCKCAT_OK;
    }

    static int write_macro(rockcat_device *dev, const rockcat_macro &m)
    {
        std::vector<uint8_t> rep(M_SIZE, 0);
        rep[0] = REPORT_MACRO;
        put16(rep.data() + 1, M_SIZE);
        rep[M_PROFILE] = m.profile;
        rep[M_BUTTON] = m.button;
        rep[M_LOOP] = m.loop;
        // 24 bytes each on the mouse, roccat-tools keeps them zero terminated so we do too
        std::memcpy(rep.data() + M_SET_NAME, m.set_name, strnlen(m.set_name, 23));
        std::memcpy(rep.data() + M_NAME, m.name, strnlen(m.name, 23));
        put16(rep.data() + M_COUNT, m.count);
        for (int k = 0; k < m.count; ++k) {
            uint8_t *e = rep.data() + M_KEYS + 4 * k;
            e[0] = m.keys[k].key;
            e[1] = m.keys[k].action;
            put16(e + 2, m.keys[k].delay_ms);
        }
        put_checksum(rep.data(), rep.size());
        return set_raw(dev, rep.data(), rep.size(), true);
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
    const variant *v = variant_of(desc->pid);
    return v ? new kone_plus_profile(desc, index, *v) : nullptr;
}

// [09 06 firmware dfu ? ?], firmware 100 = "1.00" like on every other ROCCAT
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    uint8_t info[6] = {REPORT_INFO};
    int r = get_raw(dev, info, sizeof info);
    if (r != ROCKCAT_OK)
        return r;
    std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
    return ROCKCAT_OK;
}

// three bytes here, [05 03 profile], and not the four of the Kone AIMO
int get_active(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t rep[3] = {REPORT_PROFILE};
    int r = get_raw(dev, rep, sizeof rep);
    if (r != ROCKCAT_OK)
        return r;
    if (rep[2] >= PROFILES)
        return ROCKCAT_ERR_PROTOCOL;
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
    const uint8_t rep[3] = {REPORT_PROFILE, 3, profile};
    return set_raw(dev, rep, sizeof rep, true);
}

// Talk FX, the way roccat-tools' gfx does it for all of them but the XTD: [10 10 ff ff ff state zone 0 effect speed rgb rgb].
// ff ff ff = leave Easy-Shift, its lock and Easy-Aim alone, state 1 zone 1 effect 1 (on) speed 1, the same colour as ambient
// and event colour. state 0 and everything else zero gives the lights back to the profile
int talk(rockcat_device *dev, bool on, rockcat_color c)
{
    const uint8_t rep[16] = {REPORT_TALK, 0x10, 0xff, 0xff, 0xff,
                             static_cast<uint8_t>(on), static_cast<uint8_t>(on), 0, static_cast<uint8_t>(on), static_cast<uint8_t>(on),
                             c.r, c.g, c.b, c.r, c.g, c.b};
    int r = set_raw(dev, rep, sizeof rep, false);
    return r == ROCKCAT_OK ? wait_ready_after(dev, 0) : r;
}

rockcat_color folded(const rockcat_color &c)
{
    return rockcat_color{255, scale(c.r, c.a), scale(c.g, c.a), scale(c.b, c.a)};
}

int direct_begin(rockcat_device *)
{
    return ROCKCAT_OK;
}

int direct_frame_talk(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!colors || !dev->desc || count != dev->desc->caps.led_count)
        return ROCKCAT_ERR_ARGUMENT;
    // one colour for the whole mouse this way, the first led speaks for all of them
    return talk(dev, true, folded(colors[0]));
}

// the Kone XTD has its own layout of report 10: [10 10 02 01 rgb rgb rgb rgb], mode 2 = "gfx", effect 1 = on, a colour per light
int direct_frame_xtd(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!colors || count != 4)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t rep[16] = {REPORT_TALK, 0x10, 0x02, 0x01};
    for (int i = 0; i < 4; ++i) {
        rockcat_color c = folded(colors[i]);
        rep[4 + 3 * i] = c.r;
        rep[5 + 3 * i] = c.g;
        rep[6 + 3 * i] = c.b;
    }
    int r = set_raw(dev, rep, sizeof rep, false);
    return r == ROCKCAT_OK ? wait_ready_after(dev, 0) : r;
}

// roccat-tools never hands the XTD back, its gfx just stays. so this is a guess: the plain Talk FX "off" the XTD understands
// for its Easy-Shift talk as well
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

// 01 is the plain mouse report. 03 is [03 00 type data1 data2]: 20 profile, b0 cpi, c0 sensitivity switched on the mouse,
// ff = a button with Easy-Shift or a talk function went down (data2 0) or up (data2 1), data1 = which one + 1
bool decode_event(const uint8_t *in, int n, rockcat_event *ev)
{
    if (in[0] == 0x01 && n >= 2) {
        *ev = rockcat_event{ROCKCAT_EVENT_ACTIVITY, in[1], 0};
        return true;
    }
    if (in[0] != 0x03 || n < 5)
        return false;
    switch (in[2]) {
    case 0x20:
    case 0xb0:
    case 0xc0:
        *ev = rockcat_event{ROCKCAT_EVENT_CHANGED, in[3], 0};
        break;
    case 0xff:
        *ev = rockcat_event{ROCKCAT_EVENT_EASYSHIFT, in[3], in[4] == 0};
        break;
    default:
        *ev = rockcat_event{ROCKCAT_EVENT_NONE, 0, 0};
        break;
    }
    return true;
}

const char *input_label_12(int input)
{
    static const char *names[12] = {"Left button", "Right button", "Middle button", "Thumb forward", "Thumb backward", "Wheel tilt left",
                                    "Wheel tilt right", "Wheel up", "Wheel down", "Plus button", "Minus button", "Top button"};
    return input >= 0 && input < 12 ? names[input] : "?";
}

const char *input_label_9(int input)
{
    static const char *names[9] = {"Left button", "Right button", "Middle button", "Thumb forward", "Thumb backward",
                                   "Wheel up", "Wheel down", "Plus button", "Minus button"};
    return input >= 0 && input < 9 ? names[input] : "?";
}

const char *led_label_4(int led)
{
    static const char *names[4] = {"Top left", "Bottom left", "Top right", "Bottom right"};
    return led >= 0 && led < 4 ? names[led] : "?";
}

const char *led_label_1(int led)
{
    return led == 0 ? "Logo" : "?";
}

const char *layer_label(int layer)
{
    return layer == 0 ? "Normal" : layer == 1 ? "Easy-Shift[+]" : "?";
}

const family_ops KONE_PLUS_FAMILY = {firmware, get_active, set_active, new_profile, direct_begin, direct_frame_talk, direct_end,
                                     direct_switch, direct_active, decode_event, input_label_12, led_label_4, layer_label};
const family_ops KONE_XTD_FAMILY = {firmware, get_active, set_active, new_profile, direct_begin, direct_frame_xtd, direct_end,
                                    direct_switch, direct_active, decode_event, input_label_12, led_label_4, layer_label};
const family_ops KONE_PURE_FAMILY = {firmware, get_active, set_active, new_profile, direct_begin, direct_frame_talk, direct_end,
                                     direct_switch, direct_active, decode_event, input_label_9, led_label_1, layer_label};

constexpr uint32_t bit(int n) { return 1u << n; }

constexpr uint32_t EFFECTS_4 = bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BLINKING) |
                               bit(ROCKCAT_EFFECT_BREATHING) | bit(ROCKCAT_EFFECT_HEARTBEAT);
constexpr uint32_t EFFECTS_1 = bit(ROCKCAT_EFFECT_OFF) | bit(ROCKCAT_EFFECT_FULLY_LIT) | bit(ROCKCAT_EFFECT_BREATHING);
constexpr uint32_t BASE = ROCKCAT_HAS_DPI | ROCKCAT_HAS_MACROS | ROCKCAT_HAS_EASYSHIFT | ROCKCAT_HAS_RGB | ROCKCAT_HAS_DIRECT_LIGHT;

// 5 profiles, two layers, speed 1-3, no effect the pc has to draw. nobody has tried any of them, so tested = no
#define KONE_PLUS_CAPS(family, features, inputs, leds, dmin, dmax, dstep, effects)                                    \
    {ROCKCAT_KIND_MOUSE, family, ROCKCAT_TESTED_NO, "", features, 5, inputs, 2, leds, 5, dmin, dmax, dstep,           \
     {125, 250, 500, 1000, 0}, effects, 0, 1, 3, -1}

const device_desc KONE_PLUS_DEVICES[] = {
    {0x2d51, "ROCCAT Kone[+]", &KONE_PLUS_FAMILY, nullptr, 0x06, KONE_PLUS_CAPS("kone-plus", BASE, 12, 4, 100, 6000, 100, EFFECTS_4)},
    // the vendor tool offers 200 to 8200 in steps of 200, the mouse counts in 50s
    {0x2e22, "ROCCAT Kone XTD", &KONE_XTD_FAMILY, nullptr, 0x06, KONE_PLUS_CAPS("kone-xtd", BASE, 12, 4, 200, 8200, 200, EFFECTS_4)},
    {0x2e23, "ROCCAT Kone XTD Optical", &KONE_PLUS_FAMILY, nullptr, 0x06,
     KONE_PLUS_CAPS("kone-xtd-optical", BASE | ROCKCAT_HAS_DPI_XY | ROCKCAT_HAS_ANGLE_SNAP, 12, 4, 100, 6400, 100, EFFECTS_4)},
    {0x2dbe, "ROCCAT Kone Pure", &KONE_PURE_FAMILY, nullptr, 0x06, KONE_PLUS_CAPS("kone-pure", BASE, 9, 1, 200, 8200, 200, EFFECTS_1)},
    // seven fixed steps between 400 and 4000, all multiples of 400 (but not every multiple is one of them)
    {0x2db4, "ROCCAT Kone Pure Optical", &KONE_PURE_FAMILY, nullptr, 0x06,
     KONE_PLUS_CAPS("kone-pure-optical", BASE, 9, 1, 400, 4000, 400, EFFECTS_1)},
    {0x2dbf, "ROCCAT Kone Pure Military", &KONE_PURE_FAMILY, nullptr, 0x06,
     KONE_PLUS_CAPS("kone-pure-military", BASE | ROCKCAT_HAS_DPI_XY | ROCKCAT_HAS_ANGLE_SNAP, 9, 1, 100, 5000, 100, EFFECTS_1)},
    // roccat-tools drives it with the Military code, same reports
    {0x2dc2, "ROCCAT Kone Pure Optical Black", &KONE_PURE_FAMILY, nullptr, 0x06,
     KONE_PLUS_CAPS("kone-pure-military", BASE | ROCKCAT_HAS_DPI_XY | ROCKCAT_HAS_ANGLE_SNAP, 9, 1, 100, 5000, 100, EFFECTS_1)},
};

const device_registration reg(KONE_PLUS_DEVICES, sizeof KONE_PLUS_DEVICES / sizeof KONE_PLUS_DEVICES[0]);

} // namespace

#include "kone_plus_sim.inc"
