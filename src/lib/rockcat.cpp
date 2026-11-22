// rockcat-swarm core library: HID transport and Kone AIMO Remastered protocol.
//
// Report flow mirrors ROCCAT_Swarm_Monitor.exe (kone_aimo_base_device_c):
//   * every SetFeature is followed by polling report 0x04 until the device
//     reports "ready" (status byte 1) / "invalid" (2) instead of "busy" (3);
//   * per-profile data is read by first writing a 0x04 "select" report and
//     then fetching the data report with GetFeature.

#include "internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint8_t REPORT_CONTROL  = 0x04;
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

bool g_initialised = false;

const char *env(const char *name)
{
    const char *v = std::getenv(name);
    return v && *v ? v : nullptr;
}

rockcat_device_info to_info(const hid_node &n, const char *name)
{
    rockcat_device_info info;
    std::memset(&info, 0, sizeof info);
    std::snprintf(info.path, sizeof info.path, "%s", n.path.c_str());
    info.vendor_id = n.vid;
    info.product_id = n.pid;
    info.release = n.release;
    info.interface_number = n.interface_number;
    std::snprintf(info.name, sizeof info.name, "%s", name);
    std::snprintf(info.serial, sizeof info.serial, "%s", n.serial.c_str());
    return info;
}

// every hidraw node of a ROCCAT device, supported or not. with ROCKCAT_REPLAY set the recording is the only device there is
std::vector<rockcat_device_info> scan_raw()
{
    std::vector<rockcat_device_info> found;
    if (const char *file = env("ROCKCAT_REPLAY")) {
        rockcat_device_info info;
        if (transport_replay(file, &info))
            found.push_back(info);
        return found;
    }
    for (const hid_node &n : hid_nodes(ROCKCAT_VID))
        found.push_back(to_info(n, n.product.c_str()));
    return found;
}

// Collects the control interface of every supported device. When the report
// descriptor cannot be read the device's interface 0 is used as fallback.
std::vector<rockcat_device_info> scan()
{
    std::vector<rockcat_device_info> found, fallback;
    bool replay = env("ROCKCAT_REPLAY") != nullptr;
    for (rockcat_device_info info : scan_raw()) {
        const device_desc *m = device_find(info.vendor_id, info.product_id);
        if (!m)
            continue;
        std::snprintf(info.name, sizeof info.name, "%s", m->name);
        std::vector<uint8_t> ids;
        if (!replay)
            ids = hid_report_ids(info.path);
        if (replay || std::find(ids.begin(), ids.end(), REPORT_SETTINGS) != ids.end())
            found.push_back(info);
        else if (info.interface_number == 0)
            fallback.push_back(info);
    }
    return found.empty() ? fallback : found;
}

// real node, or the recording when ROCKCAT_REPLAY is set. ROCKCAT_CAPTURE writes everything that happens into a file
std::unique_ptr<transport> open_transport(rockcat_device_info &info)
{
    if (const char *file = env("ROCKCAT_REPLAY"))
        return transport_replay(file, nullptr);
    std::unique_ptr<transport> io = transport_open_hid(info.path);
    if (io)
        if (const char *file = env("ROCKCAT_CAPTURE"))
            io = transport_record(std::move(io), file, info);
    return io;
}

int get_raw(rockcat_device *dev, uint8_t *buf, size_t len)
{
    if (!dev || !dev->io || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t id = buf[0];
    int r = dev->io->get_feature(buf, len);
    if (r < 0)
        return r;
    if (buf[0] != id)
        return ROCKCAT_ERR_PROTOCOL;
    return ROCKCAT_OK;
}

int wait_ready(rockcat_device *dev)
{
    dev->io->sleep_ms(dev->wait.first_delay_ms);
    for (int i = 0; i < dev->wait.max_polls; ++i) {
        uint8_t status[3] = {REPORT_CONTROL, 0, 0};
        int r = dev->io->get_feature(status, sizeof status);
        if (r < 0)
            return r;
        switch (status[1]) {
        case 3: // busy
            dev->io->sleep_ms(dev->wait.poll_delay_ms);
            continue;
        case 2:
            return ROCKCAT_ERR_REJECTED;
        default:
            return ROCKCAT_OK;
        }
    }
    return ROCKCAT_ERR_BUSY;
}

int set_raw(rockcat_device *dev, const uint8_t *buf, size_t len, bool wait)
{
    if (!dev || !dev->io || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    int r = dev->io->set_feature(buf, len);
    if (r < 0)
        return r;
    return wait ? wait_ready(dev) : ROCKCAT_OK;
}

int select_data(rockcat_device *dev, uint8_t a, uint8_t b)
{
    const uint8_t sel[3] = {REPORT_CONTROL, a, b};
    return set_raw(dev, sel, sizeof sel, true);
}

void put_checksum(uint8_t *buf, size_t len)
{
    uint16_t sum = rockcat_checksum(buf, len - 2);
    buf[len - 2] = sum & 0xff;
    buf[len - 1] = sum >> 8;
}

bool checksum_ok(const uint8_t *buf, size_t len)
{
    uint16_t stored = buf[len - 2] | (buf[len - 1] << 8);
    return stored == rockcat_checksum(buf, len - 2);
}

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }

int ensure_init()
{
    return g_initialised ? ROCKCAT_OK : rockcat_init();
}

} // namespace

rockcat_device *device_from_transport(std::unique_ptr<transport> io, const rockcat_device_info &info)
{
    if (!io)
        return nullptr;
    auto *dev = new rockcat_device;
    dev->io = std::move(io);
    dev->info = info;
    dev->desc = device_find(info.vendor_id, info.product_id);
    return dev;
}

extern "C" {

const char *rockcat_version(void)
{
#define ROCKCAT_STR2(x) #x
#define ROCKCAT_STR(x) ROCKCAT_STR2(x)
    return ROCKCAT_STR(ROCKCAT_VERSION_MAJOR) "." ROCKCAT_STR(ROCKCAT_VERSION_MINOR) "." ROCKCAT_STR(ROCKCAT_VERSION_PATCH);
}

const char *rockcat_strerror(int error)
{
    switch (error) {
    case ROCKCAT_OK:            return "success";
    case ROCKCAT_ERR_INIT:      return "failed to initialise hidapi";
    case ROCKCAT_ERR_NO_DEVICE: return "no supported ROCCAT device found";
    case ROCKCAT_ERR_ACCESS:    return "cannot open device (check udev permissions)";
    case ROCKCAT_ERR_IO:        return "HID feature report transfer failed";
    case ROCKCAT_ERR_BUSY:      return "device stayed busy";
    case ROCKCAT_ERR_REJECTED:  return "device rejected the data";
    case ROCKCAT_ERR_CHECKSUM:  return "checksum mismatch in data read from device";
    case ROCKCAT_ERR_ARGUMENT:  return "invalid argument";
    case ROCKCAT_ERR_PROTOCOL:  return "unexpected answer from device";
    case ROCKCAT_ERR_UNSUPPORTED: return "this device has no such thing";
    default:                   return "unknown error";
    }
}

int rockcat_init(void)
{
    if (g_initialised)
        return ROCKCAT_OK;
    if (hid_backend_init() != ROCKCAT_OK)
        return ROCKCAT_ERR_INIT;
    g_initialised = true;
    return ROCKCAT_OK;
}

void rockcat_exit(void)
{
    if (g_initialised)
        hid_backend_exit();
    g_initialised = false;
}

int rockcat_enumerate(rockcat_device_info *out, int max)
{
    int r = ensure_init();
    if (r != ROCKCAT_OK)
        return r;
    auto found = scan();
    for (int i = 0; i < static_cast<int>(found.size()) && i < max && out; ++i)
        out[i] = found[i];
    return static_cast<int>(found.size());
}

int rockcat_open(const char *path, rockcat_device **out)
{
    if (!out)
        return ROCKCAT_ERR_ARGUMENT;
    *out = nullptr;
    int r = ensure_init();
    if (r != ROCKCAT_OK)
        return r;

    auto found = scan();
    const rockcat_device_info *chosen = nullptr;
    for (const auto &i : found)
        if (!path || std::strcmp(path, i.path) == 0) {
            chosen = &i;
            break;
        }
    if (!chosen)
        return ROCKCAT_ERR_NO_DEVICE;

    rockcat_device_info info = *chosen;
    *out = device_from_transport(open_transport(info), info);
    return *out ? ROCKCAT_OK : ROCKCAT_ERR_ACCESS;
}

int rockcat_enumerate_raw(rockcat_device_info *out, int max)
{
    int r = ensure_init();
    if (r != ROCKCAT_OK)
        return r;
    auto found = scan_raw();
    for (int i = 0; i < static_cast<int>(found.size()) && i < max && out; ++i)
        out[i] = found[i];
    return static_cast<int>(found.size());
}

int rockcat_open_raw(const char *path, rockcat_device **out)
{
    if (!out || (!path && !env("ROCKCAT_REPLAY")))
        return ROCKCAT_ERR_ARGUMENT;
    *out = nullptr;
    int r = ensure_init();
    if (r != ROCKCAT_OK)
        return r;
    for (rockcat_device_info info : scan_raw())
        if (!path || std::strcmp(path, info.path) == 0) {
            *out = device_from_transport(open_transport(info), info);
            return *out ? ROCKCAT_OK : ROCKCAT_ERR_ACCESS;
        }
    return ROCKCAT_ERR_NO_DEVICE;
}

int rockcat_report_ids(const char *path, uint8_t *ids, int max)
{
    if (!path)
        return ROCKCAT_ERR_ARGUMENT;
    int r = ensure_init();
    if (r != ROCKCAT_OK)
        return r;
    std::vector<uint8_t> found = hid_report_ids(path);
    for (int i = 0; i < static_cast<int>(found.size()) && i < max && ids; ++i)
        ids[i] = found[i];
    return static_cast<int>(found.size());
}

int rockcat_replay_verdict(rockcat_device *dev, int finished)
{
    if (!dev || !dev->io)
        return ROCKCAT_ERR_ARGUMENT;
    return dev->io->verdict(finished != 0);
}

void rockcat_close(rockcat_device *dev)
{
    delete dev;
}

int rockcat_get_info(rockcat_device *dev, rockcat_device_info *info)
{
    if (!dev || !info)
        return ROCKCAT_ERR_ARGUMENT;
    *info = dev->info;
    return ROCKCAT_OK;
}

int rockcat_get_firmware_version(rockcat_device *dev, char *buf, size_t len)
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

uint16_t rockcat_checksum(const uint8_t *buf, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum = static_cast<uint16_t>(sum + buf[i]);
    return sum;
}

int rockcat_feature_get(rockcat_device *dev, uint8_t *buf, size_t len)
{
    return get_raw(dev, buf, len);
}

int rockcat_feature_set(rockcat_device *dev, const uint8_t *buf, size_t len)
{
    return set_raw(dev, buf, len, true);
}

int rockcat_feature_send(rockcat_device *dev, const uint8_t *buf, size_t len)
{
    return set_raw(dev, buf, len, false);
}

int rockcat_feature_read(rockcat_device *dev, uint8_t *buf, size_t len)
{
    if (!dev || !dev->io || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    return dev->io->get_feature(buf, len);
}

int rockcat_read(rockcat_device *dev, uint8_t *buf, size_t len, int timeout_ms)
{
    if (!dev || !dev->io || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    return dev->io->read(buf, len, timeout_ms);
}

/* ---- profiles ---------------------------------------------------------- */

int rockcat_get_active_profile(rockcat_device *dev, uint8_t *profile, uint8_t *count)
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

int rockcat_set_active_profile(rockcat_device *dev, uint8_t profile)
{
    uint8_t current = 0, count = ROCKCAT_PROFILE_COUNT;
    int r = rockcat_get_active_profile(dev, &current, &count);
    if (r != ROCKCAT_OK)
        return r;
    if (count == 0 || count > ROCKCAT_PROFILE_COUNT)
        count = ROCKCAT_PROFILE_COUNT;
    if (profile >= count)
        return ROCKCAT_ERR_ARGUMENT;
    const uint8_t buf[4] = {REPORT_PROFILE, 4, profile, count};
    return set_raw(dev, buf, sizeof buf, true);
}

/* ---- settings ---------------------------------------------------------- */

int rockcat_polling_rate_hz(uint8_t rate)
{
    static const int hz[] = {125, 250, 500, 1000};
    return rate < 4 ? hz[rate] : 0;
}

const char *rockcat_effect_name(uint8_t effect)
{
    switch (effect) {
    case ROCKCAT_EFFECT_OFF:         return "Off";
    case ROCKCAT_EFFECT_FULLY_LIT:   return "Fully lit";
    case ROCKCAT_EFFECT_BLINKING:    return "Blinking";
    case ROCKCAT_EFFECT_BREATHING:   return "Breathing";
    case ROCKCAT_EFFECT_HEARTBEAT:   return "Heartbeat";
    case ROCKCAT_EFFECT_COLOR_CYCLE: return "Colour cycle";
    case ROCKCAT_EFFECT_SNAKE:       return "Snake";
    case ROCKCAT_EFFECT_BREATHING_2: return "Breathing 2.0";
    case ROCKCAT_EFFECT_HEARTBEAT_2: return "Heartbeat 2.0";
    case ROCKCAT_EFFECT_AIMO:        return "AIMO";
    case ROCKCAT_EFFECT_COLORWAVE:   return "Colorwave";
    default:                        return nullptr;
    }
}

int rockcat_effect_is_software(uint8_t effect)
{
    return effect == ROCKCAT_EFFECT_HEARTBEAT || effect == ROCKCAT_EFFECT_BREATHING_2 ||
           effect == ROCKCAT_EFFECT_HEARTBEAT_2 || effect == ROCKCAT_EFFECT_AIMO;
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

/* ---- buttons ----------------------------------------------------------- */

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

/* ---- macros ------------------------------------------------------------ */

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
    if (!out)
        return ROCKCAT_ERR_ARGUMENT;
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
    const uint8_t buf[ROCKCAT_DCU_SIZE] = {REPORT_TDCU, ROCKCAT_DCU_SIZE, 2, 0xff, 0, 0};
    return set_raw(dev, buf, sizeof buf, false);
}

int rockcat_calibration_poll(rockcat_device *dev, int *finished)
{
    if (!dev || !dev->io || !finished)
        return ROCKCAT_ERR_ARGUMENT;
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
    const uint8_t buf[ROCKCAT_DCU_SIZE] = {REPORT_TDCU, ROCKCAT_DCU_SIZE, 2, static_cast<uint8_t>(save ? 1 : 0), 0, 0};
    return set_raw(dev, buf, sizeof buf, true);
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

} // extern "C"
