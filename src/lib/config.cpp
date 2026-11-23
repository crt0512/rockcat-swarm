// Host-side settings the mouse cannot store (Easy-Shift[+] lighting).
// Format: $XDG_CONFIG_HOME/rockcat/<pid>.ini with one [profileN] section per
// profile and keys shift_light=on|off, shift_style=solid|pulse,
// shift_color=#rrggbb, shift_brightness=0-255.

#include "internal.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <sys/stat.h>

namespace {

// just a sanity limit so garbage doesnt end up as a section name, no device has anywhere near this many
constexpr int MAX_PROFILES = 32;

using ini = std::map<std::string, std::map<std::string, std::string>>;

std::string config_base()
{
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return xdg;
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config";
}

const std::string &config_dir()
{
    static const std::string dir = [] {
        std::string now = config_base() + "/rockcat", old = config_base() + "/rossat";
        struct stat st;
        // we used to be called rossat, drag the old config dir along once so nobody loses their settings
        if (::stat(now.c_str(), &st) != 0 && ::stat(old.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            std::rename(old.c_str(), now.c_str());
        return now;
    }();
    return dir;
}

std::string config_path(uint16_t pid)
{
    char name[32];
    std::snprintf(name, sizeof name, "/%04x.ini", pid);
    return config_dir() + name;
}

ini read_ini(const std::string &path)
{
    ini out;
    std::ifstream f(path);
    std::string line, section;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        auto eq = line.find('=');
        if (eq != std::string::npos)
            out[section][line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

bool write_ini(const std::string &path, const ini &data)
{
    // on a fresh account ~/.config itself might not be there yet
    ::mkdir(config_base().c_str(), 0755);
    ::mkdir(config_dir().c_str(), 0755);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f)
            return false;
        f << "# rockcat-swarm host-side settings\n";
        for (const auto &sec : data) {
            f << "[" << sec.first << "]\n";
            for (const auto &kv : sec.second)
                f << kv.first << "=" << kv.second << "\n";
            f << "\n";
        }
        if (!f)
            return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string section_name(uint8_t profile)
{
    return "profile" + std::to_string(profile + 1);
}

} // namespace

extern "C" const char *rockcat_config_dir(void)
{
    return config_dir().c_str();
}

extern "C" int rockcat_shift_lighting_load(uint16_t pid, uint8_t profile, rockcat_shift_lighting *out)
{
    if (!out || profile >= MAX_PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    // default: off, solid ROCCAT blue
    *out = rockcat_shift_lighting{0, ROCKCAT_SHIFT_SOLID, rockcat_color{255, 0x65, 0xc3, 0xf7}};
    ini data = read_ini(config_path(pid));
    auto sec = data.find(section_name(profile));
    if (sec == data.end())
        return ROCKCAT_OK;
    auto get = [&](const char *k) -> std::string {
        auto it = sec->second.find(k);
        return it == sec->second.end() ? "" : it->second;
    };
    out->enabled = get("shift_light") == "on";
    out->style = get("shift_style") == "pulse" ? ROCKCAT_SHIFT_PULSE : ROCKCAT_SHIFT_SOLID;
    std::string c = get("shift_color");
    if (c.size() == 7 && c[0] == '#') {
        unsigned long v = std::strtoul(c.c_str() + 1, nullptr, 16);
        out->color.r = (v >> 16) & 0xff;
        out->color.g = (v >> 8) & 0xff;
        out->color.b = v & 0xff;
    }
    std::string b = get("shift_brightness");
    if (!b.empty())
        out->color.a = static_cast<uint8_t>(std::strtoul(b.c_str(), nullptr, 10) & 0xff);
    return ROCKCAT_OK;
}

extern "C" int rockcat_shift_lighting_save(uint16_t pid, uint8_t profile, const rockcat_shift_lighting *in)
{
    if (!in || profile >= MAX_PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    std::string path = config_path(pid);
    ini data = read_ini(path);
    auto &sec = data[section_name(profile)];
    char color[8];
    std::snprintf(color, sizeof color, "#%02x%02x%02x", in->color.r, in->color.g, in->color.b);
    sec["shift_light"] = in->enabled ? "on" : "off";
    sec["shift_style"] = in->style == ROCKCAT_SHIFT_PULSE ? "pulse" : "solid";
    sec["shift_color"] = color;
    sec["shift_brightness"] = std::to_string(in->color.a);
    return write_ini(path, data) ? ROCKCAT_OK : ROCKCAT_ERR_IO;
}

// anything else somebody wants to remember per device and profile. "" when its not there
std::string config_get(uint16_t pid, uint8_t profile, const std::string &key)
{
    ini data = read_ini(config_path(pid));
    auto sec = data.find(section_name(profile));
    if (sec == data.end())
        return "";
    auto it = sec->second.find(key);
    return it == sec->second.end() ? "" : it->second;
}

bool config_set(uint16_t pid, uint8_t profile, const std::string &key, const std::string &value)
{
    std::string path = config_path(pid);
    ini data = read_ini(path);
    data[section_name(profile)][key] = value;
    return write_ini(path, data);
}

// when the settings of that device were last written, so somebody who cached them knows when to look again. 0 = never
long long config_stamp(uint16_t pid)
{
    struct stat st;
    if (::stat(config_path(pid).c_str(), &st) != 0)
        return 0;
    return static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
}

extern "C" int rockcat_engine_effect_load(uint16_t pid, uint8_t profile, uint8_t *effect)
{
    if (!effect || profile >= MAX_PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    *effect = ROCKCAT_EFFECT_AIMO;
    ini data = read_ini(config_path(pid));
    auto sec = data.find(section_name(profile));
    if (sec != data.end()) {
        auto it = sec->second.find("engine_effect");
        if (it != sec->second.end() && !it->second.empty())
            *effect = static_cast<uint8_t>(std::strtoul(it->second.c_str(), nullptr, 10) & 0xff);
    }
    return ROCKCAT_OK;
}

extern "C" int rockcat_engine_effect_save(uint16_t pid, uint8_t profile, uint8_t effect)
{
    if (profile >= MAX_PROFILES)
        return ROCKCAT_ERR_ARGUMENT;
    std::string path = config_path(pid);
    ini data = read_ini(path);
    data[section_name(profile)]["engine_effect"] = std::to_string(effect);
    return write_ini(path, data) ? ROCKCAT_OK : ROCKCAT_ERR_IO;
}

extern "C" void rockcat_effective_effect(uint16_t pid, const rockcat_settings *s, uint8_t *effect, int *by_engine)
{
    uint8_t e = s->effect;
    int engine = rockcat_effect_is_software(e);
    if (e == ROCKCAT_EFFECT_AIMO)
        rockcat_engine_effect_load(pid, s->profile, &e);
    if (effect)
        *effect = e;
    if (by_engine)
        *by_engine = engine;
}

extern "C" int rockcat_select_effect(uint16_t pid, rockcat_settings *s, uint8_t effect, int by_engine)
{
    if (!s)
        return ROCKCAT_ERR_ARGUMENT;
    if (rockcat_effect_is_software(effect))
        by_engine = 1;
    if (!by_engine) {
        s->effect = effect;
        return ROCKCAT_OK;
    }
    s->effect = ROCKCAT_EFFECT_AIMO;
    return rockcat_engine_effect_save(pid, s->profile, effect);
}
