// Human readable names and factory defaults for the Kone AIMO Remastered.

#include "internal.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct type_entry {
    uint8_t type;
    const char *key;   // CLI spelling
    const char *label; // UI spelling
};

constexpr type_entry TYPES[] = {
    {ROCKCAT_BT_UNUSED,            "unused",            "Unassigned"},
    {ROCKCAT_BT_CLICK,             "click",             "Click"},
    {ROCKCAT_BT_MENU,              "menu",              "Menu"},
    {ROCKCAT_BT_UNIVERSAL_SCROLL,  "universal-scroll",  "Universal scrolling"},
    {ROCKCAT_BT_DOUBLE_CLICK,      "double-click",      "Double click"},
    {ROCKCAT_BT_SHORTCUT,          "shortcut",          "Shortcut"},
    {ROCKCAT_BT_DISABLED,          "disabled",          "Disabled"},
    {ROCKCAT_BT_BROWSER_FORWARD,   "forward",           "Browser forward"},
    {ROCKCAT_BT_BROWSER_BACKWARD,  "backward",          "Browser backward"},
    {ROCKCAT_BT_TILT_LEFT,         "tilt-left",         "Tilt left"},
    {ROCKCAT_BT_TILT_RIGHT,        "tilt-right",        "Tilt right"},
    {ROCKCAT_BT_SCROLL_UP,         "scroll-up",         "Scroll up"},
    {ROCKCAT_BT_SCROLL_DOWN,       "scroll-down",       "Scroll down"},
    {ROCKCAT_BT_QUICKLAUNCH,       "quicklaunch",       "Quicklaunch"},
    {ROCKCAT_BT_PROFILE_CYCLE,     "profile-cycle",     "Profile cycle"},
    {ROCKCAT_BT_PROFILE_UP,        "profile-up",        "Profile up"},
    {ROCKCAT_BT_PROFILE_DOWN,      "profile-down",      "Profile down"},
    {ROCKCAT_BT_DPI_CYCLE,         "dpi-cycle",         "DPI cycle"},
    {ROCKCAT_BT_DPI_UP,            "dpi-up",            "DPI up"},
    {ROCKCAT_BT_DPI_DOWN,          "dpi-down",          "DPI down"},
    {ROCKCAT_BT_WINDOWS_KEY,       "windows-key",       "Windows key"},
    {ROCKCAT_BT_OPEN_DRIVER,       "open-driver",       "Open driver"},
    {ROCKCAT_BT_OPEN_PLAYER,       "open-player",       "Open media player"},
    {ROCKCAT_BT_PREV_TRACK,        "prev-track",        "Previous track"},
    {ROCKCAT_BT_NEXT_TRACK,        "next-track",        "Next track"},
    {ROCKCAT_BT_PLAY_PAUSE,        "play-pause",        "Play / pause"},
    {ROCKCAT_BT_STOP,              "stop",              "Stop"},
    {ROCKCAT_BT_MUTE,              "mute",              "Mute"},
    {ROCKCAT_BT_VOLUME_UP,         "volume-up",         "Volume up"},
    {ROCKCAT_BT_VOLUME_DOWN,       "volume-down",       "Volume down"},
    {ROCKCAT_BT_MACRO,             "macro",             "Macro"},
    {ROCKCAT_BT_TIMER,             "timer",             "Timer"},
    {ROCKCAT_BT_TIMER_STOP,        "timer-stop",        "Timer stop"},
    {ROCKCAT_BT_EASYAIM_1,         "easyaim-1",         "EasyAim DPI level 1"},
    {ROCKCAT_BT_EASYAIM_2,         "easyaim-2",         "EasyAim DPI level 2"},
    {ROCKCAT_BT_EASYAIM_3,         "easyaim-3",         "EasyAim DPI level 3"},
    {ROCKCAT_BT_EASYAIM_4,         "easyaim-4",         "EasyAim DPI level 4"},
    {ROCKCAT_BT_EASYAIM_5,         "easyaim-5",         "EasyAim DPI level 5"},
    {ROCKCAT_BT_EASYSHIFT,         "easyshift",         "Easy-Shift[+]"},
    {ROCKCAT_BT_EASYSHIFT_OTHER,   "easyshift-other",   "Easy-Shift[+] other devices"},
    {ROCKCAT_BT_EASYSHIFT_LOCK,    "easyshift-lock",    "Easy-Shift[+] lock"},
    {ROCKCAT_BT_EASYSHIFT_ALL,     "easyshift-all",     "Easy-Shift[+] all devices"},
    {ROCKCAT_BT_EASYSHIFT_LOCK_ALL,"easyshift-lock-all","Easy-Shift[+] lock all devices"},
    {ROCKCAT_BT_EASYWHEEL_PROFILE, "easywheel-profile", "EasyWheel: switch profile"},
    {ROCKCAT_BT_EASYWHEEL_DPI,     "easywheel-dpi",     "EasyWheel: change DPI"},
    {ROCKCAT_BT_EASYWHEEL_VOLUME,  "easywheel-volume",  "EasyWheel: volume"},
    {ROCKCAT_BT_EASYWHEEL_ALT_TAB, "easywheel-alt-tab", "EasyWheel: alt+tab"},
    {ROCKCAT_BT_HOME,              "home",              "Home"},
    {ROCKCAT_BT_END,               "end",               "End"},
    {ROCKCAT_BT_PAGE_UP,           "page-up",           "Page up"},
    {ROCKCAT_BT_PAGE_DOWN,         "page-down",         "Page down"},
    {ROCKCAT_BT_LEFT_CTRL,         "left-ctrl",         "Left Ctrl"},
    {ROCKCAT_BT_LEFT_SHIFT,        "left-shift",        "Left Shift"},
    {ROCKCAT_BT_LEFT_ALT,          "left-alt",          "Left Alt"},
    {ROCKCAT_BT_EASYAIM_CUSTOM,    "easyaim-custom",    "EasyAim custom DPI"},
    {ROCKCAT_BT_PROFILE_1,         "profile-1",         "Profile 1"},
    {ROCKCAT_BT_PROFILE_2,         "profile-2",         "Profile 2"},
    {ROCKCAT_BT_PROFILE_3,         "profile-3",         "Profile 3"},
    {ROCKCAT_BT_PROFILE_4,         "profile-4",         "Profile 4"},
    {ROCKCAT_BT_PROFILE_5,         "profile-5",         "Profile 5"},
    {ROCKCAT_BT_EASYAIM_200,       "easyaim-200",       "EasyAim 200"},
    {ROCKCAT_BT_BRIGHTNESS_UP,     "brightness-up",     "Brightness step up (engine)"},
    {ROCKCAT_BT_BRIGHTNESS_DOWN,   "brightness-down",   "Brightness step down (engine)"},
};

struct key_entry {
    uint8_t usage;
    const char *name;
};

constexpr key_entry KEYS[] = {
    {0x28, "enter"}, {0x29, "esc"}, {0x2a, "backspace"}, {0x2b, "tab"}, {0x2c, "space"},
    {0x2d, "minus"}, {0x2e, "equal"}, {0x2f, "leftbrace"}, {0x30, "rightbrace"}, {0x31, "backslash"},
    {0x33, "semicolon"}, {0x34, "apostrophe"}, {0x35, "grave"}, {0x36, "comma"}, {0x37, "dot"},
    {0x38, "slash"}, {0x39, "capslock"}, {0x46, "printscreen"}, {0x47, "scrolllock"},
    {0x48, "pause"}, {0x49, "insert"}, {0x4a, "home"}, {0x4b, "pageup"}, {0x4c, "delete"},
    {0x4d, "end"}, {0x4e, "pagedown"}, {0x4f, "right"}, {0x50, "left"}, {0x51, "down"},
    {0x52, "up"}, {0x53, "numlock"}, {0x54, "kpslash"}, {0x55, "kpasterisk"}, {0x56, "kpminus"},
    {0x57, "kpplus"}, {0x58, "kpenter"}, {0x63, "kpdot"}, {0x65, "menu"},
    // the iso keys: # next to enter and < > next to left shift
    {0x32, "hash"}, {0x64, "102nd"},
    {0x59, "kp1"}, {0x5a, "kp2"}, {0x5b, "kp3"}, {0x5c, "kp4"}, {0x5d, "kp5"}, {0x5e, "kp6"}, {0x5f, "kp7"},
    {0x60, "kp8"}, {0x61, "kp9"}, {0x62, "kp0"},
    // keys of japanese, korean and brazilian boards
    {0x87, "ro"}, {0x88, "katakana"}, {0x89, "yen"}, {0x8a, "henkan"}, {0x8b, "muhenkan"},
    // the modifiers as keys of their own, for making one key into another ("bind capslock lctrl")
    {0xe0, "lctrl"}, {0xe1, "lshift"}, {0xe2, "lalt"}, {0xe3, "lwin"},
    {0xe4, "rctrl"}, {0xe5, "rshift"}, {0xe6, "ralt"}, {0xe7, "rwin"},
};

std::string lower(const std::string &in)
{
    std::string out;
    for (char c : in)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int key_from_name(const std::string &n)
{
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z')
        return 0x04 + (n[0] - 'a');
    if (n.size() == 1 && n[0] >= '1' && n[0] <= '9')
        return 0x1e + (n[0] - '1');
    if (n == "0")
        return 0x27;
    if (n.size() >= 2 && n[0] == 'f') {
        char *end = nullptr;
        long f = std::strtol(n.c_str() + 1, &end, 10);
        if (*end == '\0' && f >= 1 && f <= 12)
            return static_cast<int>(0x3a + f - 1);
        if (*end == '\0' && f >= 13 && f <= 24)
            return static_cast<int>(0x68 + f - 13);
    }
    if (n.size() == 3 && n[0] == 'k' && n[1] == 'p' && n[2] >= '0' && n[2] <= '9')
        return n[2] == '0' ? 0x62 : 0x59 + (n[2] - '1');
    for (const auto &k : KEYS)
        if (n == k.name)
            return k.usage;
    if (n.rfind("0x", 0) == 0) {
        char *end = nullptr;
        long v = std::strtol(n.c_str(), &end, 16);
        if (*end == '\0' && v > 0 && v <= 0xff)
            return static_cast<int>(v);
    }
    return -1;
}

} // namespace

extern "C" {

int rockcat_parse_shortcut(const char *text, uint8_t *modifiers, uint8_t *key)
{
    if (!text || !modifiers || !key)
        return ROCKCAT_ERR_ARGUMENT;
    std::string s = lower(text);
    uint8_t mods = 0;
    int k = -1;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t plus = s.find('+', pos);
        std::string part = s.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
        if (part.empty() && plus != std::string::npos) { // "ctrl++" -> plus key
            part = "equal";
        }
        if (part == "shift") mods |= ROCKCAT_MOD_SHIFT;
        else if (part == "ctrl" || part == "control") mods |= ROCKCAT_MOD_CTRL;
        else if (part == "alt") mods |= ROCKCAT_MOD_ALT;
        else if (part == "win" || part == "super" || part == "meta") mods |= ROCKCAT_MOD_WIN;
        else {
            if (k >= 0)
                return ROCKCAT_ERR_ARGUMENT; // two non-modifier keys
            k = key_from_name(part);
            if (k < 0)
                return ROCKCAT_ERR_ARGUMENT;
        }
        if (plus == std::string::npos)
            break;
        pos = plus + 1;
    }
    if (k < 0)
        return ROCKCAT_ERR_ARGUMENT;
    *modifiers = mods;
    *key = static_cast<uint8_t>(k);
    return ROCKCAT_OK;
}

void rockcat_format_shortcut(uint8_t modifiers, uint8_t key, char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    std::string out;
    if (modifiers & ROCKCAT_MOD_CTRL) out += "ctrl+";
    if (modifiers & ROCKCAT_MOD_SHIFT) out += "shift+";
    if (modifiers & ROCKCAT_MOD_ALT) out += "alt+";
    if (modifiers & ROCKCAT_MOD_WIN) out += "win+";
    char tmp[8];
    if (key >= 0x04 && key <= 0x1d) out += static_cast<char>('a' + key - 0x04);
    else if (key >= 0x1e && key <= 0x26) out += static_cast<char>('1' + key - 0x1e);
    else if (key == 0x27) out += '0';
    else if (key >= 0x3a && key <= 0x45) { std::snprintf(tmp, sizeof tmp, "f%d", key - 0x3a + 1); out += tmp; }
    else if (key >= 0x68 && key <= 0x73) { std::snprintf(tmp, sizeof tmp, "f%d", key - 0x68 + 13); out += tmp; }
    else {
        const char *name = nullptr;
        for (const auto &k : KEYS)
            if (k.usage == key)
                name = k.name;
        if (name) out += name;
        else { std::snprintf(tmp, sizeof tmp, "0x%02x", key); out += tmp; }
    }
    std::snprintf(buf, len, "%s", out.c_str());
}

const char *rockcat_button_type_name(uint8_t type)
{
    for (const auto &t : TYPES)
        if (t.type == type)
            return t.label;
    return nullptr;
}

int rockcat_button_type_from_string(const char *s)
{
    if (!s || !*s)
        return -1;
    std::string k;
    for (const char *p = s; *p; ++p)
        k += (*p == '_' || *p == ' ') ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    for (const auto &t : TYPES)
        if (k == t.key)
            return t.type;
    char *end = nullptr;
    long v = std::strtol(s, &end, 0);
    if (end && *end == '\0' && v >= 0 && v <= 0xff)
        return static_cast<int>(v);
    return -1;
}

int rockcat_button_type_count(void)
{
    return static_cast<int>(sizeof TYPES / sizeof TYPES[0]);
}

int rockcat_button_type_at(int i)
{
    return i >= 0 && i < rockcat_button_type_count() ? TYPES[i].type : -1;
}

const char *rockcat_button_type_key(uint8_t type)
{
    for (const auto &t : TYPES)
        if (t.type == type)
            return t.key;
    return nullptr;
}

} // extern "C"
