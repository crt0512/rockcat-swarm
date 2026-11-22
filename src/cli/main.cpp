// rockcat-cli - command line front end for librockcat.

#include "rockcat/rockcat.h"

#include <cctype>
#include <chrono>
#include <csignal>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char *g_prog = "rockcat-cli";

void usage(FILE *f)
{
    std::fprintf(f,
        "Usage: %s [--device PATH] <command> [options]\n"
        "\n"
        "Commands:\n"
        "  list                         list supported devices\n"
        "  info                         device, firmware and active profile\n"
        "  profile [N]                  show or switch the active profile (1-5)\n"
        "  show [-p N]                  show settings of a profile (default: active)\n"
        "  set [-p N] KEY=VALUE...      change settings, keys:\n"
        "        dpiN=DPI | dpiN=XxY    DPI level N, how many levels and which range depends on the device\n"
        "        dpi-active=N           selected DPI level (1-5)\n"
        "        dpi-enabled=1,2,3      enabled DPI levels\n"
        "        polling=125|250|500|1000\n"
        "        angle-snapping=on|off  debounce=MS\n"
        "        effect=NAME|NUM        off, fully-lit, blinking, breathing, heartbeat, color-cycle,\n"
        "                               snake, breathing-2, heartbeat-2, aimo, colorwave\n"
        "        render=engine|mouse    who draws the effect; heartbeat, *-2 and aimo are always\n"
        "                               engine-rendered. Engine rendering is needed for a custom\n"
        "                               Easy-Shift colour (the mouse's own one is fixed #059cfa)\n"
        "        theme=1-10             AIMO colour theme\n"
        "        speed=N  brightness=0-255\n"
        "        led=all|wheel|left|right|N[,N..]:#RRGGBB[@ALPHA]\n"
        "        led-enable=all|none|MASK\n"
        "  buttons [-p N]               show button assignment\n"
        "  bind [-p N] BUTTON TYPE [ARGS]  e.g. bind 9 shortcut ctrl+c\n"
        "                               BUTTON: 1-12 or shift+1..12, TYPE: name or number\n"
        "  reset-buttons [-p N]         restore the factory button layout\n"
        "  types                        list button function names\n"
        "  macro [-p N] -b BUTTON                 show the macro stored for a button\n"
        "  macro [-p N] -b BUTTON --text TEXT [--delay MS] [--name NAME]\n"
        "                               store TEXT as a macro and bind it to BUTTON\n"
        "  direct off|#RRGGBB|C1,..,C11 host-controlled LED colours (\"off\" restores effect)\n"
        "  dump [-p N]                  hex dump of the raw settings and button reports\n"
        "  shift-light [-p N] [on|off] [#RRGGBB[@ALPHA]] [solid|pulse]\n"
        "                               lighting while Easy-Shift[+] is held (needs the engine)\n"
        "  calibrate                    TDCU sensor calibration for your mousepad\n"
        "  tdcu                         show the stored sensor calibration\n"
        "  engine [--mode auto|aimo|heartbeat]\n"
        "                               run the lighting engine (AIMO, Heartbeat, Breathing/Heartbeat 2.0)\n"
        "                               in the foreground until Ctrl+C\n"
        "  backup FILE                  save settings + buttons of all 5 profiles\n"
        "  restore FILE                 write a backup back to the mouse\n"
        "\n"
        "For devices that arent supported yet, and for finding out how they talk:\n"
        "  raw list                     every ROCCAT hidraw node with the report ids it declares\n"
        "  raw get ID LEN               read feature report ID (hex), LEN bytes (needs --device)\n"
        "  raw set BYTES...             send a feature report (hex bytes), then wait for the ready status\n"
        "  raw send BYTES...            same but without asking the device how it feels about it\n"
        "  raw wait                     poll the status report (04) until the device stops being busy\n"
        "  raw read [MS]                print input reports for MS milliseconds (default 1000)\n"
        "  capture FILE COMMAND...      run COMMAND and append everything that went over the wire to FILE\n"
        "\n"
        "ROCKCAT_REPLAY=FILE makes every command talk to a recording instead of hardware.\n"
        "\n"
        "Profiles, DPI levels and buttons are numbered from 1.\n",
        g_prog);
}

[[noreturn]] void die(const char *what, int err)
{
    std::fprintf(stderr, "%s: %s: %s\n", g_prog, what, rockcat_strerror(err));
    std::exit(1);
}

[[noreturn]] void fail(const char *fmt, const char *arg = "")
{
    std::fprintf(stderr, "%s: ", g_prog);
    std::fprintf(stderr, fmt, arg);
    std::fputc('\n', stderr);
    std::exit(2);
}

void check(int r, const char *what)
{
    if (r != ROCKCAT_OK)
        die(what, r);
}

long parse_long(const std::string &s, const char *what)
{
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 0);
    if (s.empty() || *end)
        fail("invalid number for %s", what);
    return v;
}

bool parse_bool(const std::string &s)
{
    if (s == "on" || s == "1" || s == "true" || s == "yes")
        return true;
    if (s == "off" || s == "0" || s == "false" || s == "no")
        return false;
    fail("expected on/off, got '%s'", s.c_str());
}

bool parse_color(const std::string &s, rockcat_color *c)
{
    std::string hex = s, alpha;
    auto at = s.find('@');
    if (at != std::string::npos) {
        hex = s.substr(0, at);
        alpha = s.substr(at + 1);
    }
    if (!hex.empty() && hex[0] == '#')
        hex.erase(0, 1);
    if (hex.size() != 6)
        return false;
    for (char ch : hex)
        if (!std::isxdigit(static_cast<unsigned char>(ch)))
            return false;
    unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
    c->r = (v >> 16) & 0xff;
    c->g = (v >> 8) & 0xff;
    c->b = v & 0xff;
    c->a = alpha.empty() ? 255 : static_cast<uint8_t>(parse_long(alpha, "alpha") & 0xff);
    return true;
}

std::vector<int> led_selection(const std::string &s, int led_count)
{
    if (s == "all") {
        std::vector<int> v;
        for (int i = 0; i < led_count; ++i)
            v.push_back(i);
        return v;
    }
    // these three only mean something on the Kone AIMO with its two led strips
    if (led_count == 11) {
        if (s == "wheel")
            return {0};
        if (s == "left")
            return {1, 2, 3, 4, 9};
        if (s == "right")
            return {5, 6, 7, 8, 10};
    }
    std::vector<int> v;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string part = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        long n = parse_long(part, "LED index");
        if (n < 1 || n > led_count)
            fail("%s", ("LED index must be 1-" + std::to_string(led_count)).c_str());
        v.push_back(static_cast<int>(n - 1));
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return v;
}

void print_color(const rockcat_color &c)
{
    std::printf("#%02x%02x%02x@%d", c.r, c.g, c.b, c.a);
}


void print_profile(rockcat_device *dev, const rockcat_profile *p)
{
    const rockcat_caps *caps = rockcat_get_caps(dev);
    std::printf("Profile %d\n", rockcat_profile_index(p) + 1);
    int v = 0;
    if (caps->features & ROCKCAT_HAS_DPI) {
        int active = 0;
        rockcat_profile_get_int(p, ROCKCAT_SET_DPI_ACTIVE, &active);
        std::printf("  DPI levels:\n");
        for (int i = 0; i < caps->dpi_levels; ++i) {
            int x = 0, y = 0, en = 0;
            rockcat_profile_get_dpi(p, i, &x, &y, &en);
            std::printf("    %c %d: ", i == active ? '*' : ' ', i + 1);
            if (x == y)
                std::printf("%5d", x);
            else
                std::printf("%5dx%d", x, y);
            std::printf("%s\n", en ? "" : "  (disabled)");
        }
    }
    // whatever the device doesnt have simply doesnt get a line
    if (rockcat_profile_get_int(p, ROCKCAT_SET_POLLING_HZ, &v) == ROCKCAT_OK)
        std::printf("  Polling rate:    %d Hz\n", v);
    if (rockcat_profile_get_int(p, ROCKCAT_SET_ANGLE_SNAPPING, &v) == ROCKCAT_OK)
        std::printf("  Angle snapping:  %s\n", v ? "on" : "off");
    if (rockcat_profile_get_int(p, ROCKCAT_SET_DEBOUNCE_MS, &v) == ROCKCAT_OK)
        std::printf("  Debounce time:   %d ms\n", v);
    int eff = 0, by_engine = 0;
    if (rockcat_profile_get_effect(p, &eff, &by_engine) == ROCKCAT_OK) {
        const char *ename = rockcat_effect_name(static_cast<uint8_t>(eff));
        std::printf("  Lighting effect: %s (%d), %s\n", ename ? ename : "unknown", eff,
                    by_engine ? "rendered by the rockcat engine (custom Easy-Shift colour possible)"
                              : "rendered by the mouse (Easy-Shift shows the built-in light blue)");
    }
    if (rockcat_profile_get_int(p, ROCKCAT_SET_COLOR_THEME, &v) == ROCKCAT_OK)
        std::printf("  AIMO theme:      %d\n", v + 1);
    if (rockcat_profile_get_int(p, ROCKCAT_SET_EFFECT_SPEED, &v) == ROCKCAT_OK)
        std::printf("  Effect speed:    %d\n", v);
    if (rockcat_profile_get_int(p, ROCKCAT_SET_BRIGHTNESS, &v) == ROCKCAT_OK)
        std::printf("  Brightness:      %d\n", v);
    unsigned mask = 0;
    for (int i = 0; i < caps->led_count; ++i) {
        int en = 0;
        rockcat_profile_get_led(p, i, nullptr, &en);
        mask |= static_cast<unsigned>(en) << i;
    }
    std::printf("  LEDs (enable mask 0x%03x):\n", mask);
    for (int i = 0; i < caps->led_count; ++i) {
        rockcat_color c{};
        rockcat_profile_get_led(p, i, &c, nullptr);
        std::printf("    %2d %-12s ", i + 1, rockcat_led_label(caps, i));
        print_color(c);
        std::printf("%s\n", mask & (1u << i) ? "" : "  (off)");
    }
}

// a button on a layer. layer 1 is Easy-Shift[+], written "shift+N" on the command line
struct slot {
    int layer = 0, input = 0;
};

std::string slot_key(slot sl)
{
    return (sl.layer ? "shift+" : "") + std::to_string(sl.input + 1);
}

std::string slot_name(rockcat_device *dev, slot sl)
{
    return (sl.layer ? "Easy-Shift + " : "") + std::string(rockcat_input_label(rockcat_get_caps(dev), sl.input));
}

void print_buttons(rockcat_device *dev, const rockcat_profile *p)
{
    const rockcat_caps *caps = rockcat_get_caps(dev);
    std::printf("Profile %d buttons\n", rockcat_profile_index(p) + 1);
    for (int layer = 0; layer < caps->layer_count; ++layer)
        for (int input = 0; input < caps->input_count; ++input) {
            slot sl{layer, input};
            rockcat_assignment bt{};
            rockcat_profile_get_input(p, layer, input, &bt);
            const char *tn = rockcat_button_type_name(bt.type);
            std::printf("  %-9s %-34s %s", slot_key(sl).c_str(), slot_name(dev, sl).c_str(), tn ? tn : "unknown");
            if (bt.type == ROCKCAT_BT_SHORTCUT) {
                char sc[64];
                rockcat_format_shortcut(bt.param1, bt.param2, sc, sizeof sc);
                std::printf(" %s", sc);
            } else if (!tn || bt.param1 || bt.param2) {
                std::printf(" [type 0x%02x, 0x%02x, 0x%02x]", bt.type, bt.param1, bt.param2);
            }
            std::printf("\n");
        }
}

// only the syntax, whether the device has that many buttons gets checked once we know which device it is
slot parse_button(const std::string &s)
{
    slot sl;
    std::string t = s;
    if (t.rfind("shift+", 0) == 0) {
        sl.layer = 1;
        t = t.substr(6);
    }
    sl.input = static_cast<int>(parse_long(t, "button")) - 1;
    return sl;
}

void check_slot(rockcat_device *dev, slot sl)
{
    const rockcat_caps *caps = rockcat_get_caps(dev);
    if (sl.input < 0 || sl.input >= caps->input_count || sl.layer >= caps->layer_count) {
        std::string n = std::to_string(caps->input_count);
        fail("%s", ("button must be 1-" + n + (caps->layer_count > 1 ? " or shift+1.." + n : "")).c_str());
    }
}

void hexdump(const char *title, const uint8_t *buf, size_t len)
{
    std::printf("%s (%zu bytes)\n", title, len);
    for (size_t i = 0; i < len; i += 16) {
        std::printf("  %04zx:", i);
        for (size_t j = i; j < i + 16 && j < len; ++j)
            std::printf(" %02x", buf[j]);
        std::printf("\n");
    }
}

// US layout ASCII -> HID usage (+ shift flag).
bool ascii_to_hid(char ch, uint8_t *usage, bool *shift)
{
    static const char *row_lower = "1234567890";
    *shift = false;
    if (ch >= 'a' && ch <= 'z') { *usage = static_cast<uint8_t>(0x04 + ch - 'a'); return true; }
    if (ch >= 'A' && ch <= 'Z') { *usage = static_cast<uint8_t>(0x04 + ch - 'A'); *shift = true; return true; }
    if (const char *p = std::strchr(row_lower, ch); p && ch) { *usage = static_cast<uint8_t>(0x1e + (p - row_lower)); return true; }
    struct { char c; uint8_t u; bool s; } map[] = {
        {'\n', 0x28, false}, {'\t', 0x2b, false}, {' ', 0x2c, false}, {'-', 0x2d, false},
        {'=', 0x2e, false}, {'[', 0x2f, false}, {']', 0x30, false}, {'\\', 0x31, false},
        {';', 0x33, false}, {'\'', 0x34, false}, {'`', 0x35, false}, {',', 0x36, false},
        {'.', 0x37, false}, {'/', 0x38, false},
        {'!', 0x1e, true}, {'@', 0x1f, true}, {'#', 0x20, true}, {'$', 0x21, true},
        {'%', 0x22, true}, {'^', 0x23, true}, {'&', 0x24, true}, {'*', 0x25, true},
        {'(', 0x26, true}, {')', 0x27, true}, {'_', 0x2d, true}, {'+', 0x2e, true},
        {'{', 0x2f, true}, {'}', 0x30, true}, {'|', 0x31, true}, {':', 0x33, true},
        {'"', 0x34, true}, {'~', 0x35, true}, {'<', 0x36, true}, {'>', 0x37, true},
        {'?', 0x38, true},
    };
    for (const auto &m : map)
        if (m.c == ch) { *usage = m.u; *shift = m.s; return true; }
    return false;
}

struct options {
    const char *device = nullptr;
    int profile = -1; // 0-based, -1 = active
    std::vector<std::string> args;
};

rockcat_device *open_device(const options &o)
{
    rockcat_device *dev = nullptr;
    check(rockcat_open(o.device, &dev), "open");
    return dev;
}

int resolve_profile(rockcat_device *dev, const options &o)
{
    if (o.profile >= 0) {
        int count = rockcat_get_caps(dev)->profile_count;
        if (o.profile >= count)
            fail("%s", ("profile must be 1-" + std::to_string(count)).c_str());
        return o.profile;
    }
    uint8_t p = 0;
    check(rockcat_get_active_profile(dev, &p, nullptr), "read active profile");
    return p;
}

rockcat_profile *read_profile(rockcat_device *dev, const options &o)
{
    rockcat_profile *p = nullptr;
    check(rockcat_profile_read(dev, resolve_profile(dev, o), &p), "read profile");
    return p;
}

void print_hex(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        std::printf("%s%02x", i ? " " : "", buf[i]);
    std::printf("\n");
}

std::vector<uint8_t> parse_bytes(const std::vector<std::string> &args, size_t from)
{
    std::vector<uint8_t> out;
    for (size_t i = from; i < args.size(); ++i) {
        char *end = nullptr;
        long v = std::strtol(args[i].c_str(), &end, 16);
        if (args[i].empty() || *end || v < 0 || v > 255)
            fail("'%s' is not a hex byte", args[i].c_str());
        out.push_back(static_cast<uint8_t>(v));
    }
    if (out.empty())
        fail("need at least the report id");
    return out;
}

// straight to the reports, no model check and no safety net. this is how devices get figured out before they are supported
int cmd_raw(const options &o)
{
    std::string sub = o.args.empty() ? "" : o.args[0];
    if (sub == "list") {
        rockcat_device_info infos[32];
        int n = rockcat_enumerate_raw(infos, 32);
        if (n < 0)
            die("enumerate", n);
        for (int i = 0; i < n && i < 32; ++i) {
            std::printf("%s  %04x:%04x  if%d  %s\n   reports:", infos[i].path, infos[i].vendor_id, infos[i].product_id,
                        infos[i].interface_number, infos[i].name);
            uint8_t ids[64];
            int k = rockcat_report_ids(infos[i].path, ids, 64);
            for (int j = 0; j < k && j < 64; ++j)
                std::printf(" %02x", ids[j]);
            std::printf(k > 0 ? "\n" : " (none declared or not readable)\n");
        }
        if (n == 0)
            std::printf("no ROCCAT hidraw nodes around. plugged in? udev rule installed?\n");
        return n > 0 ? 0 : 1;
    }
    if (sub != "get" && sub != "set" && sub != "send" && sub != "wait" && sub != "read")
        fail("raw wants one of: list, get, set, send, wait, read");
    if (!o.device && !std::getenv("ROCKCAT_REPLAY") && !std::getenv("ROCKCAT_SIM"))
        fail("raw %s needs --device /dev/hidrawN (see 'raw list'), guessing which node you meant is how keyboards get bricked", sub.c_str());
    rockcat_device *dev = nullptr;
    check(rockcat_open_raw(o.device, &dev), "open");
    int rc = 0;
    if (sub == "get") {
        if (o.args.size() != 3)
            fail("usage: raw get ID LEN");
        long id = std::strtol(o.args[1].c_str(), nullptr, 16), len = parse_long(o.args[2], "length");
        if (id < 0 || id > 255 || len < 1 || len > 4096)
            fail("id is one hex byte, length 1-4096");
        std::vector<uint8_t> buf(static_cast<size_t>(len), 0);
        buf[0] = static_cast<uint8_t>(id);
        int n = rockcat_feature_read(dev, buf.data(), buf.size());
        if (n < 0)
            die("get feature", n);
        print_hex(buf.data(), static_cast<size_t>(n));
    } else if (sub == "set" || sub == "send") {
        std::vector<uint8_t> buf = parse_bytes(o.args, 1);
        check(sub == "set" ? rockcat_feature_set(dev, buf.data(), buf.size()) : rockcat_feature_send(dev, buf.data(), buf.size()), "set feature");
    } else if (sub == "wait") {
        // same loop the library runs after a write: 1 = ready, 2 = didnt like it, 3 = busy
        for (int i = 0;; ++i) {
            uint8_t st[3] = {0x04, 0, 0};
            int n = rockcat_feature_read(dev, st, sizeof st);
            if (n < 0)
                die("status", n);
            if (st[1] != 3 || i >= 100) {
                print_hex(st, static_cast<size_t>(n));
                rc = st[1] == 1 ? 0 : 1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } else {
        long ms = o.args.size() > 1 ? parse_long(o.args[1], "milliseconds") : 1000;
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            long left = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
            if (left <= 0)
                break;
            uint8_t buf[256];
            int n = rockcat_read(dev, buf, sizeof buf, static_cast<int>(left));
            if (n < 0)
                die("read", n);
            if (n > 0)
                print_hex(buf, static_cast<size_t>(n));
        }
    }
    rockcat_close(dev);
    return rc;
}

int cmd_list()
{
    rockcat_device_info infos[16];
    int n = rockcat_enumerate(infos, 16);
    if (n < 0)
        die("enumerate", n);
    if (n == 0) {
        std::printf("No supported devices found.\n");
        return 1;
    }
    for (int i = 0; i < n && i < 16; ++i)
        std::printf("%s  %04x:%04x  if%d  %s%s%s\n", infos[i].path, infos[i].vendor_id,
                    infos[i].product_id, infos[i].interface_number, infos[i].name,
                    infos[i].serial[0] ? "  serial " : "", infos[i].serial);
    return 0;
}

int cmd_info(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_device_info info;
    rockcat_get_info(dev, &info);
    char fw[16];
    rockcat_get_firmware_version(dev, fw, sizeof fw);
    uint8_t p = 0, count = 0;
    check(rockcat_get_active_profile(dev, &p, &count), "read active profile");
    std::printf("Device:         %s\n", info.name);
    std::printf("USB ID:         %04x:%04x (interface %d)\n", info.vendor_id, info.product_id, info.interface_number);
    std::printf("Path:           %s\n", info.path);
    std::printf("Firmware:       %s\n", fw);
    std::printf("Active profile: %d of %d\n", p + 1, count);
    rockcat_close(dev);
    return 0;
}

int cmd_profile(const options &o)
{
    rockcat_device *dev = open_device(o);
    if (o.args.empty()) {
        uint8_t p = 0, count = 0;
        check(rockcat_get_active_profile(dev, &p, &count), "read active profile");
        std::printf("%d\n", p + 1);
    } else {
        long n = parse_long(o.args[0], "profile");
        int count = rockcat_get_caps(dev)->profile_count;
        if (n < 1 || n > count)
            fail("%s", ("profile must be 1-" + std::to_string(count)).c_str());
        check(rockcat_set_active_profile(dev, static_cast<uint8_t>(n - 1)), "set active profile");
    }
    rockcat_close(dev);
    return 0;
}

int cmd_show(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_profile *p = read_profile(dev, o);
    print_profile(dev, p);
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

// effect/render are applied after all keys, see cmd_set
int g_new_effect = -1, g_new_render = -1;

void apply_setting(rockcat_device *dev, rockcat_profile *p, const std::string &kv)
{
    const rockcat_caps *caps = rockcat_get_caps(dev);
    auto eq = kv.find('=');
    if (eq == std::string::npos)
        fail("expected KEY=VALUE, got '%s'", kv.c_str());
    std::string key = kv.substr(0, eq), val = kv.substr(eq + 1);
    // the cli already said what was wrong in its own words wherever it can, this is for whatever slips through
    auto set_int = [&](rockcat_setting which, long v) {
        int r = rockcat_profile_set_int(p, which, static_cast<int>(v));
        if (r == ROCKCAT_ERR_UNSUPPORTED)
            fail("this device has no '%s' setting", key.c_str());
        if (r != ROCKCAT_OK)
            fail("value out of range for '%s'", key.c_str());
    };

    if (key.size() == 4 && key.rfind("dpi", 0) == 0 && key[3] >= '1' && key[3] < '1' + caps->dpi_levels) {
        int lvl = key[3] - '1';
        long x, y;
        auto sep = val.find_first_of("xX");
        if (sep != std::string::npos) {
            x = parse_long(val.substr(0, sep), "DPI");
            y = parse_long(val.substr(sep + 1), "DPI");
        } else {
            x = y = parse_long(val, "DPI");
        }
        for (long v : {x, y})
            if (v < caps->dpi_min || v > caps->dpi_max || v % caps->dpi_step)
                fail("%s", ("DPI must be " + std::to_string(caps->dpi_min) + "-" + std::to_string(caps->dpi_max) + " in steps of " +
                            std::to_string(caps->dpi_step)).c_str());
        if (x != y && !(caps->features & ROCKCAT_HAS_DPI_XY))
            fail("this device cant do different DPI for x and y");
        int en = 1;
        rockcat_profile_get_dpi(p, lvl, nullptr, nullptr, &en);
        rockcat_profile_set_dpi(p, lvl, static_cast<int>(x), static_cast<int>(y), en);
    } else if (key == "dpi-active") {
        long n = parse_long(val, key.c_str());
        if (n < 1 || n > caps->dpi_levels)
            fail("%s", ("dpi-active must be 1-" + std::to_string(caps->dpi_levels)).c_str());
        set_int(ROCKCAT_SET_DPI_ACTIVE, n - 1);
        int x = 0, y = 0;
        rockcat_profile_get_dpi(p, static_cast<int>(n - 1), &x, &y, nullptr);
        rockcat_profile_set_dpi(p, static_cast<int>(n - 1), x, y, 1);
    } else if (key == "dpi-enabled") {
        unsigned mask = 0;
        size_t pos = 0;
        while (pos <= val.size()) {
            size_t c = val.find(',', pos);
            long n = parse_long(val.substr(pos, c == std::string::npos ? std::string::npos : c - pos), key.c_str());
            if (n < 1 || n > caps->dpi_levels)
                fail("%s", ("DPI levels are 1-" + std::to_string(caps->dpi_levels)).c_str());
            mask |= 1u << (n - 1);
            if (c == std::string::npos)
                break;
            pos = c + 1;
        }
        int active = 0;
        rockcat_profile_get_int(p, ROCKCAT_SET_DPI_ACTIVE, &active);
        if (!(mask & (1u << active)))
            fail("the active DPI level must stay enabled");
        for (int i = 0; i < caps->dpi_levels; ++i) {
            int x = 0, y = 0;
            rockcat_profile_get_dpi(p, i, &x, &y, nullptr);
            rockcat_profile_set_dpi(p, i, x, y, (mask >> i) & 1);
        }
    } else if (key == "polling") {
        long hz = parse_long(val, key.c_str());
        if (rockcat_profile_set_int(p, ROCKCAT_SET_POLLING_HZ, static_cast<int>(hz)) != ROCKCAT_OK) {
            std::string rates;
            for (int i = 0; i < 8 && caps->polling_hz[i]; ++i)
                rates += (i == 0 ? "" : caps->polling_hz[i + 1] && i < 7 ? ", " : " or ") + std::to_string(caps->polling_hz[i]);
            fail("polling must be %s", rates.c_str());
        }
    } else if (key == "angle-snapping") {
        set_int(ROCKCAT_SET_ANGLE_SNAPPING, parse_bool(val) ? 1 : 0);
    } else if (key == "debounce") {
        long ms = parse_long(val, key.c_str());
        if (ms < 0 || ms > 255)
            fail("debounce must be 0-255 ms");
        set_int(ROCKCAT_SET_DEBOUNCE_MS, ms);
    } else if (key == "effect") {
        struct { const char *n; uint8_t v; } names[] = {
            {"off", ROCKCAT_EFFECT_OFF}, {"fully-lit", ROCKCAT_EFFECT_FULLY_LIT},
            {"static", ROCKCAT_EFFECT_FULLY_LIT},
            {"blinking", ROCKCAT_EFFECT_BLINKING}, {"breathing", ROCKCAT_EFFECT_BREATHING},
            {"heartbeat", ROCKCAT_EFFECT_HEARTBEAT}, {"color-cycle", ROCKCAT_EFFECT_COLOR_CYCLE},
            {"snake", ROCKCAT_EFFECT_SNAKE}, {"breathing-2", ROCKCAT_EFFECT_BREATHING_2},
            {"heartbeat-2", ROCKCAT_EFFECT_HEARTBEAT_2}, {"aimo", ROCKCAT_EFFECT_AIMO},
            {"colorwave", ROCKCAT_EFFECT_COLORWAVE}};
        bool found = false;
        for (const auto &n : names)
            if (val == n.n) {
                g_new_effect = n.v;
                found = true;
            }
        if (!found)
            g_new_effect = static_cast<int>(parse_long(val, key.c_str()) & 0xff);
    } else if (key == "render") {
        if (val != "engine" && val != "mouse")
            fail("render must be engine or mouse");
        g_new_render = val == "engine";
    } else if (key == "theme") {
        long th = parse_long(val, key.c_str());
        if (th < 1 || th > ROCKCAT_AIMO_THEMES)
            fail("theme must be 1-10");
        set_int(ROCKCAT_SET_COLOR_THEME, th - 1);
    } else if (key == "speed") {
        set_int(ROCKCAT_SET_EFFECT_SPEED, parse_long(val, key.c_str()) & 0xff);
    } else if (key == "brightness") {
        long b = parse_long(val, key.c_str());
        if (b < 0 || b > 255)
            fail("brightness must be 0-255");
        set_int(ROCKCAT_SET_BRIGHTNESS, b);
    } else if (key == "led") {
        auto colon = val.rfind(':');
        if (colon == std::string::npos)
            fail("led expects SELECTION:#RRGGBB");
        rockcat_color c;
        if (!parse_color(val.substr(colon + 1), &c))
            fail("invalid colour '%s'", val.substr(colon + 1).c_str());
        for (int i : led_selection(val.substr(0, colon), caps->led_count)) {
            int en = 1;
            rockcat_profile_get_led(p, i, nullptr, &en);
            rockcat_profile_set_led(p, i, c, en);
        }
    } else if (key == "led-enable") {
        unsigned all = (1u << caps->led_count) - 1;
        unsigned mask = val == "all" ? all : val == "none" ? 0 : static_cast<unsigned>(parse_long(val, key.c_str())) & all;
        for (int i = 0; i < caps->led_count; ++i) {
            rockcat_color c{};
            rockcat_profile_get_led(p, i, &c, nullptr);
            rockcat_profile_set_led(p, i, c, (mask >> i) & 1);
        }
    } else {
        fail("unknown setting '%s'", key.c_str());
    }
}

int cmd_set(const options &o)
{
    if (o.args.empty())
        fail("set needs at least one KEY=VALUE");
    rockcat_device *dev = open_device(o);
    rockcat_profile *p = read_profile(dev, o);
    for (const auto &kv : o.args)
        apply_setting(dev, p, kv);
    if (g_new_effect >= 0 || g_new_render >= 0) {
        int eff = 0, engine = 0;
        check(rockcat_profile_get_effect(p, &eff, &engine), "read effect");
        if (g_new_effect >= 0)
            eff = g_new_effect;
        if (g_new_render >= 0)
            engine = g_new_render;
        if (g_new_render == 0 && rockcat_effect_is_software(static_cast<uint8_t>(eff)))
            fail("%s can only be rendered by the engine", rockcat_effect_name(static_cast<uint8_t>(eff)));
        check(rockcat_profile_select_effect(p, eff, engine), "select effect");
    }
    check(rockcat_profile_write(dev, p), "write profile");
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

int cmd_buttons(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_profile *p = read_profile(dev, o);
    print_buttons(dev, p);
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

int cmd_bind(const options &o)
{
    if (o.args.size() < 2)
        fail("bind needs BUTTON and TYPE");
    slot sl = parse_button(o.args[0]);
    int type = rockcat_button_type_from_string(o.args[1].c_str());
    if (type < 0)
        fail("unknown button type '%s' (see 'types')", o.args[1].c_str());
    rockcat_device *dev = open_device(o);
    check_slot(dev, sl);
    rockcat_profile *p = read_profile(dev, o);
    rockcat_assignment a{static_cast<uint8_t>(type), 0, 0};
    uint8_t mods = 0, key = 0;
    if (type == ROCKCAT_BT_SHORTCUT && o.args.size() == 3 &&
        rockcat_parse_shortcut(o.args[2].c_str(), &mods, &key) == ROCKCAT_OK) {
        a.param1 = mods;
        a.param2 = key;
    } else {
        if (type == ROCKCAT_BT_SHORTCUT && o.args.size() < 3)
            fail("shortcut needs a key combination, e.g. ctrl+c");
        a.param1 = o.args.size() > 2 ? static_cast<uint8_t>(parse_long(o.args[2], "P1")) : 0;
        a.param2 = o.args.size() > 3 ? static_cast<uint8_t>(parse_long(o.args[3], "P2")) : 0;
    }
    check(rockcat_profile_set_input(p, sl.layer, sl.input, &a), "bind");
    check(rockcat_profile_write(dev, p), "write profile");
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

int cmd_reset_buttons(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_profile *p = read_profile(dev, o);
    check(rockcat_profile_reset_inputs(p), "reset buttons");
    check(rockcat_profile_write(dev, p), "write profile");
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

int cmd_types()
{
    for (int i = 0; i < rockcat_button_type_count(); ++i) {
        uint8_t t = static_cast<uint8_t>(rockcat_button_type_at(i));
        std::printf("  0x%02x  %-20s %s\n", t, rockcat_button_type_key(t), rockcat_button_type_name(t));
    }
    std::printf("\nshortcut takes a key combination: bind 9 shortcut ctrl+c / alt+f4 / shift+win+s\n"
                "(raw form: bind 9 shortcut MODMASK USAGE, mod bits 2 shift, 4 ctrl, 8 alt, 16 win).\n");
    return 0;
}

int cmd_macro(const options &o)
{
    slot sl;
    bool have_button = false;
    int delay = 20;
    std::string text, name = "rockcat";
    bool have_text = false;
    for (size_t i = 0; i < o.args.size(); ++i) {
        const std::string &a = o.args[i];
        auto next = [&](const char *what) -> const std::string & {
            if (i + 1 >= o.args.size())
                fail("%s needs a value", what);
            return o.args[++i];
        };
        if (a == "-b" || a == "--button") {
            sl = parse_button(next("-b"));
            have_button = true;
        }
        else if (a == "--text") {
            text = next("--text");
            have_text = true;
        } else if (a == "--delay")
            delay = static_cast<int>(parse_long(next("--delay"), "delay"));
        else if (a == "--name")
            name = next("--name");
        else
            fail("unknown macro option '%s'", a.c_str());
    }
    if (!have_button)
        fail("macro needs -b BUTTON");

    rockcat_device *dev = open_device(o);
    check_slot(dev, sl);
    rockcat_profile *p = read_profile(dev, o);
    static rockcat_macro m;
    if (!have_text) {
        check(rockcat_profile_get_macro(dev, p, sl.layer, sl.input, &m), "read macro");
        std::printf("Macro '%s' / '%s' (profile %d, button %s), mode %d, loop %d, %d events\n",
                    m.set_name, m.name, rockcat_profile_index(p) + 1, slot_name(dev, sl).c_str(), m.mode, m.loop, m.count);
        for (int i = 0; i < m.count; ++i)
            std::printf("  key 0x%02x %-7s then wait %d ms\n", m.keys[i].key,
                        m.keys[i].action == ROCKCAT_KEY_PRESS ? "press" : "release", m.keys[i].delay_ms);
        rockcat_profile_free(p);
        rockcat_close(dev);
        return 0;
    }

    std::memset(&m, 0, sizeof m);
    m.loop = 1;
    m.mode = ROCKCAT_MACRO_ON_PRESS;
    std::snprintf(m.set_name, sizeof m.set_name, "rockcat");
    std::snprintf(m.name, sizeof m.name, "%s", name.c_str());
    auto push = [&](uint8_t key, uint8_t action) {
        if (m.count >= ROCKCAT_MACRO_KEYSTROKES)
            fail("macro too long");
        m.keys[m.count++] = rockcat_keystroke{key, action, static_cast<uint16_t>(delay)};
    };
    for (char ch : text) {
        uint8_t usage;
        bool shift;
        if (!ascii_to_hid(ch, &usage, &shift))
            fail("cannot type character '%s'", std::string(1, ch).c_str());
        if (shift)
            push(0xe1, ROCKCAT_KEY_PRESS);
        push(usage, ROCKCAT_KEY_PRESS);
        push(usage, ROCKCAT_KEY_RELEASE);
        if (shift)
            push(0xe1, ROCKCAT_KEY_RELEASE);
    }
    rockcat_assignment bind{ROCKCAT_BT_MACRO, 0, 0};
    check(rockcat_profile_set_macro(p, sl.layer, sl.input, &m), "store macro");
    check(rockcat_profile_set_input(p, sl.layer, sl.input, &bind), "bind macro");
    check(rockcat_profile_write(dev, p), "write profile");
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

int cmd_direct(const options &o)
{
    if (o.args.empty())
        fail("direct needs off or colours");
    rockcat_device *dev = open_device(o);
    if (o.args[0] == "off") {
        check(rockcat_direct_end(dev), "leave direct mode");
    } else {
        size_t leds = static_cast<size_t>(rockcat_get_caps(dev)->led_count);
        std::vector<rockcat_color> colors(leds);
        std::string spec = o.args[0];
        std::vector<std::string> parts;
        size_t pos = 0;
        while (true) {
            size_t c = spec.find(',', pos);
            parts.push_back(spec.substr(pos, c == std::string::npos ? std::string::npos : c - pos));
            if (c == std::string::npos)
                break;
            pos = c + 1;
        }
        if (parts.size() != 1 && parts.size() != leds)
            fail("%s", ("give one colour or " + std::to_string(leds) + " comma separated colours").c_str());
        for (size_t i = 0; i < leds; ++i)
            if (!parse_color(parts[parts.size() == 1 ? 0 : i], &colors[i]))
                fail("invalid colour '%s'", parts[parts.size() == 1 ? 0 : i].c_str());
        check(rockcat_direct_begin(dev), "enter direct mode");
        check(rockcat_direct_frame(dev, colors.data(), static_cast<int>(leds)), "send colours");
    }
    rockcat_close(dev);
    return 0;
}

std::vector<uint8_t> export_profile(const rockcat_profile *p)
{
    int n = rockcat_profile_export(p, nullptr, 0);
    if (n < 0)
        die("export profile", n);
    std::vector<uint8_t> blob(static_cast<size_t>(n));
    rockcat_profile_export(p, blob.data(), blob.size());
    return blob;
}

int cmd_dump(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_profile *p = read_profile(dev, o);
    std::vector<uint8_t> blob = export_profile(p);
    // 8 bytes of our own header (RCPF, version, product id, profile), after that its the reports as the device has them
    hexdump("Profile as it goes into a backup", blob.data(), blob.size());
    rockcat_profile_free(p);
    rockcat_close(dev);
    return 0;
}

volatile sig_atomic_t g_interrupted = 0;
void on_signal(int) { g_interrupted = 1; }

int cmd_shift_light(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_device_info info;
    rockcat_get_info(dev, &info);
    rockcat_profile *p = read_profile(dev, o);
    uint8_t profile = static_cast<uint8_t>(rockcat_profile_index(p));
    int by_engine = 0;
    rockcat_profile_get_effect(p, nullptr, &by_engine);
    rockcat_profile_free(p);
    rockcat_close(dev);
    rockcat_shift_lighting sl;
    check(rockcat_shift_lighting_load(info.product_id, profile, &sl), "load");
    for (const auto &a : o.args) {
        rockcat_color c;
        if (a == "on") sl.enabled = 1;
        else if (a == "off") sl.enabled = 0;
        else if (a == "solid") sl.style = ROCKCAT_SHIFT_SOLID;
        else if (a == "pulse") sl.style = ROCKCAT_SHIFT_PULSE;
        else if (parse_color(a, &c)) { sl.color = c; sl.enabled = 1; }
        else fail("unknown shift-light argument '%s'", a.c_str());
    }
    if (!o.args.empty())
        check(rockcat_shift_lighting_save(info.product_id, profile, &sl), "save");
    std::printf("Profile %d Easy-Shift lighting: %s, %s, #%02x%02x%02x@%d\n", profile + 1,
                sl.enabled ? "on" : "off", sl.style == ROCKCAT_SHIFT_PULSE ? "pulse" : "solid",
                sl.color.r, sl.color.g, sl.color.b, sl.color.a);
    if (sl.enabled && !by_engine)
        std::printf("Note: this profile's effect is rendered by the mouse, which always shows its own\n"
                    "light blue on Easy-Shift. Use `rockcat-cli -p %d set render=engine` to use this colour.\n",
                    profile + 1);
    else if (sl.enabled)
        std::printf("Shown by the lighting engine (GUI switch, `rockcat-cli engine` or the service).\n");
    return 0;
}

int cmd_tdcu(const options &o)
{
    rockcat_device *dev = open_device(o);
    rockcat_tdcu t;
    check(rockcat_read_tdcu(dev, &t), "read TDCU");
    std::printf("TDCU mode %d (%s), state %d, value 0x%04x\n", t.mode,
                t.mode == 2 ? "custom calibration" : "factory", t.state, t.value);
    rockcat_close(dev);
    return 0;
}

int cmd_calibrate(const options &o)
{
    rockcat_device *dev = open_device(o);
    std::printf("TDCU calibration\n"
                "Put the mouse on the surface you use. After pressing Enter, keep moving\n"
                "it in large circles and figure-eights until calibration completes.\n"
                "Press Enter to start...");
    std::fflush(stdout);
    std::getchar();
    check(rockcat_calibration_start(dev), "start calibration");
    std::printf("Calibrating - keep moving the mouse");
    std::fflush(stdout);
    int finished = 0;
    for (int i = 0; i < 600 && !finished; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (i < 10)
            continue; // give the sensor time to enter calibration
        check(rockcat_calibration_poll(dev, &finished), "calibration");
        if (i % 10 == 0) {
            std::printf(".");
            std::fflush(stdout);
        }
    }
    std::printf("\n");
    if (!finished) {
        rockcat_calibration_finish(dev, 0);
        fail("calibration did not finish within 60 s, discarded");
    }
    rockcat_tdcu t;
    check(rockcat_read_tdcu(dev, &t), "read TDCU");
    std::printf("Calibration finished (value 0x%04x). Save it? [Y/n] ", t.value);
    std::fflush(stdout);
    int c = std::getchar();
    if (c == '\n')
        c = std::getchar();
    bool save = !(c == 'n' || c == 'N');
    check(rockcat_calibration_finish(dev, save ? 1 : 0), "finish calibration");
    std::printf(save ? "Saved.\n" : "Discarded.\n");
    rockcat_close(dev);
    return 0;
}

int cmd_engine(const options &o)
{
    rockcat_engine_mode mode = ROCKCAT_ENGINE_AUTO;
    for (size_t i = 0; i < o.args.size(); ++i) {
        if (o.args[i] == "--mode" && i + 1 < o.args.size()) {
            const std::string &m = o.args[++i];
            mode = m == "aimo" ? ROCKCAT_ENGINE_AIMO : m == "heartbeat" ? ROCKCAT_ENGINE_HEARTBEAT
                 : m == "auto" ? ROCKCAT_ENGINE_AUTO : (fail("unknown mode '%s'", m.c_str()), mode);
        } else {
            fail("unknown engine option '%s'", o.args[i].c_str());
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    rockcat_engine *e = nullptr;
    int r = rockcat_engine_start(o.device, mode, &e);
    if (r == ROCKCAT_ERR_BUSY)
        fail("another lighting engine is already running (GUI or service)");
    check(r, "start engine");
    std::printf("Lighting engine running (%s mode), Ctrl+C to stop.\n",
                mode == ROCKCAT_ENGINE_AUTO ? "auto" : mode == ROCKCAT_ENGINE_AIMO ? "aimo" : "heartbeat");
    int last_effect = -1, last_rendering = -1, last_error = 0;
    while (!g_interrupted) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        rockcat_engine_status st;
        rockcat_engine_get_status(e, &st);
        if (st.last_error != last_error && st.last_error != ROCKCAT_OK)
            std::fprintf(stderr, "engine: %s (retrying)\n", rockcat_strerror(st.last_error));
        last_error = st.last_error;
        if (st.profile != 0xff && (st.effect != last_effect || st.rendering != last_rendering)) {
            const char *n = rockcat_effect_name(st.effect);
            std::printf("profile %d: %s - %s\n", st.profile + 1, n ? n : "unknown",
                        st.rendering ? "rendered by rockcat" : "rendered by the mouse");
            std::fflush(stdout);
            last_effect = st.effect;
            last_rendering = st.rendering;
        }
    }
    rockcat_engine_stop(e);
    std::printf("Lighting handed back to the mouse.\n");
    return 0;
}

// Backup format: "RSWB" v1, then per profile 126 settings bytes + 72 button bytes.
constexpr char BACKUP_MAGIC[4] = {'R', 'S', 'W', 'B'};
// per profile in a version 1 backup: the 126 byte settings report and 24 buttons at 3 bytes each
constexpr size_t BACKUP_PROFILE = 126 + 3 * 24;

// backup file: "RSWB", format version, then for version 2 the number of profiles and per profile a 16 bit length + the exported profile
// version 1 was the Kone AIMO Remastered only layout from rossat-swarm (5 times settings report + button table, no lengths), we still read it
int cmd_backup(const options &o)
{
    if (o.args.size() != 1)
        fail("backup needs a FILE");
    rockcat_device *dev = open_device(o);
    int count = rockcat_get_caps(dev)->profile_count;
    std::vector<uint8_t> out = {'R', 'S', 'W', 'B', 2, static_cast<uint8_t>(count)};
    for (int i = 0; i < count; ++i) {
        rockcat_profile *p = nullptr;
        check(rockcat_profile_read(dev, i, &p), "read profile");
        std::vector<uint8_t> blob = export_profile(p);
        out.push_back(static_cast<uint8_t>(blob.size() & 0xff));
        out.push_back(static_cast<uint8_t>(blob.size() >> 8));
        out.insert(out.end(), blob.begin(), blob.end());
        rockcat_profile_free(p);
    }
    rockcat_close(dev);
    FILE *f = std::fopen(o.args[0].c_str(), "wb");
    if (!f || std::fwrite(out.data(), 1, out.size(), f) != out.size())
        fail("cannot write '%s'", o.args[0].c_str());
    std::fclose(f);
    std::printf("Saved %d profiles to %s\n", count, o.args[0].c_str());
    return 0;
}

int cmd_restore(const options &o)
{
    if (o.args.size() != 1)
        fail("restore needs a FILE");
    FILE *f = std::fopen(o.args[0].c_str(), "rb");
    if (!f)
        fail("cannot open '%s'", o.args[0].c_str());
    std::vector<uint8_t> in(1 << 20);
    in.resize(std::fread(in.data(), 1, in.size(), f));
    std::fclose(f);
    if (in.size() < 6 || std::memcmp(in.data(), BACKUP_MAGIC, 4) != 0 || (in[4] != 1 && in[4] != 2))
        fail("'%s' is not a rockcat backup", o.args[0].c_str());

    rockcat_device *dev = open_device(o);
    rockcat_device_info info;
    rockcat_get_info(dev, &info);
    std::vector<std::vector<uint8_t>> blobs;
    if (in[4] == 1) {
        // old layout has no idea which mouse it came from, it only ever came from this one
        if (info.product_id != ROCKCAT_PID_KONE_AIMO_REMASTERED || in.size() != 5 + 5 * BACKUP_PROFILE)
            fail("'%s' is an old Kone AIMO Remastered backup and doesnt fit this device", o.args[0].c_str());
        for (uint8_t i = 0; i < 5; ++i) {
            std::vector<uint8_t> blob = {'R', 'C', 'P', 'F', 1, static_cast<uint8_t>(info.product_id & 0xff),
                                         static_cast<uint8_t>(info.product_id >> 8), i};
            blob.insert(blob.end(), in.begin() + 5 + i * BACKUP_PROFILE, in.begin() + 5 + (i + 1) * BACKUP_PROFILE);
            blobs.push_back(blob);
        }
    } else {
        size_t pos = 6;
        for (int i = 0; i < in[5]; ++i) {
            if (pos + 2 > in.size())
                fail("'%s' is cut off", o.args[0].c_str());
            size_t len = in[pos] | (in[pos + 1] << 8);
            pos += 2;
            if (pos + len > in.size())
                fail("'%s' is cut off", o.args[0].c_str());
            blobs.emplace_back(in.begin() + static_cast<long>(pos), in.begin() + static_cast<long>(pos + len));
            pos += len;
        }
    }
    int n = 0;
    for (const auto &blob : blobs) {
        rockcat_profile *p = nullptr;
        if (rockcat_profile_import(dev, blob.data(), blob.size(), &p) != ROCKCAT_OK)
            fail("'%s' was made with a different device, not restoring it onto this one", o.args[0].c_str());
        check(rockcat_profile_set_index(p, n), "restore profile");
        check(rockcat_profile_write(dev, p), "write profile");
        rockcat_profile_free(p);
        ++n;
    }
    rockcat_close(dev);
    std::printf("Restored %d profiles from %s\n", n, o.args[0].c_str());
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    g_prog = argv[0] && std::strrchr(argv[0], '/') ? std::strrchr(argv[0], '/') + 1 : "rockcat-cli";
    options o;
    std::string cmd;
    int first = 1;
    if (argc > 1 && std::strcmp(argv[1], "capture") == 0) {
        if (argc < 4)
            fail("usage: capture FILE COMMAND...");
        // the library appends, so several captures into the same file make one long recording
        setenv("ROCKCAT_CAPTURE", argv[2], 1);
        first = 3;
    }
    for (int i = first; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(stdout);
            return 0;
        } else if (a == "--version") {
            std::printf("rockcat-cli %s\n", rockcat_version());
            return 0;
        } else if (a == "-d" || a == "--device") {
            if (++i >= argc)
                fail("--device needs a path");
            o.device = argv[i];
        } else if (a == "-p" || a == "--profile") {
            if (++i >= argc)
                fail("--profile needs a number");
            long n = parse_long(argv[i], "profile");
            if (n < 1 || n > 255)
                fail("profile numbers start at 1");
            o.profile = static_cast<int>(n - 1);
        } else if (cmd.empty()) {
            cmd = a;
        } else {
            o.args.push_back(a);
        }
    }

    int rc;
    if (cmd.empty()) {
        usage(stderr);
        return 2;
    } else if (cmd == "list") rc = cmd_list();
    else if (cmd == "info") rc = cmd_info(o);
    else if (cmd == "profile") rc = cmd_profile(o);
    else if (cmd == "show") rc = cmd_show(o);
    else if (cmd == "set") rc = cmd_set(o);
    else if (cmd == "buttons") rc = cmd_buttons(o);
    else if (cmd == "bind") rc = cmd_bind(o);
    else if (cmd == "reset-buttons") rc = cmd_reset_buttons(o);
    else if (cmd == "types") rc = cmd_types();
    else if (cmd == "macro") rc = cmd_macro(o);
    else if (cmd == "direct") rc = cmd_direct(o);
    else if (cmd == "dump") rc = cmd_dump(o);
    else if (cmd == "backup") rc = cmd_backup(o);
    else if (cmd == "calibrate") rc = cmd_calibrate(o);
    else if (cmd == "tdcu") rc = cmd_tdcu(o);
    else if (cmd == "shift-light") rc = cmd_shift_light(o);
    else if (cmd == "engine") rc = cmd_engine(o);
    else if (cmd == "restore") rc = cmd_restore(o);
    else if (cmd == "raw") rc = cmd_raw(o);
    else {
        std::fprintf(stderr, "%s: unknown command '%s'\n\n", g_prog, cmd.c_str());
        usage(stderr);
        return 2;
    }
    rockcat_exit();
    return rc;
}
