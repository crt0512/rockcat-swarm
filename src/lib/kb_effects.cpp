// see kb_effects.h. wave, ripple and heatmap are our take on three effects Swarm had (nobody here ever saw the originals),
// fire, drip and the idle / wake animations are our own. the equalizer listens to what the computer plays

#include "internal.h"
#include "kb_effects.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include <dlfcn.h>

namespace {

const double PI = 3.14159265358979323846;

struct rgb {
    double r, g, b;
};
rgb mix(rgb a, rgb b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
rgb scaled(rgb a, double f) { return {a.r * f, a.g * f, a.b * f}; }
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
rgb from_int(int c) { return {((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0}; }
rgb from_color(const rockcat_color &c) { return {c.r / 255.0, c.g / 255.0, c.b / 255.0}; }

rgb hue(double h)
{
    h = (h - std::floor(h)) * 6.0;
    double x = 1 - std::fabs(std::fmod(h, 2.0) - 1);
    switch (static_cast<int>(h)) {
    case 0: return {1, x, 0};
    case 1: return {x, 1, 0};
    case 2: return {0, 1, x};
    case 3: return {0, x, 1};
    case 4: return {x, 0, 1};
    default: return {1, 0, x};
    }
}

// a number between 0 and 1 for every whole number pair, always the same one
double hash2(int x, int y)
{
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / static_cast<double>(0xffffff);
}

// smooth noise, 0 to 1
double noise(double x, double y)
{
    int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    double fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    double a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

/* ---- what the computer is playing ----------------------------------------- */

// the equalizer needs the sound that goes to the speakers. that comes from pulseaudio (or pipewire pretending to be it)
// through libpulse-simple, which gets loaded when the effect is first used and not before: no sound server, no library,
// no problem, the effect then just shows its base colour. nothing that gets heard is kept, only how loud each band is
constexpr int BANDS = 24, CHUNK = 1024, RATE = 44100;

struct listener {
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> works{false};
    std::mutex lock;
    double band[BANDS] = {}; // how loud each band was at its loudest since somebody last took them
    double loud = 0;
    bool taken = true;       // somebody took them, the next chunk starts over
    unsigned chunks = 0;     // counts up with every bit of sound that got looked at

    static void fft(std::vector<double> &re, std::vector<double> &im)
    {
        const size_t n = re.size();
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (size_t len = 2; len <= n; len <<= 1) {
            double ang = -2 * PI / static_cast<double>(len);
            for (size_t i = 0; i < n; i += len)
                for (size_t k = 0; k < len / 2; ++k) {
                    double wr = std::cos(ang * static_cast<double>(k)), wi = std::sin(ang * static_cast<double>(k));
                    double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi, xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                    re[i + k + len / 2] = re[i + k] - xr;
                    im[i + k + len / 2] = im[i + k] - xi;
                    re[i + k] += xr;
                    im[i + k] += xi;
                }
        }
    }

    void run()
    {
        struct sample_spec {
            int format;
            uint32_t rate;
            uint8_t channels;
        } spec{3 /* signed 16 bit little endian */, RATE, 1};
        struct buffer_attr {
            uint32_t maxlength, tlength, prebuf, minreq, fragsize;
        } attr{static_cast<uint32_t>(-1), static_cast<uint32_t>(-1), static_cast<uint32_t>(-1), static_cast<uint32_t>(-1), CHUNK * 2};
        void *lib = dlopen("libpulse-simple.so.0", RTLD_NOW);
        if (!lib)
            return;
        auto pa_new = reinterpret_cast<void *(*)(const char *, const char *, int, const char *, const char *, const void *, const void *, const void *, int *)>(dlsym(lib, "pa_simple_new"));
        auto pa_read = reinterpret_cast<int (*)(void *, void *, size_t, int *)>(dlsym(lib, "pa_simple_read"));
        auto pa_free = reinterpret_cast<void (*)(void *)>(dlsym(lib, "pa_simple_free"));
        void *pa = nullptr;
        if (pa_new && pa_read && pa_free)
            // 2 = record. the "monitor" of the default output is what is being played
            pa = pa_new(nullptr, "rockcat-swarm", 2, "@DEFAULT_MONITOR@", "keyboard equalizer", &spec, nullptr, &attr, nullptr);
        if (!pa) {
            dlclose(lib);
            return;
        }
        works = true;
        std::vector<int16_t> pcm(CHUNK);
        std::vector<double> re(CHUNK), im(CHUNK);
        double peak = 1e-3;
        while (!stop) {
            if (pa_read(pa, pcm.data(), CHUNK * sizeof(int16_t), nullptr) < 0)
                break;
            double sum = 0;
            for (int i = 0; i < CHUNK; ++i) {
                double s = pcm[static_cast<size_t>(i)] / 32768.0;
                sum += s * s;
                re[static_cast<size_t>(i)] = s * (0.5 - 0.5 * std::cos(2 * PI * i / (CHUNK - 1)));
                im[static_cast<size_t>(i)] = 0;
            }
            fft(re, im);
            // bands from 50 Hz to 14 kHz, each as wide again as the one before by the same factor (the way ears hear it)
            double now[BANDS];
            double top = 0;
            for (int b = 0; b < BANDS; ++b) {
                double lo = 50 * std::pow(280.0, b / static_cast<double>(BANDS)), hi = 50 * std::pow(280.0, (b + 1) / static_cast<double>(BANDS));
                int from = std::max(1, static_cast<int>(lo * CHUNK / RATE)), to = std::max(from + 1, static_cast<int>(hi * CHUNK / RATE));
                double m = 0;
                for (int i = from; i < to && i < CHUNK / 2; ++i)
                    m = std::max(m, std::hypot(re[static_cast<size_t>(i)], im[static_cast<size_t>(i)]));
                // high bands carry less energy, lift them so the right half of the board has something to do
                now[b] = m * (1 + b * 0.18);
                top = std::max(top, now[b]);
            }
            // loudness sets itself: the loudest thing lately is "full", and that slowly forgets
            peak = std::max(top, peak * 0.995);
            std::lock_guard<std::mutex> g(lock);
            // as heard, nothing smoothed in here: how lazy or jumpy the bars are is for whoever draws them to say. the
            // loudest value since the last look is kept, so a short hit between two frames does not get lost
            for (int b = 0; b < BANDS; ++b) {
                double v = clamp01(now[b] / std::max(peak, 1e-3));
                band[b] = taken ? v : std::max(band[b], v);
            }
            taken = false;
            ++chunks;
            loud = loud * 0.7 + clamp01(std::sqrt(sum / CHUNK) * 6) * 0.3;
        }
        pa_free(pa);
        dlclose(lib);
        works = false;
    }

    void start()
    {
        if (!worker.joinable())
            worker = std::thread([this] { run(); });
    }
    ~listener()
    {
        stop = true;
        if (worker.joinable())
            worker.join();
    }
};

/* ---- the settings ---------------------------------------------------------- */

using R = rockcat_effect_option;

const R WAVE_OPTIONS[] = {
    {"speed", "Speed", ROCKCAT_OPTION_RANGE, 1, 30, 8, nullptr},
    {"width", "Stretch", ROCKCAT_OPTION_RANGE, 1, 12, 5, nullptr},
    {"direction", "Runs", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "Across, at an angle|Out from the middle|Into the middle"},
    {"angle", "Angle (0 = right, 90 = down)", ROCKCAT_OPTION_RANGE, 0, 359, 0, nullptr},
    {"colours", "Colours", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "Rainbow|Two of my own|Three of my own|Key colours, brightness waves"},
    {"min_level", "Lowest brightness", ROCKCAT_OPTION_RANGE, 0, 100, 15, nullptr},
    {"colour1", "Colour 1", ROCKCAT_OPTION_COLOR, 0, 0, 0xff0000, nullptr},
    {"colour2", "Colour 2", ROCKCAT_OPTION_COLOR, 0, 0, 0x0030ff, nullptr},
    {"colour3", "Colour 3", ROCKCAT_OPTION_COLOR, 0, 0, 0x00ff60, nullptr},
};
const R RIPPLE_OPTIONS[] = {
    {"base", "Base", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "Key colours|One colour"},
    {"base_colour", "Base colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x001a40, nullptr},
    {"base_level", "Base brightness", ROCKCAT_OPTION_RANGE, 0, 100, 18, nullptr},
    {"ring", "Ripple", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "One colour|Key colours, brighter|Rainbow"},
    {"ring_colour", "Ripple colour", ROCKCAT_OPTION_COLOR, 0, 0, 0xffffff, nullptr},
    {"ring_level", "Ripple brightness", ROCKCAT_OPTION_RANGE, 0, 100, 100, nullptr},
    {"speed", "Speed", ROCKCAT_OPTION_RANGE, 1, 30, 10, nullptr},
    {"width", "Ring width", ROCKCAT_OPTION_RANGE, 1, 12, 3, nullptr},
};
const R HEATMAP_OPTIONS[] = {
    {"base", "Cold keys", ROCKCAT_OPTION_CHOICE, 0, 0, 1, "Key colours|One colour"},
    {"base_colour", "Cold colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x000038, nullptr},
    {"base_level", "Cold brightness", ROCKCAT_OPTION_RANGE, 0, 100, 100, nullptr},
    {"cool", "Seconds to cool down", ROCKCAT_OPTION_RANGE, 2, 120, 14, nullptr},
    {"presses", "Presses until red hot", ROCKCAT_OPTION_RANGE, 1, 30, 6, nullptr},
};
const R FIRE_OPTIONS[] = {
    {"speed", "Speed", ROCKCAT_OPTION_RANGE, 1, 30, 10, nullptr},
    {"height", "Height", ROCKCAT_OPTION_RANGE, 1, 12, 7, nullptr},
    {"colours", "Flames", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "Fire|Gas flame|Poison green|Purple|My own colour"},
    {"colour", "My own colour", ROCKCAT_OPTION_COLOR, 0, 0, 0xff2000, nullptr},
    {"base", "Where nothing burns", ROCKCAT_OPTION_CHOICE, 0, 0, 1, "Key colours|One colour"},
    {"base_colour", "Background colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x000000, nullptr},
    {"base_level", "Background brightness", ROCKCAT_OPTION_RANGE, 0, 100, 40, nullptr},
    {"sparks", "Key presses throw sparks", ROCKCAT_OPTION_TOGGLE, 0, 1, 1, nullptr},
};
const R DRIP_OPTIONS[] = {
    {"drop_colour", "Drop colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x30a0ff, nullptr},
    {"pool_colour", "Pool colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x0040ff, nullptr},
    {"base", "Background", ROCKCAT_OPTION_CHOICE, 0, 0, 1, "Key colours|One colour"},
    {"base_colour", "Background colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x000820, nullptr},
    {"base_level", "Background brightness", ROCKCAT_OPTION_RANGE, 0, 100, 60, nullptr},
    {"mode", "A held key", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "Drops once|Keeps dripping"},
    {"speed", "Speed", ROCKCAT_OPTION_RANGE, 1, 30, 10, nullptr},
    {"drain", "Pool drains (0 = never)", ROCKCAT_OPTION_RANGE, 0, 30, 8, nullptr},
};
const R EQUALIZER_OPTIONS[] = {
    {"style", "Style", ROCKCAT_OPTION_CHOICE, 0, 0, 0, "Bars|Bars in rainbow colours|Whole board pulses"},
    {"bar_colour", "Bar colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x00ff40, nullptr},
    {"peak_colour", "Top of the bars", ROCKCAT_OPTION_COLOR, 0, 0, 0xff2000, nullptr},
    {"base", "Background", ROCKCAT_OPTION_CHOICE, 0, 0, 1, "Key colours|One colour"},
    {"base_colour", "Background colour", ROCKCAT_OPTION_COLOR, 0, 0, 0x000010, nullptr},
    {"base_level", "Background brightness", ROCKCAT_OPTION_RANGE, 0, 100, 40, nullptr},
    {"follow", "Brightness follows the volume", ROCKCAT_OPTION_TOGGLE, 0, 1, 0, nullptr},
    {"gain", "Sensitivity", ROCKCAT_OPTION_RANGE, 1, 30, 10, nullptr},
    {"react", "Reacts (1 = lazy, 20 = jumpy)", ROCKCAT_OPTION_RANGE, 1, 20, 11, nullptr},
    {"sharp", "Bars (0 = soft hills, 20 = pointy)", ROCKCAT_OPTION_RANGE, 0, 20, 15, nullptr},
};
const R FLOW_OPTIONS[] = {
    {"speed", "Speed", ROCKCAT_OPTION_RANGE, 1, 30, 6, nullptr},
    {"level", "Brightness", ROCKCAT_OPTION_RANGE, 5, 100, 100, nullptr},
};
// what the keys do when nobody types, drawn by us. a thing of its own: the same whichever effect is on, switched on
// and off by itself, asked for with ROCKCAT_EFFECT_OPTIONS_IDLE in place of an effect
const R IDLE_OPTIONS[] = {
    {"idle_on", "Go idle when nobody types", ROCKCAT_OPTION_TOGGLE, 0, 1, 0, nullptr},
    {"idle_after", "After seconds", ROCKCAT_OPTION_RANGE, 5, 1800, 60, nullptr},
    {"idle_style", "Going idle", ROCKCAT_OPTION_CHOICE, 0, 0, 1, "Lights off|Fade out|Old tv switching off|Starlit sky"},
    {"wake_style", "Waking up", ROCKCAT_OPTION_CHOICE, 0, 0, 2, "At once|Fade in|Old tv warming up|Fluorescent tube"},
};

template <size_t N> std::vector<R> list(const R (&own)[N])
{
    return std::vector<R>(own, own + N);
}

const std::vector<R> &options_of(int effect)
{
    static const std::vector<R> wave = list(WAVE_OPTIONS), ripple = list(RIPPLE_OPTIONS), heat = list(HEATMAP_OPTIONS),
                                fire = list(FIRE_OPTIONS), drip = list(DRIP_OPTIONS), eq = list(EQUALIZER_OPTIONS),
                                flow = list(FLOW_OPTIONS), idle = list(IDLE_OPTIONS), none;
    switch (effect) {
    case ROCKCAT_EFFECT_OPTIONS_IDLE: return idle;
    case ROCKCAT_EFFECT_WAVE:       return wave;
    case ROCKCAT_EFFECT_RIPPLE:     return ripple;
    case ROCKCAT_EFFECT_HEATMAP:    return heat;
    case ROCKCAT_EFFECT_FIRE:       return fire;
    case ROCKCAT_EFFECT_DRIP:       return drip;
    case ROCKCAT_EFFECT_EQUALIZER:  return eq;
    case ROCKCAT_EFFECT_COLOR_FLOW: return flow;
    default:                        return none;
    }
}

std::string stored_key(int effect, const char *key)
{
    if (std::strncmp(key, "idle_", 5) == 0 || std::strncmp(key, "wake_", 5) == 0)
        return std::string("fx_") + key;
    return "fx" + std::to_string(effect) + "_" + key;
}

const R *find(int effect, const char *key)
{
    for (const R &o : options_of(effect))
        if (std::strcmp(o.key, key) == 0)
            return &o;
    return nullptr;
}

int in_range(const R &o, long v)
{
    switch (o.kind) {
    case ROCKCAT_OPTION_RANGE:  return static_cast<int>(std::min<long>(std::max<long>(v, o.min), o.max));
    case ROCKCAT_OPTION_COLOR:  return static_cast<int>(v & 0xffffff);
    case ROCKCAT_OPTION_TOGGLE: return v ? 1 : 0;
    default: {
        int n = 1;
        for (const char *c = o.choices; c && *c; ++c)
            n += *c == '|';
        return static_cast<int>(std::min<long>(std::max<long>(v, 0), n - 1));
    }
    }
}

} // namespace

extern "C" int rockcat_effect_options(int effect, const rockcat_effect_option **out)
{
    const std::vector<R> &o = options_of(effect);
    if (out)
        *out = o.empty() ? nullptr : o.data();
    return static_cast<int>(o.size());
}

extern "C" int rockcat_effect_option_get(uint16_t pid, uint8_t profile, int effect, const char *key)
{
    const R *o = key ? find(effect, key) : nullptr;
    if (!o)
        return 0;
    std::string v = config_get(pid, profile, stored_key(effect, key));
    return v.empty() ? o->fallback : in_range(*o, std::strtol(v.c_str(), nullptr, 0));
}

// the Easy-Shift light layer: "es_light" says if its on, "es_colors" has six hex digits per led, "-" for a dark one
extern "C" int rockcat_shift_layer_get(uint16_t pid, uint8_t profile, int *enabled, rockcat_color *colors, int count)
{
    if (count < 0 || (count && !colors))
        return ROCKCAT_ERR_ARGUMENT;
    if (enabled)
        *enabled = config_get(pid, profile, "es_light") == "1";
    std::string text = config_get(pid, profile, "es_colors");
    size_t at = 0;
    for (int i = 0; i < count; ++i) {
        colors[i] = rockcat_color{0, 0, 0, 0};
        if (at < text.size() && text[at] == '-') {
            ++at;
        } else if (at + 6 <= text.size()) {
            unsigned long v = std::strtoul(text.substr(at, 6).c_str(), nullptr, 16);
            colors[i] = rockcat_color{255, static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
            at += 6;
        }
        if (at < text.size() && text[at] == ',')
            ++at;
    }
    return ROCKCAT_OK;
}

extern "C" int rockcat_shift_layer_set(uint16_t pid, uint8_t profile, int enabled, const rockcat_color *colors, int count)
{
    if (count < 0 || (count && !colors))
        return ROCKCAT_ERR_ARGUMENT;
    std::string text;
    for (int i = 0; i < count; ++i) {
        if (i)
            text += ',';
        if (colors[i].a == 0) {
            text += '-';
            continue;
        }
        char hex[8];
        std::snprintf(hex, sizeof hex, "%02x%02x%02x", colors[i].r, colors[i].g, colors[i].b);
        text += hex;
    }
    bool ok = config_set(pid, profile, "es_light", enabled ? "1" : "0") && config_set(pid, profile, "es_colors", text);
    return ok ? ROCKCAT_OK : ROCKCAT_ERR_IO;
}

extern "C" int rockcat_shift_layer_effect(uint16_t pid, uint8_t profile)
{
    std::string v = config_get(pid, profile, "es_effect");
    return v.empty() ? ROCKCAT_EFFECT_FULLY_LIT : static_cast<int>(std::strtol(v.c_str(), nullptr, 10));
}

extern "C" int rockcat_shift_layer_set_effect(uint16_t pid, uint8_t profile, int effect)
{
    if (effect < 0 || effect > 31)
        return ROCKCAT_ERR_ARGUMENT;
    return config_set(pid, profile, "es_effect", std::to_string(effect)) ? ROCKCAT_OK : ROCKCAT_ERR_IO;
}

extern "C" int rockcat_effect_idle_preview(uint16_t pid, uint8_t profile)
{
    long n = std::strtol(config_get(pid, profile, "fx_idle_preview").c_str(), nullptr, 10);
    return config_set(pid, profile, "fx_idle_preview", std::to_string(n % 1000000 + 1)) ? ROCKCAT_OK : ROCKCAT_ERR_IO;
}

extern "C" int rockcat_effect_option_set(uint16_t pid, uint8_t profile, int effect, const char *key, int value)
{
    const R *o = key ? find(effect, key) : nullptr;
    if (!o)
        return ROCKCAT_ERR_ARGUMENT;
    return config_set(pid, profile, stored_key(effect, key), std::to_string(in_range(*o, value))) ? ROCKCAT_OK : ROCKCAT_ERR_IO;
}

std::map<std::string, int> kb_fx_options(uint16_t pid, uint8_t profile, int effect)
{
    // the effects own, and what to do when nobody types, which the engine needs whatever the effect is
    std::map<std::string, int> all;
    for (int which : {effect, ROCKCAT_EFFECT_OPTIONS_IDLE})
        for (const R &o : options_of(which))
            all[o.key] = rockcat_effect_option_get(pid, profile, which, o.key);
    // somebody pressing PREVIEW bumps this number, see kb_fx_setup
    all["idle_preview"] = static_cast<int>(std::strtol(config_get(pid, profile, "fx_idle_preview").c_str(), nullptr, 10));
    return all;
}

/* ---- the effects ----------------------------------------------------------- */

struct kb_fx {
    int effect = ROCKCAT_EFFECT_OFF;
    std::map<std::string, int> opt;
    // ripple
    struct ring {
        double x, y, born;
        int led;
    };
    std::vector<ring> rings;
    // heatmap, and the sparks of the fire
    std::vector<double> heat;
    // drip
    struct drop {
        double x, y, speed;
    };
    std::vector<drop> drops;
    std::vector<double> pool;   // how deep the water stands, per column across the board (in board heights)
    std::vector<char> held;
    std::vector<double> dripped; // when a held key last let a drop go
    listener *ears = nullptr;
    double heard[BANDS] = {}; // equalizer: the bands as last heard, and the bars on their way there
    double bars[BANDS] = {};
    unsigned heard_chunk = 0;
    // idle and wake
    double last_key = -1; // not known yet
    bool asleep = false;
    double changed = -100; // when it fell asleep or woke up
    int preview_seen = -1;       // the preview counter as it was last time, -1 = never looked
    bool preview_wanted = false; // PREVIEW got pressed, the next frame starts it
    double preview_until = -1;   // while a preview plays: when to wake up again

    int o(const char *key) const
    {
        auto it = opt.find(key);
        return it == opt.end() ? 0 : it->second;
    }
};

kb_fx *kb_fx_new()
{
    return new kb_fx;
}

void kb_fx_free(kb_fx *fx)
{
    if (!fx)
        return;
    delete fx->ears;
    delete fx;
}

void kb_fx_setup(kb_fx *fx, int effect, const std::map<std::string, int> &options)
{
    if (effect != fx->effect) {
        fx->rings.clear();
        fx->drops.clear();
        std::fill(fx->heat.begin(), fx->heat.end(), 0);
        std::fill(fx->pool.begin(), fx->pool.end(), 0);
    }
    fx->effect = effect;
    fx->opt = options;
    // the preview counter moved since we last looked: somebody wants to see the idle animation
    int counter = fx->o("idle_preview");
    if (fx->preview_seen >= 0 && counter != fx->preview_seen)
        fx->preview_wanted = true;
    fx->preview_seen = counter;
    if (effect == ROCKCAT_EFFECT_EQUALIZER) {
        if (!fx->ears)
            fx->ears = new listener;
        fx->ears->start();
    } else if (fx->ears) {
        // nobody listens to what you play longer than the effect that needs it is on
        delete fx->ears;
        fx->ears = nullptr;
    }
}

namespace {

constexpr int COLUMNS = 24;

void grow(kb_fx *fx, const kb_board &b)
{
    size_t n = static_cast<size_t>(b.leds);
    if (fx->heat.size() != n) {
        fx->heat.assign(n, 0);
        fx->held.assign(n, 0);
        fx->dripped.assign(n, -100);
    }
    if (fx->pool.size() != COLUMNS)
        fx->pool.assign(COLUMNS, 0);
}

rgb base_of(const kb_fx *fx, const kb_board &b, size_t k)
{
    rgb c = fx->o("base") == 1 ? from_int(fx->o("base_colour")) : from_color(b.base[k]);
    return scaled(c, fx->o("base_level") / 100.0);
}

} // namespace

void kb_fx_key(kb_fx *fx, const kb_board &b, int led, bool down, double t)
{
    grow(fx, b);
    if (led < 0 || led >= b.leds)
        return;
    size_t k = static_cast<size_t>(led);
    fx->held[k] = down;
    if (!down)
        return;
    // any key wakes the board up
    fx->last_key = t;
    if (fx->asleep) {
        fx->asleep = false;
        fx->changed = t;
    }
    fx->heat[k] += 1;
    if (b.x[k] < 0)
        return;
    fx->rings.push_back({b.x[k], b.y[k], t, led});
    fx->drops.push_back({b.x[k], b.y[k], 0.15});
    fx->dripped[k] = t;
}

void kb_fx_render(kb_fx *fx, const kb_board &b, double t, double dt, rockcat_color *out)
{
    grow(fx, b);
    const int effect = fx->effect;
    const double speed = std::max(1, fx->o("speed")) / 10.0;
    std::vector<rgb> px(static_cast<size_t>(b.leds), rgb{0, 0, 0});

    if (effect == ROCKCAT_EFFECT_WAVE) {
        const int dir = fx->o("direction"), colours = fx->o("colours");
        const double stretch = 0.25 + (12 - std::min(12, std::max(1, fx->o("width")))) * 0.2;
        const rgb own[3] = {from_int(fx->o("colour1")), from_int(fx->o("colour2")), from_int(fx->o("colour3"))};
        const double angle = fx->o("angle") * PI / 180, low = fx->o("min_level") / 100.0;
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            if (b.x[k] < 0)
                continue;
            // at an angle: how far along that direction the key sits. 0 runs to the right, 90 down, any number in between
            double middle = std::hypot(b.x[k] - 0.5, (b.y[k] - 0.5) * b.aspect);
            double pos = dir == 1 ? -middle : dir == 2 ? middle : -(b.x[k] * std::cos(angle) + b.y[k] * b.aspect * std::sin(angle));
            double phase = -pos * stretch - t * speed * 0.35;
            phase -= std::floor(phase);
            if (colours == 0) {
                px[k] = hue(phase);
            } else if (colours == 3) {
                px[k] = scaled(from_color(b.base[k]), low + (1 - low) * (0.5 - 0.5 * std::cos(2 * PI * phase)));
            } else {
                // around the colours in a circle, so the wave has no seam
                const int n = colours == 1 ? 2 : 3;
                double at = phase * n;
                int a = static_cast<int>(at) % n;
                double f = at - std::floor(at);
                px[k] = mix(own[a], own[(a + 1) % n], f * f * (3 - 2 * f));
            }
        }
    } else if (effect == ROCKCAT_EFFECT_RIPPLE) {
        const double life = 1.6, width = 0.012 + fx->o("width") * 0.011, level = fx->o("ring_level") / 100.0;
        const int ring = fx->o("ring");
        fx->rings.erase(std::remove_if(fx->rings.begin(), fx->rings.end(), [&](const kb_fx::ring &r) { return t - r.born > life; }), fx->rings.end());
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            if (b.x[k] < 0)
                continue;
            double lift = 0, when = 0;
            for (const kb_fx::ring &r : fx->rings) {
                double age = t - r.born, radius = age * 0.42 * speed;
                double off = (std::hypot(b.x[k] - r.x, (b.y[k] - r.y) * b.aspect) - radius) / width;
                double v = std::exp(-off * off) * clamp01(1 - age / life);
                if (v > lift) {
                    lift = v;
                    when = r.born;
                }
            }
            rgb top = ring == 0 ? from_int(fx->o("ring_colour")) : ring == 1 ? mix(from_color(b.base[k]), {1, 1, 1}, 0.35) : hue(when * 0.31 + 0.1);
            px[k] = mix(base_of(fx, b, k), scaled(top, level), lift);
        }
    } else if (effect == ROCKCAT_EFFECT_HEATMAP) {
        const double cool = std::max(2, fx->o("cool")), full = std::max(1, fx->o("presses"));
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            fx->heat[k] *= std::exp(-dt * 0.693 / cool * 2);
            if (b.x[k] < 0)
                continue;
            // a cold key has the background colour, from there it goes over green and yellow to red
            const rgb ramp[4] = {base_of(fx, b, k), {0.1, 0.9, 0.1}, {1, 0.85, 0}, {1, 0.05, 0}};
            double pos = clamp01(fx->heat[k] / full) * 3;
            int a = std::min(2, static_cast<int>(pos));
            px[k] = mix(ramp[a], ramp[a + 1], pos - a);
        }
    } else if (effect == ROCKCAT_EFFECT_FIRE) {
        const int colours = fx->o("colours");
        const double height = std::min(12, std::max(1, fx->o("height"))) / 12.0;
        rgb own = from_int(fx->o("colour"));
        const rgb ramps[5][4] = {
            {{0.35, 0, 0}, {1, 0.12, 0}, {1, 0.55, 0}, {1, 0.95, 0.55}},      // fire
            {{0, 0.02, 0.35}, {0, 0.25, 1}, {0.15, 0.7, 1}, {0.85, 1, 1}},    // gas flame
            {{0, 0.25, 0}, {0.1, 0.8, 0}, {0.55, 1, 0.1}, {0.9, 1, 0.6}},     // poison green
            {{0.2, 0, 0.3}, {0.55, 0, 0.9}, {0.9, 0.3, 1}, {1, 0.8, 1}},      // purple
            {scaled(own, 0.3), scaled(own, 0.75), own, mix(own, {1, 1, 1}, 0.6)},
        };
        const rgb *ramp = ramps[std::min(4, std::max(0, colours))];
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            fx->heat[k] *= std::exp(-dt * 3.5);
            if (b.x[k] < 0)
                continue;
            // two layers of noise that climb at different speeds, and the higher up a key sits the more of it gets cut away
            double n = noise(b.x[k] * 9, b.y[k] * 3.2 + t * speed * 1.7) * 0.65 + noise(b.x[k] * 21 + 40, b.y[k] * 6 + t * speed * 3.1) * 0.35;
            double up = 1 - b.y[k];
            double flame = clamp01((n * 1.25 + 0.25 - up * (1.9 - height * 1.35)) * 1.5);
            if (fx->o("sparks"))
                flame = clamp01(flame + std::min(1.0, fx->heat[k]) * 0.9);
            double pos = flame * 3;
            int a = std::min(2, static_cast<int>(pos));
            // where nothing burns the key has its background, the flame takes over the brighter it gets
            px[k] = mix(base_of(fx, b, k), mix(ramp[a], ramp[a + 1], pos - a), clamp01(flame * 2.2));
        }
    } else if (effect == ROCKCAT_EFFECT_DRIP) {
        const rgb drop_colour = from_int(fx->o("drop_colour")), pool_colour = from_int(fx->o("pool_colour"));
        // a held key lets go of another drop every now and then, if its told to
        if (fx->o("mode") == 1)
            for (int i = 0; i < b.leds; ++i) {
                size_t k = static_cast<size_t>(i);
                if (fx->held[k] && b.x[k] >= 0 && t - fx->dripped[k] > 0.22 / speed) {
                    fx->drops.push_back({b.x[k], b.y[k], 0.15});
                    fx->dripped[k] = t;
                }
            }
        // drops fall faster the longer they fall, and what reaches the water makes it rise where it lands
        for (kb_fx::drop &d : fx->drops) {
            d.speed += dt * 2.6 * speed;
            d.y += d.speed * dt * speed;
        }
        auto column = [](double x) { return std::min(COLUMNS - 1, std::max(0, static_cast<int>(x * COLUMNS))); };
        for (auto it = fx->drops.begin(); it != fx->drops.end();) {
            double surface = 1.04 - fx->pool[static_cast<size_t>(column(it->x))];
            if (it->y >= surface) {
                fx->pool[static_cast<size_t>(column(it->x))] += 0.085;
                it = fx->drops.erase(it);
            } else {
                ++it;
            }
        }
        // water finds its level, and drains
        std::vector<double> next = fx->pool;
        for (int c = 0; c < COLUMNS; ++c) {
            double l = fx->pool[static_cast<size_t>(std::max(0, c - 1))], r = fx->pool[static_cast<size_t>(std::min(COLUMNS - 1, c + 1))];
            next[static_cast<size_t>(c)] += ((l + r) / 2 - fx->pool[static_cast<size_t>(c)]) * std::min(1.0, dt * 6);
        }
        for (double &v : next)
            v = std::min(1.1, std::max(0.0, v - dt * fx->o("drain") * 0.006));
        fx->pool = next;
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            if (b.x[k] < 0)
                continue;
            rgb c = base_of(fx, b, k);
            // how far the water stands over this key. the bottom row is under water as soon as there is any, and dry
            // again once the pool has drained: nothing stays lit down there
            double depth = fx->pool[static_cast<size_t>(column(b.x[k]))] - (1 - b.y[k]);
            if (depth > 0)
                c = mix(c, pool_colour, clamp01(depth * 14));
            for (const kb_fx::drop &d : fx->drops) {
                if (std::fabs(d.x - b.x[k]) > 0.03)
                    continue;
                // the drop itself, and a short tail above it
                double above = d.y - b.y[k];
                if (above > -0.09 && above < 0.3)
                    c = mix(c, drop_colour, above < 0 ? 1 + above / 0.09 : clamp01(1 - above / 0.3) * 0.8);
            }
            px[k] = c;
        }
    } else if (effect == ROCKCAT_EFFECT_EQUALIZER) {
        double band[BANDS] = {}, loud = 0;
        bool works = fx->ears && fx->ears->works;
        if (works) {
            std::lock_guard<std::mutex> g(fx->ears->lock);
            // frames come faster than sound does, without anything new the last that was heard still counts
            if (fx->ears->chunks != fx->heard_chunk) {
                std::copy(fx->ears->band, fx->ears->band + BANDS, fx->heard);
                fx->heard_chunk = fx->ears->chunks;
                fx->ears->taken = true;
            }
            loud = fx->ears->loud;
        } else {
            std::fill(fx->heard, fx->heard + BANDS, 0);
        }
        // how reactive: every bar runs after its band, up four times as fast as down. at 1 it takes its time (a bar
        // needs over a second to fall), at 20 it is where the sound is on the next frame
        const double fall = 0.6 * std::pow(1.28, std::max(1, fx->o("react"))), rise = fall * 4;
        for (int i = 0; i < BANDS; ++i) {
            double to = fx->heard[i], &at = fx->bars[i];
            at += (to - at) * (1 - std::exp(-dt * (to > at ? rise : fall)));
        }
        // how pointy: at 20 every band stands for itself, below that each one gets mixed with its neighbours, wider and
        // wider, until whats left at 0 is a few soft hills
        const double wide = (20 - std::min(20, fx->o("sharp"))) * 0.2;
        for (int i = 0; i < BANDS; ++i) {
            if (wide < 0.05) {
                band[i] = fx->bars[i];
                continue;
            }
            double sum = 0, weight = 0;
            for (int j = 0; j < BANDS; ++j) {
                double w = std::exp(-(i - j) * (i - j) / (2 * wide * wide));
                sum += fx->bars[j] * w;
                weight += w;
            }
            band[i] = sum / weight;
        }
        const double gain = fx->o("gain") / 10.0;
        const int style = fx->o("style");
        const rgb bar = from_int(fx->o("bar_colour")), peak = from_int(fx->o("peak_colour"));
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            if (b.x[k] < 0)
                continue;
            rgb c = base_of(fx, b, k);
            double at = clamp01(b.x[k]) * (BANDS - 1);
            int lo = static_cast<int>(at);
            double v = clamp01((band[lo] + (band[std::min(BANDS - 1, lo + 1)] - band[lo]) * (at - lo)) * gain);
            if (style == 2) {
                c = mix(c, bar, clamp01(loud * gain * 1.4));
            } else {
                // a bar stands on the bottom row and reaches as high as its band is loud
                double up = 1 - b.y[k];
                if (v > 0.03 && up <= v * 1.05) {
                    rgb top = style == 1 ? hue(b.x[k] * 0.85) : mix(bar, peak, clamp01(up / std::max(v, 0.05)) * clamp01(up * 1.4));
                    c = top;
                }
            }
            if (fx->o("follow"))
                c = scaled(c, 0.25 + 0.75 * clamp01(loud * gain * 1.6));
            px[k] = c;
        }
    } else if (effect == ROCKCAT_EFFECT_COLOR_FLOW) {
        // everything the same colour, wandering around the colour wheel. no steps: thats the whole point of doing it here
        rgb c = scaled(hue(t * speed * 0.08), fx->o("level") / 100.0);
        for (size_t k = 0; k < px.size(); ++k)
            px[k] = c;
    }

    // idle and waking up. how much of the picture is left is a number per key between 0 and 1 (and a bit over for a flash)
    if (fx->last_key < 0)
        fx->last_key = t;
    const bool previewing = fx->preview_until >= 0;
    if (fx->preview_wanted) {
        // show it once: fall asleep now whatever the clock says, and wake up again after a moment
        fx->preview_wanted = false;
        fx->asleep = true;
        fx->changed = t;
        fx->preview_until = t + 3.2;
    } else if (previewing && t >= fx->preview_until) {
        fx->preview_until = -1;
        fx->asleep = false;
        fx->changed = t;
        fx->last_key = t;
    } else if (!previewing) {
        const bool on = fx->o("idle_on") != 0;
        if (on && !fx->asleep && t - fx->last_key > std::max(1, fx->o("idle_after"))) {
            fx->asleep = true;
            fx->changed = t;
        }
        if (!on && fx->asleep) {
            fx->asleep = false;
            fx->changed = -100;
        }
    }
    const double since = t - fx->changed;
    const int style = fx->asleep ? fx->o("idle_style") : fx->o("wake_style");
    const bool moving = since < 1.6;
    if (fx->asleep || moving) {
        for (int i = 0; i < b.leds; ++i) {
            size_t k = static_cast<size_t>(i);
            if (b.x[k] < 0)
                continue;
            double f = fx->asleep ? 0 : 1;
            const double dy = std::fabs(b.y[k] - 0.45), dx = std::fabs(b.x[k] - 0.5);
            if (fx->asleep) {
                if (style == 1) {
                    f = clamp01(1 - since / 1.2);
                } else if (style == 2) {
                    // an old tv: the picture gets squeezed into a bright line, the line into a dot, the dot fades
                    double squeeze = clamp01(since / 0.28), line = clamp01((since - 0.28) / 0.3), fade = clamp01((since - 0.58) / 0.35);
                    double keep_y = (1 - squeeze) * 0.6 + 0.09, keep_x = (1 - line) * 0.55 + 0.03;
                    f = dy < keep_y && dx < keep_x ? (1 + squeeze * 1.2) * (1 - fade) : 0;
                } else if (style == 3) {
                    double own = hash2(i, 7), phase = std::fmod(t * (0.2 + 0.2 * own) + own * 9, 1.0);
                    f = std::max(clamp01(1 - since / 0.8), phase < 0.2 ? std::sin(phase / 0.2 * PI) * 0.8 : 0.0);
                }
            } else {
                if (style == 1) {
                    f = clamp01(since / 0.7);
                } else if (style == 2) {
                    // and the other way around, with the glow such a tube needs a moment to get rid of
                    double dot = clamp01(since / 0.2), line = clamp01((since - 0.2) / 0.3), open = clamp01((since - 0.5) / 0.4);
                    double keep_x = 0.03 + line * 0.55, keep_y = 0.09 + open * 0.6;
                    f = dy < keep_y && dx < keep_x ? dot * (2.2 - open * 1.2) : 0;
                } else if (style == 3) {
                    // a fluorescent tube: a few tries, each a bit longer, the ends light up first, then it holds
                    const double tries[4][2] = {{0.05, 0.11}, {0.3, 0.4}, {0.62, 0.78}, {1.0, 99}};
                    f = 0;
                    for (const auto &go : tries)
                        if (since >= go[0] && since < go[1])
                            f = go[1] > 50 ? clamp01(0.75 + (since - go[0]) * 1.2) : 0.55 + 0.4 * hash2(static_cast<int>(since * 60), i % 5);
                    if (since < 1.0)
                        f = std::max(f, (dx > 0.42 ? 0.3 : 0.0) * hash2(static_cast<int>(since * 30), 3));
                }
            }
            px[k] = scaled(px[k], f);
        }
    }

    for (int i = 0; i < b.leds; ++i) {
        const rgb &c = px[static_cast<size_t>(i)];
        auto to8 = [&](double v) { return static_cast<uint8_t>(std::lround(clamp01(v * b.level) * 255)); };
        out[i] = rockcat_color{255, to8(c.r), to8(c.g), to8(c.b)};
    }
}
