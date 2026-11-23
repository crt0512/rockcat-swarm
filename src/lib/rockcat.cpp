// rockcat-swarm core library: finding and opening devices, raw report access, the bits every device shares.
//
// Report flow mirrors ROCCAT_Swarm_Monitor.exe (kone_aimo_base_device_c):
//   * every SetFeature is followed by polling report 0x04 until the device
//     reports "ready" (status byte 1) / "invalid" (2) instead of "busy" (3);
//   * per-profile data is read by first writing a 0x04 "select" report and
//     then fetching the data report with GetFeature.

#include "internal.h"

#include <map>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

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
    if (const char *name = env("ROCKCAT_SIM")) {
        rockcat_device_info info;
        if (transport_sim(name, &info))
            found.push_back(info);
        return found;
    }
    for (const hid_node &n : hid_nodes(ROCKCAT_VID))
        found.push_back(to_info(n, n.product.c_str()));
    // pretend devices somebody plugged in (the debug menu of the gui), next to the real ones
    for (const std::string &name : sims_plugged()) {
        rockcat_device_info info;
        if (transport_sim(name, &info))
            found.push_back(info);
    }
    return found;
}

// Collects the control interface of every supported device. When the report
// descriptor cannot be read the device's interface 0 is used as fallback.
std::vector<rockcat_device_info> scan()
{
    std::vector<rockcat_device_info> found, fallback;
    // neither a recording nor a simulated device has a report descriptor to look at
    const bool all_pretend = env("ROCKCAT_REPLAY") || env("ROCKCAT_SIM");
    for (rockcat_device_info info : scan_raw()) {
        const bool replay = all_pretend || std::strncmp(info.path, "sim:", 4) == 0;
        const device_desc *m = device_find(info.vendor_id, info.product_id);
        if (!m)
            continue;
        if (!replay)
            std::snprintf(info.name, sizeof info.name, "%s", m->name);
        if (m->interface_number >= 0) {
            if (replay || info.interface_number == m->interface_number)
                found.push_back(info);
            continue;
        }
        std::vector<uint8_t> ids;
        if (!replay)
            ids = hid_report_ids(info.path);
        if (replay || std::find(ids.begin(), ids.end(), m->probe_report) != ids.end())
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
    const char *sim = env("ROCKCAT_SIM");
    std::unique_ptr<transport> io = sim ? transport_sim(sim, nullptr)
                                    : std::strncmp(info.path, "sim:", 4) == 0 ? transport_sim(info.path + 4, nullptr)
                                                                              : transport_open_hid(info.path);
    if (io)
        if (const char *file = env("ROCKCAT_CAPTURE"))
            io = transport_record(std::move(io), file, info);
    return io;
}

int ensure_init()
{
    return g_initialised ? ROCKCAT_OK : rockcat_init();
}

} // namespace

std::mutex &device_mutex(rockcat_device *dev)
{
    static std::mutex guard;
    static std::map<std::string, std::unique_ptr<std::mutex>> locks;
    std::lock_guard<std::mutex> g(guard);
    std::unique_ptr<std::mutex> &m = locks[dev ? dev->info.path : ""];
    if (!m)
        m.reset(new std::mutex);
    return *m;
}

rockcat_device *device_from_transport(std::unique_ptr<transport> io, const rockcat_device_info &info)
{
    if (!io)
        return nullptr;
    auto *dev = new rockcat_device;
    dev->io = std::move(io);
    dev->info = info;
    dev->desc = device_find(info.vendor_id, info.product_id);
    if (dev->desc)
        dev->wait.select_delay_ms = dev->desc->select_delay_ms;
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
    if (!out || (!path && !env("ROCKCAT_REPLAY") && !env("ROCKCAT_SIM")))
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
    case ROCKCAT_EFFECT_FADE:        return "Fade FX";
    case ROCKCAT_EFFECT_EQUALIZER:   return "Equalizer";
    case ROCKCAT_EFFECT_RIPPLE:      return "Ripple FX";
    case ROCKCAT_EFFECT_WAVE:        return "Wave";
    case ROCKCAT_EFFECT_HEATMAP:     return "Heatmap";
    case ROCKCAT_EFFECT_GAME_PRESET: return "Game preset";
    case ROCKCAT_EFFECT_FIRE:        return "Fire";
    case ROCKCAT_EFFECT_DRIP:        return "Drip";
    case ROCKCAT_EFFECT_COLOR_FLOW:  return "Colour flow, smooth";
    case ROCKCAT_EFFECT_IMPULSE:     return "Impulse";
    default:                        return nullptr;
    }
}

int rockcat_effect_is_software(uint8_t effect)
{
    return effect == ROCKCAT_EFFECT_HEARTBEAT || effect == ROCKCAT_EFFECT_BREATHING_2 ||
           effect == ROCKCAT_EFFECT_HEARTBEAT_2 || effect == ROCKCAT_EFFECT_AIMO;
}

} // extern "C"
