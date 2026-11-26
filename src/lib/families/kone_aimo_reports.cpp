// Kone AIMO family, the part that knows the reports byte for byte (docs/protocol/kone-aimo.md is the readable version of this file)
// read and written through the structs in kone_aimo.h, which is the api rossat-swarm had. kone_aimo.cpp wraps it into a rockcat_profile

#include "../internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint8_t REPORT_PROFILE  = 0x05;
constexpr uint8_t REPORT_SETTINGS = 0x06;
constexpr uint8_t REPORT_BUTTONS  = 0x07;
constexpr uint8_t REPORT_MACRO    = 0x08;
constexpr uint8_t REPORT_INFO     = 0x09;
constexpr uint8_t REPORT_LEDS     = 0x0d;
constexpr uint8_t REPORT_TDCU     = 0x0f;
constexpr uint8_t REPORT_LED_MODE = 0x0e;

constexpr uint8_t SELECT_SETTINGS = 0x80;
constexpr uint8_t SELECT_BUTTONS  = 0x90;
constexpr uint8_t SELECT_MACRO_1  = 0x10;
constexpr uint8_t SELECT_MACRO_2  = 0x20;

constexpr size_t MACRO_CHUNK_REPORT = 0x402; // id + chunk number + 1024 payload
constexpr size_t MACRO_CHUNK_DATA   = 0x400;
constexpr size_t MACRO_HEADER_ID    = 0x0e;  // first byte of the macro blob
constexpr size_t MACRO_KEYS_OFFSET  = 0x50;
constexpr size_t MACRO_CSUM_OFFSET  = 2000;

constexpr const char *BUTTON_NAMES[ROCKCAT_BUTTON_COUNT / 2] = {
    "Left button",   "Right button",   "Middle button", "Wheel tilt left",
    "Wheel tilt right", "Wheel up",    "Wheel down",    "Thumb forward",
    "Thumb backward", "Thumb Easy-Shift[+]", "DPI up",  "DPI down",
};

// Factory layout from the Swarm module (FUN_6a44ea50).
constexpr uint8_t DEFAULT_LAYOUT[ROCKCAT_BUTTON_COUNT] = {
    ROCKCAT_BT_CLICK,      ROCKCAT_BT_MENU,        ROCKCAT_BT_UNIVERSAL_SCROLL,
    ROCKCAT_BT_TILT_LEFT,  ROCKCAT_BT_TILT_RIGHT,  ROCKCAT_BT_SCROLL_UP,
    ROCKCAT_BT_SCROLL_DOWN, ROCKCAT_BT_BROWSER_FORWARD, ROCKCAT_BT_BROWSER_BACKWARD,
    ROCKCAT_BT_EASYSHIFT,  ROCKCAT_BT_DPI_UP,      ROCKCAT_BT_DPI_DOWN,
    // Easy-Shift[+] layer
    ROCKCAT_BT_UNUSED,     ROCKCAT_BT_UNUSED,      ROCKCAT_BT_PLAY_PAUSE,
    ROCKCAT_BT_PREV_TRACK, ROCKCAT_BT_NEXT_TRACK,  ROCKCAT_BT_VOLUME_UP,
    ROCKCAT_BT_VOLUME_DOWN, ROCKCAT_BT_UNUSED,     ROCKCAT_BT_UNUSED,
    ROCKCAT_BT_UNUSED,     ROCKCAT_BT_PROFILE_UP,  ROCKCAT_BT_PROFILE_DOWN,
};

// the sensor calibration reports here are a Kone AIMO thing, sending them to a keyboard would be asking for trouble
bool has_calibration(const rockcat_device *dev)
{
    return dev && dev->desc && (dev->desc->caps.features & ROCKCAT_HAS_CALIBRATION);
}

// a family with a calibration of its own (the Nyth), nullptr for the Kone AIMO way
const calibration_ops *own_calibration(const rockcat_device *dev)
{
    return has_calibration(dev) ? dev->desc->family->calibration : nullptr;
}

} // namespace

extern "C" {

int kone_firmware_version(rockcat_device *dev, char *buf, size_t len)
{
    if (!dev || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    // Report 0x09 = [09 09 fw ...]; fw 105 = "1.05" (matches Swarm's
    // firmware_upgrade.ini). Fall back to the USB release number.
    uint8_t info[9] = {REPORT_INFO};
    if (get_raw(dev, info, sizeof info) == ROCKCAT_OK && info[1] == sizeof info) {
        std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
        return ROCKCAT_OK;
    }
    unsigned v = dev->info.release;
    std::snprintf(buf, len, "%x.%02x", v >> 8, v & 0xff);
    return ROCKCAT_OK;
}

int kone_get_active_profile(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t buf[4] = {REPORT_PROFILE, 0, 0, 0};
    int r = get_raw(dev, buf, sizeof buf);
    if (r != ROCKCAT_OK)
        return r;
    if (profile)
        *profile = buf[2];
    if (count)
        *count = buf[3];
    return ROCKCAT_OK;
}

int kone_set_active_profile(rockcat_device *dev, uint8_t profile)
{
    uint8_t current = 0, count = ROCKCAT_PROFILE_COUNT;
    int r = kone_get_active_profile(dev, &current, &count);
    if (r != ROCKCAT_OK)
        return r;
    if (count == 0 || count > ROCKCAT_PROFILE_COUNT)
        count = ROCKCAT_PROFILE_COUNT;
    if (profile >= count)
        return ROCKCAT_ERR_ARGUMENT;
    const uint8_t buf[4] = {REPORT_PROFILE, 4, profile, count};
    return set_raw(dev, buf, sizeof buf, true);
}

const char *rockcat_led_name(int led)
{
    static const char *names[ROCKCAT_LED_COUNT] = {
        "Wheel",        "Left 1 (front)", "Left 2",       "Left 3",
        "Left 4",       "Right 1 (front)", "Right 2",     "Right 3",
        "Right 4",      "Left 5 (rear)",   "Right 5 (rear)",
    };
    return led >= 0 && led < ROCKCAT_LED_COUNT ? names[led] : "?";
}

void rockcat_settings_decode(const uint8_t raw[ROCKCAT_SETTINGS_SIZE], rockcat_settings *s)
{
    std::memset(s, 0, sizeof *s);
    std::memcpy(s->raw, raw, ROCKCAT_SETTINGS_SIZE);
    s->profile = raw[0x02];
    s->dpi_enabled_mask = raw[0x04];
    s->dpi_active = raw[0x05];
    for (int i = 0; i < ROCKCAT_DPI_LEVELS; ++i) {
        s->dpi_x[i] = static_cast<uint16_t>(get16(raw + 0x06 + 2 * i) * ROCKCAT_DPI_STEP);
        s->dpi_y[i] = static_cast<uint16_t>(get16(raw + 0x10 + 2 * i) * ROCKCAT_DPI_STEP);
    }
    s->polling_rate = raw[0x1a];
    s->angle_snapping = raw[0x1b];
    s->debounce_ms = raw[0x1d];
    s->led_enable_mask = get16(raw + 0x1e);
    s->effect = raw[0x21];
    s->effect_speed = raw[0x22];
    s->brightness = raw[0x23];
    for (int i = 0; i < ROCKCAT_LED_COUNT; ++i) {
        const uint8_t *p = raw + 0x3d + 5 * i;
        s->leds[i] = rockcat_color{p[0], p[1], p[2], p[3]};
    }
    s->color_theme = raw[0x74];
}

void rockcat_settings_encode(const rockcat_settings *s, uint8_t raw[ROCKCAT_SETTINGS_SIZE])
{
    std::memcpy(raw, s->raw, ROCKCAT_SETTINGS_SIZE);
    raw[0x00] = REPORT_SETTINGS;
    raw[0x01] = ROCKCAT_SETTINGS_SIZE;
    raw[0x02] = s->profile;
    raw[0x04] = s->dpi_enabled_mask & 0x1f;
    raw[0x05] = s->dpi_active;
    for (int i = 0; i < ROCKCAT_DPI_LEVELS; ++i) {
        put16(raw + 0x06 + 2 * i, static_cast<uint16_t>(s->dpi_x[i] / ROCKCAT_DPI_STEP));
        put16(raw + 0x10 + 2 * i, static_cast<uint16_t>(s->dpi_y[i] / ROCKCAT_DPI_STEP));
    }
    raw[0x1a] = s->polling_rate;
    raw[0x1b] = s->angle_snapping;
    raw[0x1d] = s->debounce_ms;
    put16(raw + 0x1e, s->led_enable_mask & 0x7ff);
    raw[0x21] = s->effect;
    raw[0x22] = s->effect_speed;
    raw[0x23] = s->brightness;
    for (int i = 0; i < ROCKCAT_LED_COUNT; ++i) {
        uint8_t *p = raw + 0x3d + 5 * i;
        p[0] = s->leds[i].a;
        p[1] = s->leds[i].r;
        p[2] = s->leds[i].g;
        p[3] = s->leds[i].b;
        p[4] = 0;
    }
    raw[0x74] = s->color_theme;
    put_checksum(raw, ROCKCAT_SETTINGS_SIZE);
}

int rockcat_read_settings_raw(rockcat_device *dev, uint8_t profile, uint8_t buf[ROCKCAT_SETTINGS_SIZE])
{
    if (profile >= ROCKCAT_PROFILE_COUNT || !buf)
        return ROCKCAT_ERR_ARGUMENT;
    int r = select_data(dev, profile, SELECT_SETTINGS);
    if (r != ROCKCAT_OK)
        return r;
    std::memset(buf, 0, ROCKCAT_SETTINGS_SIZE);
    buf[0] = REPORT_SETTINGS;
    r = get_raw(dev, buf, ROCKCAT_SETTINGS_SIZE);
    if (r != ROCKCAT_OK)
        return r;
    if (buf[1] != ROCKCAT_SETTINGS_SIZE || buf[2] != profile)
        return ROCKCAT_ERR_PROTOCOL;
    return checksum_ok(buf, ROCKCAT_SETTINGS_SIZE) ? ROCKCAT_OK : ROCKCAT_ERR_CHECKSUM;
}

int rockcat_write_settings_raw(rockcat_device *dev, uint8_t buf[ROCKCAT_SETTINGS_SIZE])
{
    if (!buf || buf[2] >= ROCKCAT_PROFILE_COUNT)
        return ROCKCAT_ERR_ARGUMENT;
    buf[0] = REPORT_SETTINGS;
    buf[1] = ROCKCAT_SETTINGS_SIZE;
    put_checksum(buf, ROCKCAT_SETTINGS_SIZE);
    return set_raw(dev, buf, ROCKCAT_SETTINGS_SIZE, true);
}

int rockcat_read_settings(rockcat_device *dev, uint8_t profile, rockcat_settings *out)
{
    if (!out)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t raw[ROCKCAT_SETTINGS_SIZE];
    int r = rockcat_read_settings_raw(dev, profile, raw);
    if (r != ROCKCAT_OK)
        return r;
    rockcat_settings_decode(raw, out);
    return ROCKCAT_OK;
}

int rockcat_write_settings(rockcat_device *dev, const rockcat_settings *in)
{
    if (!in || in->profile >= ROCKCAT_PROFILE_COUNT || in->dpi_active >= ROCKCAT_DPI_LEVELS ||
        in->polling_rate > ROCKCAT_POLLING_1000HZ)
        return ROCKCAT_ERR_ARGUMENT;
    for (int i = 0; i < ROCKCAT_DPI_LEVELS; ++i)
        if (in->dpi_x[i] < ROCKCAT_DPI_MIN || in->dpi_x[i] > ROCKCAT_DPI_MAX ||
            in->dpi_y[i] < ROCKCAT_DPI_MIN || in->dpi_y[i] > ROCKCAT_DPI_MAX)
            return ROCKCAT_ERR_ARGUMENT;
    uint8_t raw[ROCKCAT_SETTINGS_SIZE];
    rockcat_settings_encode(in, raw);
    return set_raw(dev, raw, sizeof raw, true);
}

int rockcat_read_buttons(rockcat_device *dev, uint8_t profile, rockcat_buttons *out)
{
    if (!out || profile >= ROCKCAT_PROFILE_COUNT)
        return ROCKCAT_ERR_ARGUMENT;
    int r = select_data(dev, profile, SELECT_BUTTONS);
    if (r != ROCKCAT_OK)
        return r;
    uint8_t raw[ROCKCAT_BUTTONS_SIZE] = {REPORT_BUTTONS};
    r = get_raw(dev, raw, sizeof raw);
    if (r != ROCKCAT_OK)
        return r;
    // unlike settings and macros, this report carries no checksum
    if (raw[1] != ROCKCAT_BUTTONS_SIZE || raw[2] != profile)
        return ROCKCAT_ERR_PROTOCOL;
    out->profile = raw[2];
    for (int i = 0; i < ROCKCAT_BUTTON_COUNT; ++i) {
        const uint8_t *p = raw + 3 + 3 * i;
        out->buttons[i] = rockcat_button{p[0], p[1], p[2]};
    }
    return ROCKCAT_OK;
}

int rockcat_write_buttons(rockcat_device *dev, const rockcat_buttons *in)
{
    if (!in || in->profile >= ROCKCAT_PROFILE_COUNT)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t raw[ROCKCAT_BUTTONS_SIZE] = {REPORT_BUTTONS, ROCKCAT_BUTTONS_SIZE, in->profile};
    for (int i = 0; i < ROCKCAT_BUTTON_COUNT; ++i) {
        uint8_t *p = raw + 3 + 3 * i;
        p[0] = in->buttons[i].type;
        p[1] = in->buttons[i].param1;
        p[2] = in->buttons[i].param2;
    }
    return set_raw(dev, raw, sizeof raw, true);
}

int rockcat_read_macro(rockcat_device *dev, uint8_t profile, uint8_t button, rockcat_macro *out)
{
    if (!out || profile >= ROCKCAT_PROFILE_COUNT || button >= ROCKCAT_BUTTON_COUNT)
        return ROCKCAT_ERR_ARGUMENT;

    std::vector<uint8_t> blob(ROCKCAT_MACRO_SIZE, 0);
    const uint8_t selects[2] = {SELECT_MACRO_1, SELECT_MACRO_2};
    for (int chunk = 0; chunk < 2; ++chunk) {
        int r = select_data(dev, static_cast<uint8_t>(profile | selects[chunk]), button);
        if (r != ROCKCAT_OK)
            return r;
        std::vector<uint8_t> rep(MACRO_CHUNK_REPORT, 0);
        rep[0] = REPORT_MACRO;
        r = get_raw(dev, rep.data(), rep.size());
        if (r != ROCKCAT_OK)
            return r;
        size_t off = chunk * MACRO_CHUNK_DATA;
        size_t n = std::min(MACRO_CHUNK_DATA, ROCKCAT_MACRO_SIZE - off);
        std::memcpy(blob.data() + off, rep.data() + 2, n);
    }

    std::memset(out, 0, sizeof *out);
    const uint8_t *b = blob.data();
    out->profile = b[3];
    out->button = b[4];
    out->loop = b[5];
    std::memcpy(out->set_name, b + 0x06, 40);
    std::memcpy(out->name, b + 0x2e, 32);
    uint16_t cm = get16(b + 0x4e);
    out->mode = static_cast<uint8_t>(cm >> 12);
    out->count = std::min<uint16_t>(cm & 0x0fff, ROCKCAT_MACRO_KEYSTROKES);
    for (int i = 0; i < out->count; ++i) {
        const uint8_t *k = b + MACRO_KEYS_OFFSET + 4 * i;
        out->keys[i] = rockcat_keystroke{k[0], k[1], get16(k + 2)};
    }
    // An unused slot reads back as zeros; only validate populated macros.
    if (b[0] == MACRO_HEADER_ID && get16(b + MACRO_CSUM_OFFSET) != rockcat_checksum(b, MACRO_CSUM_OFFSET))
        return ROCKCAT_ERR_CHECKSUM;
    return ROCKCAT_OK;
}

int rockcat_write_macro(rockcat_device *dev, const rockcat_macro *in)
{
    if (!in || in->profile >= ROCKCAT_PROFILE_COUNT || in->button >= ROCKCAT_BUTTON_COUNT ||
        in->count > ROCKCAT_MACRO_KEYSTROKES || in->mode > ROCKCAT_MACRO_INFINITE)
        return ROCKCAT_ERR_ARGUMENT;

    std::vector<uint8_t> blob(ROCKCAT_MACRO_SIZE, 0);
    uint8_t *b = blob.data();
    b[0] = MACRO_HEADER_ID;
    put16(b + 1, ROCKCAT_MACRO_SIZE);
    b[3] = in->profile;
    b[4] = in->button;
    b[5] = in->loop;
    std::memcpy(b + 0x06, in->set_name, strnlen(in->set_name, 40));
    std::memcpy(b + 0x2e, in->name, strnlen(in->name, 32));
    put16(b + 0x4e, static_cast<uint16_t>((in->mode << 12) | (in->count & 0x0fff)));
    for (int i = 0; i < in->count; ++i) {
        uint8_t *k = b + MACRO_KEYS_OFFSET + 4 * i;
        k[0] = in->keys[i].key;
        k[1] = in->keys[i].action;
        put16(k + 2, in->keys[i].delay_ms);
    }
    put16(b + MACRO_CSUM_OFFSET, rockcat_checksum(b, MACRO_CSUM_OFFSET));

    for (int chunk = 0; chunk < 2; ++chunk) {
        std::vector<uint8_t> rep(MACRO_CHUNK_REPORT, 0);
        rep[0] = REPORT_MACRO;
        rep[1] = static_cast<uint8_t>(chunk + 1);
        size_t off = chunk * MACRO_CHUNK_DATA;
        size_t n = std::min(MACRO_CHUNK_DATA, ROCKCAT_MACRO_SIZE - off);
        std::memcpy(rep.data() + 2, b + off, n);
        int r = set_raw(dev, rep.data(), rep.size(), true);
        if (r != ROCKCAT_OK)
            return r;
    }
    return ROCKCAT_OK;
}

/* ---- sensor calibration (TDCU) -------------------------------------------- */

// Sequence from the Swarm module's dcu_calibration_fm (FUN_6a467480,
// FUN_6a467b80): [0f 06 02 ff] starts (no status wait), the control status
// stays "busy" while the sensor samples, [0f 06 02 01] keeps the result and
// [0f 06 02 00] discards it.

int rockcat_read_tdcu(rockcat_device *dev, rockcat_tdcu *out)
{
    if (!has_calibration(dev))
        return ROCKCAT_ERR_UNSUPPORTED;
    if (!out)
        return ROCKCAT_ERR_ARGUMENT;
    if (const calibration_ops *own = own_calibration(dev))
        return own->read ? own->read(dev, out) : ROCKCAT_ERR_UNSUPPORTED;
    uint8_t buf[ROCKCAT_DCU_SIZE] = {REPORT_TDCU};
    int r = get_raw(dev, buf, sizeof buf);
    if (r != ROCKCAT_OK)
        return r;
    out->mode = buf[2];
    out->state = buf[3];
    out->value = get16(buf + 4);
    return ROCKCAT_OK;
}

int rockcat_calibration_start(rockcat_device *dev)
{
    if (!has_calibration(dev))
        return ROCKCAT_ERR_UNSUPPORTED;
    if (const calibration_ops *own = own_calibration(dev))
        return own->start ? own->start(dev) : ROCKCAT_ERR_UNSUPPORTED;
    const uint8_t buf[ROCKCAT_DCU_SIZE] = {REPORT_TDCU, ROCKCAT_DCU_SIZE, 2, 0xff, 0, 0};
    return set_raw(dev, buf, sizeof buf, false);
}

int rockcat_calibration_poll(rockcat_device *dev, int *finished)
{
    if (!has_calibration(dev))
        return ROCKCAT_ERR_UNSUPPORTED;
    if (!dev || !dev->io || !finished)
        return ROCKCAT_ERR_ARGUMENT;
    if (const calibration_ops *own = own_calibration(dev))
        return own->poll ? own->poll(dev, finished) : ROCKCAT_ERR_UNSUPPORTED;
    uint8_t status[3] = {REPORT_CONTROL, 0, 0};
    int r = dev->io->get_feature(status, sizeof status);
    if (r < 0)
        return r;
    if (status[1] == 2)
        return ROCKCAT_ERR_REJECTED;
    *finished = status[1] != 3;
    return ROCKCAT_OK;
}

int rockcat_calibration_finish(rockcat_device *dev, int save)
{
    if (!has_calibration(dev))
        return ROCKCAT_ERR_UNSUPPORTED;
    if (const calibration_ops *own = own_calibration(dev))
        return own->finish ? own->finish(dev, save) : ROCKCAT_ERR_UNSUPPORTED;
    const uint8_t buf[ROCKCAT_DCU_SIZE] = {REPORT_TDCU, ROCKCAT_DCU_SIZE, 2, static_cast<uint8_t>(save ? 1 : 0), 0, 0};
    return set_raw(dev, buf, sizeof buf, true);
}

// the rest only exists on families with their own calibration
int rockcat_calibration_off(rockcat_device *dev)
{
    const calibration_ops *own = own_calibration(dev);
    return own && own->off ? own->off(dev) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_get_lift_off(rockcat_device *dev, int *level)
{
    if (!level)
        return ROCKCAT_ERR_ARGUMENT;
    const calibration_ops *own = own_calibration(dev);
    return own && own->get_lift_off ? own->get_lift_off(dev, level) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_set_lift_off(rockcat_device *dev, int level)
{
    if (level < ROCKCAT_LIFT_OFF_OFF || level > ROCKCAT_LIFT_OFF_NORMAL)
        return ROCKCAT_ERR_ARGUMENT;
    const calibration_ops *own = own_calibration(dev);
    return own && own->set_lift_off ? own->set_lift_off(dev, level) : ROCKCAT_ERR_UNSUPPORTED;
}

/* ---- direct lighting ----------------------------------------------------- */

int rockcat_set_direct_lighting(rockcat_device *dev, int enable)
{
    // byte 4 != 0 makes the firmware show host colours unscaled; otherwise
    // they are multiplied by the profile brightness (0 = invisible)
    const uint8_t buf[6] = {REPORT_LED_MODE, 6, 1, static_cast<uint8_t>(enable ? 1 : 0),
                            static_cast<uint8_t>(enable ? 0xff : 0), 0xff};
    return set_raw(dev, buf, sizeof buf, true);
}

int rockcat_set_direct_colors(rockcat_device *dev, const rockcat_color colors[ROCKCAT_LED_COUNT])
{
    if (!colors)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t buf[2 + 4 * ROCKCAT_LED_COUNT] = {REPORT_LEDS, sizeof buf};
    // The LEDs are R, G, B + amber. The firmware's own white (R=G=80 %,
    // B=100 %, amber on) looks mint; this neutral mix was calibrated by eye on
    // a Kone AIMO Remastered. The white part of each colour, min(R,G,B), is
    // drawn with that mix, the rest of the colour stays as given.
    constexpr int WHITE_R = 200, WHITE_G = 190, WHITE_B = 170, WHITE_A = 255;
    for (int i = 0; i < ROCKCAT_LED_COUNT; ++i) {
        const rockcat_color &c = colors[i];
        int r = (c.r * c.a + 127) / 255;
        int g = (c.g * c.a + 127) / 255;
        int b = (c.b * c.a + 127) / 255;
        int w = std::min(r, std::min(g, b));
        r = std::min(255, r - w + w * WHITE_R / 255);
        g = std::min(255, g - w + w * WHITE_G / 255);
        b = std::min(255, b - w + w * WHITE_B / 255);
        uint8_t *p = buf + 2 + 4 * i;
        p[0] = static_cast<uint8_t>(r);
        p[1] = static_cast<uint8_t>(g);
        p[2] = static_cast<uint8_t>(b);
        p[3] = static_cast<uint8_t>(w * WHITE_A / 255);
    }
    return set_raw(dev, buf, sizeof buf, false);
}

/* ---- names and the factory layout ------------------------------------------ */

const char *rockcat_button_name(int index)
{
    static std::string shifted[ROCKCAT_BUTTON_COUNT / 2];
    if (index < 0 || index >= ROCKCAT_BUTTON_COUNT)
        return "?";
    if (index < ROCKCAT_BUTTON_COUNT / 2)
        return BUTTON_NAMES[index];
    int base = index - ROCKCAT_BUTTON_COUNT / 2;
    if (shifted[base].empty())
        shifted[base] = std::string("Easy-Shift + ") + BUTTON_NAMES[base];
    return shifted[base].c_str();
}

void rockcat_buttons_default(uint8_t profile, rockcat_buttons *out)
{
    if (!out)
        return;
    std::memset(out, 0, sizeof *out);
    out->profile = profile;
    for (int i = 0; i < ROCKCAT_BUTTON_COUNT; ++i)
        out->buttons[i].type = DEFAULT_LAYOUT[i];
}

} // extern "C"
