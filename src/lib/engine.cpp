// Lighting engine: renders the effects the Kone AIMO firmware cannot draw
// itself (AIMO, Heartbeat, Breathing 2.0, Heartbeat 2.0) and streams them to
// the mouse, the way Swarm's aimo.dll does.
//
// Protocol (verified on hardware): [0e 06 01 01 00 ff] hands the LEDs to the
// host, then report 0x0d = [0d 2e] + 11 x [R G B 0] sets them;
// [0e 06 01 00 00 ff] gives control back to the firmware effect.

#include "internal.h"
#include "kb_effects.h"
#include "keyboards.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

constexpr double PI = 3.14159265358979323846;

// AIMO colour themes from Swarm's data/Swarm/aimo_color_theme.dat.
constexpr uint8_t THEMES[ROCKCAT_AIMO_THEMES][3][3] = {
    {{0xff, 0x00, 0x00}, {0xff, 0x50, 0x00}, {0xe6, 0x8c, 0x00}},
    {{0x00, 0x00, 0xff}, {0x4b, 0x4b, 0xff}, {0x96, 0x96, 0xff}},
    {{0x5a, 0x13, 0xff}, {0x9d, 0x14, 0xff}, {0xaf, 0x0d, 0xdb}},
    {{0xc5, 0x0b, 0xdc}, {0xf1, 0x7f, 0xff}, {0xd0, 0x01, 0x57}},
    {{0xf6, 0xff, 0x39}, {0xf9, 0xc2, 0x00}, {0xf5, 0x68, 0x00}},
    {{0xc8, 0xff, 0x00}, {0x64, 0xff, 0x05}, {0x00, 0xff, 0x0a}},
    {{0x00, 0xff, 0x00}, {0x00, 0xff, 0x32}, {0x00, 0xff, 0x64}},
    {{0xf5, 0x4c, 0x6c}, {0xf6, 0x7e, 0x47}, {0xf9, 0xc2, 0x00}},
    {{0x00, 0x00, 0xff}, {0x00, 0x7f, 0x98}, {0x00, 0xff, 0x32}},
    {{0xf4, 0x00, 0x6b}, {0xf4, 0x00, 0x00}, {0xf6, 0x78, 0xa5}},
};

struct rgb {
    double r, g, b;
};

rgb mix(rgb a, rgb b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

rgb theme_color(int theme, int slot)
{
    const uint8_t *c = THEMES[theme][slot];
    return {c[0] / 255.0, c[1] / 255.0, c[2] / 255.0};
}

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

double gauss(double x, double mu, double sigma)
{
    double d = (x - mu) / sigma;
    return std::exp(-0.5 * d * d);
}

// One heartbeat: a strong beat followed by a weaker one, then rest.
double heartbeat(double phase)
{
    phase -= std::floor(phase);
    return clamp01(gauss(phase, 0.10, 0.045) + 0.65 * gauss(phase, 0.32, 0.05));
}

// Speed byte 1..11 (Swarm slider) -> rate multiplier.
double rate(uint8_t speed)
{
    int s = speed < 1 ? 1 : speed > 11 ? 11 : speed;
    return 0.35 + 0.13 * s;
}

// what the renderer needs to know about a profile, pulled out once so it doesnt ask for every led on every frame
struct light_params {
    int brightness = 255, speed = 6, theme = 0;
    std::vector<rockcat_color> leds;
    std::vector<char> on;
};

rgb led_color(const light_params &lp, int i)
{
    const rockcat_color &c = lp.leds[i];
    double a = c.a / 255.0;
    return {c.r / 255.0 * a, c.g / 255.0 * a, c.b / 255.0 * a};
}

light_params params_of(const rockcat_profile &p)
{
    light_params lp;
    int count = p.desc->caps.led_count;
    lp.leds.assign(static_cast<size_t>(count), rockcat_color{255, 255, 255, 255});
    lp.on.assign(static_cast<size_t>(count), 1);
    for (int i = 0; i < count; ++i) {
        int en = 1;
        p.get_led(i, &lp.leds[i], &en);
        lp.on[i] = static_cast<char>(en);
    }
    // a device without one of these keeps the default from above
    p.get_int(ROCKCAT_SET_BRIGHTNESS, &lp.brightness);
    p.get_int(ROCKCAT_SET_EFFECT_SPEED, &lp.speed);
    // the renderer thinks in the 1 to 11 of the Kone AIMO, a device that counts 0 to 3 gets stretched onto that
    const rockcat_caps &c = p.desc->caps;
    if (c.effect_speed_max > c.effect_speed_min && (c.effect_speed_min != 1 || c.effect_speed_max != 11))
        lp.speed = 1 + (std::min(std::max(lp.speed, c.effect_speed_min), c.effect_speed_max) - c.effect_speed_min) * 10 /
                           (c.effect_speed_max - c.effect_speed_min);
    p.get_int(ROCKCAT_SET_COLOR_THEME, &lp.theme);
    return lp;
}

uint8_t to8(double v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255)); }

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

void render(const light_params &lp, const led_geo *geo, int effect, double t, double activity, rockcat_color *out)
{
    int count = static_cast<int>(lp.leds.size());
    double level = lp.brightness / 255.0;
    double f = rate(static_cast<uint8_t>(lp.speed));
    activity = clamp01(activity);
    int theme = (lp.theme & 0x7f) % ROCKCAT_AIMO_THEMES;

    for (int i = 0; i < count; ++i) {
        // nobody measured where the leds of this device are: treat them all as one spot, in order
        const led_geo p = geo ? geo[i] : led_geo{0, 0, i % 3, i};
        rgb c{0, 0, 0};
        switch (effect) {
        case ROCKCAT_EFFECT_OFF:
            break;
        case ROCKCAT_EFFECT_FULLY_LIT:
            c = led_color(lp, i);
            break;
        case ROCKCAT_EFFECT_BLINKING:
            c = std::fmod(t * f * 0.8, 1.0) < 0.5 ? led_color(lp, i) : rgb{0, 0, 0};
            break;
        case ROCKCAT_EFFECT_BREATHING: {
            double v = 0.5 - 0.5 * std::cos(2 * PI * t * f * 0.3);
            c = mix({0, 0, 0}, led_color(lp, i), v * v);
            break;
        }
        case ROCKCAT_EFFECT_COLOR_CYCLE:
            c = hue(t * f * 0.08);
            break;
        case ROCKCAT_EFFECT_SNAKE: {
            // a bright head with a fading tail running around the mouse
            double head = std::fmod(t * f * 4.0, static_cast<double>(count));
            double d = head - p.ring;
            if (d < 0)
                d += count;
            c = mix({0, 0, 0}, led_color(lp, i), clamp01(1.0 - d / 4.0));
            break;
        }
        case ROCKCAT_EFFECT_COLORWAVE:
            c = hue(t * f * 0.12 - p.depth * 0.35 - (p.side > 0 ? 0.05 : 0));
            break;
        case ROCKCAT_EFFECT_HEARTBEAT:
            c = led_color(lp, i);
            c = mix({0, 0, 0}, c, heartbeat(t * f * 0.75));
            break;
        case ROCKCAT_EFFECT_HEARTBEAT_2:
            // the beat travels from the front of the mouse to the back
            c = mix({0, 0, 0}, led_color(lp, i), heartbeat(t * f * 0.75 - 0.18 * (p.depth + 0.2)));
            break;
        case ROCKCAT_EFFECT_BREATHING_2: {
            double v = 0.5 - 0.5 * std::cos(2 * PI * (t * f * 0.3 - 0.45 * (p.depth + 0.2)));
            c = mix({0, 0, 0}, led_color(lp, i), v * v);
            break;
        }
        case ROCKCAT_EFFECT_AIMO:
        default: {
            // Slowly drifting colour field over the three theme colours. Each
            // LED starts at its own theme slot; activity brightens and
            // lightens the colours.
            double drift = std::sin(t * f * 0.55 + p.depth * 2.4 + p.side * 0.9) *
                           std::cos(t * f * 0.31 - p.depth * 1.3);
            double pos = p.theme_slot + 1.2 * drift;          // -1.2 .. 3.2
            pos = std::fmod(std::fmod(pos, 3.0) + 3.0, 3.0);  // wrap 0..3
            int a = static_cast<int>(pos) % 3, b = (a + 1) % 3;
            double w = pos - std::floor(pos);
            w = w * w * (3 - 2 * w);
            c = mix(theme_color(theme, a), theme_color(theme, b), w);
            double glow = 0.62 + 0.38 * (0.5 + 0.5 * std::sin(t * f * 0.9 + p.depth * 3.1 + p.side));
            glow = glow + (1.0 - glow) * activity;
            c = mix(c, {1, 1, 1}, 0.25 * activity * activity);
            c = {c.r * glow, c.g * glow, c.b * glow};
            break;
        }
        }
        out[i] = lp.on[i] ? rockcat_color{255, to8(c.r * level), to8(c.g * level), to8(c.b * level)}
                          : rockcat_color{255, 0, 0, 0};
    }
}

} // namespace

extern "C" void rockcat_aimo_theme(int theme, rockcat_color out[3])
{
    if (theme < 0 || theme >= ROCKCAT_AIMO_THEMES)
        theme = 0;
    for (int i = 0; i < 3; ++i)
        out[i] = rockcat_color{255, THEMES[theme][i][0], THEMES[theme][i][1], THEMES[theme][i][2]};
}

// old struct api, the gui previews still come through here
extern "C" void rockcat_render_frame(const rockcat_settings *s, uint8_t effect, double t, double activity,
                                     rockcat_color out[ROCKCAT_LED_COUNT])
{
    light_params lp;
    lp.brightness = s->brightness;
    lp.speed = s->effect_speed;
    lp.theme = s->color_theme & 0x7f;
    lp.leds.assign(s->leds, s->leds + ROCKCAT_LED_COUNT);
    for (int i = 0; i < ROCKCAT_LED_COUNT; ++i)
        lp.on.push_back((s->led_enable_mask >> i) & 1);
    render(lp, KONE_AIMO_LEDS, effect, t, activity, out);
}

extern "C" int rockcat_profile_render(const rockcat_profile *p, int effect, double t, double activity, rockcat_color *out, int count)
{
    if (!p || !out)
        return ROCKCAT_ERR_ARGUMENT;
    light_params lp = params_of(*p);
    std::vector<rockcat_color> frame(lp.leds.size());
    render(lp, p->desc->leds, effect, t, activity, frame.data());
    for (size_t i = 0; i < frame.size() && static_cast<int>(i) < count; ++i)
        out[i] = frame[i];
    return static_cast<int>(frame.size());
}

/* ---- engine thread ----------------------------------------------------- */

struct rockcat_engine {
    std::string path;
    rockcat_engine_mode mode = ROCKCAT_ENGINE_AUTO;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> reload{true};
    int lock_fd = -1;
    std::mutex status_mutex;
    rockcat_engine_status status{};
    std::vector<rockcat_color> frame; // last one sent, same lock as status
    bool keyboard = false;            // which of the two workers this one is
};

namespace {

using clock_type = std::chrono::steady_clock;

// one engine per device and user. name = "" is the one for mice, which is also what the user service holds
int acquire_lock(const std::string &name = "")
{
    const char *dir = std::getenv("XDG_RUNTIME_DIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/rockcat-engine" + (name.empty() ? "" : "-" + name) + ".lock";
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void set_status(rockcat_engine *e, const rockcat_engine_status &s)
{
    std::lock_guard<std::mutex> g(e->status_mutex);
    e->status = s;
}

void run(rockcat_engine *e)
{
    rockcat_engine_status st{};
    st.running = 1;
    st.profile = 0xff; // unknown until the first read
    rockcat_device *dev = nullptr;
    const family_ops *fam = nullptr;
    rockcat_profile *prof = nullptr;
    light_params lp;
    int dev_effect = ROCKCAT_EFFECT_OFF;
    rockcat_shift_lighting shift{};
    uint8_t engine_effect = ROCKCAT_EFFECT_AIMO;
    // brightness changed by a button, written to the mouse once presses stop
    bool brightness_dirty = false;
    auto brightness_changed_at = clock_type::now();
    auto last_mode_check = clock_type::now();
    bool shift_held = false;
    uint8_t profile = 0xff;
    bool rendering = false;
    double t = 0, activity = 0;
    int last_buttons = 0;
    // an empty "last frame" never equals a real one, so clearing it is how a redraw gets forced
    std::vector<rockcat_color> frame, last;
    auto last_profile_check = clock_type::now() - std::chrono::seconds(10);
    auto prev = clock_type::now();

    auto fail = [&](int err) {
        st.last_error = err;
        st.rendering = 0;
        rendering = false;
        delete prof;
        prof = nullptr;
        if (dev)
            rockcat_close(dev);
        dev = nullptr;
        set_status(e, st);
    };

    while (!e->stop) {
        if (!dev) {
            // without a path: the first device that lets the pc drive its leds, a Kova next to a Kone must not steal the engine
            std::string path = e->path;
            if (path.empty()) {
                rockcat_device_info infos[16];
                int n = std::min(rockcat_enumerate(infos, 16), 16);
                for (int i = 0; i < n && path.empty(); ++i) {
                    const device_desc *d = device_find(infos[i].vendor_id, infos[i].product_id);
                    // mice only for now: the effects in here are written for a handful of leds, not for 120 keys
                    if (d && d->family == &KONE_AIMO_FAMILY && (d->caps.features & ROCKCAT_HAS_DIRECT_LIGHT))
                        path = infos[i].path;
                }
            }
            int r = path.empty() ? ROCKCAT_ERR_NO_DEVICE : rockcat_open(path.c_str(), &dev);
            if (r == ROCKCAT_OK && (dev->desc->family != &KONE_AIMO_FAMILY || !(dev->desc->caps.features & ROCKCAT_HAS_DIRECT_LIGHT))) {
                rockcat_close(dev);
                dev = nullptr;
                r = ROCKCAT_ERR_UNSUPPORTED;
            }
            if (r != ROCKCAT_OK) {
                fail(r);
                for (int i = 0; i < 20 && !e->stop; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            fam = dev->desc->family;
            frame.assign(static_cast<size_t>(dev->desc->caps.led_count), rockcat_color{255, 0, 0, 0});
            profile = 0xff;
            e->reload = true;
            last.clear();
        }

        auto now = clock_type::now();
        if (e->reload || now - last_profile_check > std::chrono::seconds(2)) {
            last_profile_check = now;
            uint8_t active = 0;
            int r = rockcat_get_active_profile(dev, &active, nullptr);
            if (r != ROCKCAT_OK) {
                fail(r);
                continue;
            }
            if (active != profile || e->reload.exchange(false)) {
                rockcat_profile *fresh = fam->new_profile(dev->desc, active);
                r = fresh->read_light(dev);
                if (r != ROCKCAT_OK) {
                    delete fresh;
                    fail(r);
                    continue;
                }
                delete prof;
                prof = fresh;
                lp = params_of(*prof);
                dev_effect = ROCKCAT_EFFECT_OFF;
                prof->get_int(ROCKCAT_SET_EFFECT, &dev_effect);
                profile = active;
                rockcat_shift_lighting_load(dev->info.product_id, profile, &shift);
                rockcat_engine_effect_load(dev->info.product_id, profile, &engine_effect);
                last.clear();
            }
        }

        int effect = dev_effect;
        bool engine_rendered = rockcat_effect_is_software(static_cast<uint8_t>(effect));
        if (effect == ROCKCAT_EFFECT_AIMO)
            effect = engine_effect; // AIMO on the mouse = "rendered by rockcat"
        if (e->mode == ROCKCAT_ENGINE_AIMO)
            effect = ROCKCAT_EFFECT_AIMO;
        else if (e->mode == ROCKCAT_ENGINE_HEARTBEAT)
            effect = ROCKCAT_EFFECT_HEARTBEAT;
        bool shift_light = shift_held && shift.enabled;
        bool software = engine_rendered || e->mode != ROCKCAT_ENGINE_AUTO || shift_light;
        if (software != rendering) {
            // Easy-Shift transitions must be immediate; profile changes can wait
            // for the device's acknowledgement.
            int r = (shift_light || (!software && shift.enabled)) ? fam->direct_switch_fast(dev, software)
                                                                  : software ? fam->direct_begin(dev) : fam->direct_end(dev);
            if (r != ROCKCAT_OK) {
                fail(r);
                continue;
            }
            rendering = software;
            last.clear();
        }

        // Pace the loop by waiting for events for ~33 ms; mouse motion and
        // clicks feed the AIMO activity level, profile / DPI changes on the
        // device trigger a settings reload.
        auto frame_end = clock_type::now() + std::chrono::milliseconds(33);
        while (!e->stop) {
            int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            frame_end - clock_type::now()).count());
            if (left <= 0)
                break;
            rockcat_event ev;
            int n = rockcat_poll_event(dev, &ev, left);
            if (n < 0) {
                fail(ROCKCAT_ERR_IO);
                break;
            }
            if (n == 0)
                break;
            if (ev.type == ROCKCAT_EVENT_ACTIVITY) {
                activity += 0.004;
                if ((ev.value & ~last_buttons) != 0)
                    activity += 0.35;
                last_buttons = ev.value;
            } else if (ev.type == ROCKCAT_EVENT_EASYSHIFT) {
                bool held = ev.pressed != 0;
                // the firmware paints its own Easy-Shift indicator in host
                // mode; always redraw over it
                last.clear();
                if (held != shift_held) {
                    shift_held = held;
                    if (shift.enabled)
                        break; // react this frame
                }
            } else if (ev.type == ROCKCAT_EVENT_HOST_ACTION) {
                // 1 and 2 are the brightness up / down buttons
                if (ev.pressed && profile != 0xff && (ev.value == 1 || ev.value == 2)) {
                    // logarithmic steps: snap to the nearest level, then move one
                    static constexpr int levels[ROCKCAT_BRIGHTNESS_LEVELS] = ROCKCAT_BRIGHTNESS_LEVEL_TABLE;
                    int idx = 0;
                    for (int i = 1; i < ROCKCAT_BRIGHTNESS_LEVELS; ++i)
                        if (std::abs(levels[i] - lp.brightness) < std::abs(levels[idx] - lp.brightness))
                            idx = i;
                    idx = std::clamp(idx + (ev.value == 1 ? 1 : -1), 0, ROCKCAT_BRIGHTNESS_LEVELS - 1);
                    lp.brightness = levels[idx];
                    brightness_dirty = true;
                    brightness_changed_at = clock_type::now();
                    last.clear();
                }
            } else if (ev.type == ROCKCAT_EVENT_CHANGED) {
                e->reload = true;
            }
        }
        if (!dev)
            continue;

        // Persist button brightness changes once the presses stop (the
        // firmware effects only see the value stored in the profile).
        if (brightness_dirty && clock_type::now() - brightness_changed_at > std::chrono::milliseconds(400)) {
            brightness_dirty = false;
            int r = prof->set_int(ROCKCAT_SET_BRIGHTNESS, lp.brightness);
            if (r == ROCKCAT_OK)
                r = prof->write(dev);
            if (r != ROCKCAT_OK) {
                fail(r);
                continue;
            }
            // storing a profile makes the firmware take the LEDs back
            if (rendering && fam->direct_switch_fast(dev, true) != ROCKCAT_OK) {
                fail(ROCKCAT_ERR_IO);
                continue;
            }
            last.clear();
        }

        // Any profile write (GUI, CLI, Swarm) returns the LEDs to the
        // firmware; check once a second and take them back.
        if (rendering && clock_type::now() - last_mode_check > std::chrono::seconds(1)) {
            last_mode_check = clock_type::now();
            if (fam->direct_active(dev) == 0) {
                if (fam->direct_switch_fast(dev, true) != ROCKCAT_OK) {
                    fail(ROCKCAT_ERR_IO);
                    continue;
                }
                last.clear();
            }
        }

        now = clock_type::now();
        double dt = std::chrono::duration<double>(now - prev).count();
        prev = now;
        activity = std::min(1.0, activity) * std::exp(-dt * 1.2);
        t += dt * (1.0 + 1.5 * activity);

        st.profile = profile;
        st.effect = static_cast<uint8_t>(effect);
        st.rendering = rendering;
        st.last_error = ROCKCAT_OK;
        st.shift_held = shift_held;
        if (rendering) {
            if (shift_light) {
                double k = shift.color.a / 255.0;
                if (shift.style == ROCKCAT_SHIFT_PULSE)
                    k *= 0.55 + 0.45 * std::sin(t * 6.0);
                for (rockcat_color &c : frame)
                    c = rockcat_color{255, static_cast<uint8_t>(shift.color.r * k), static_cast<uint8_t>(shift.color.g * k),
                                      static_cast<uint8_t>(shift.color.b * k)};
            } else {
                render(lp, dev->desc->leds, effect, t, activity, frame.data());
            }
            if (last.size() != frame.size() || std::memcmp(frame.data(), last.data(), frame.size() * sizeof frame[0]) != 0) {
                int r = fam->direct_frame(dev, frame.data(), static_cast<int>(frame.size()));
                if (r != ROCKCAT_OK) {
                    fail(r);
                    continue;
                }
                last = frame;
            }
        }
        {
            std::lock_guard<std::mutex> g(e->status_mutex);
            e->status = st;
            if (rendering)
                e->frame = frame;
        }
    }

    if (dev) {
        if (rendering)
            fam->direct_end(dev);
        rockcat_close(dev);
    }
    delete prof;
    st.running = 0;
    st.rendering = 0;
    set_status(e, st);
}

/* ---- the keyboard worker ------------------------------------------------- */

// the plain worker, for everything that is not a Kone AIMO: a keyboard with 120 keys that sit somewhere and get
// pressed, or a mouse with two lights. it follows the active profile and draws the effects the device cant
// (kb_effects.cpp has them). with any other effect it keeps its hands off and the device shows its own profile
void run_keyboard(rockcat_engine *e)
{
    rockcat_engine_status st{};
    st.running = 1;
    st.profile = 0xff;
    rockcat_device *dev = nullptr;
    rockcat_profile *prof = nullptr;
    kb_board board;
    kb_fx *fx = kb_fx_new();
    std::vector<rockcat_color> frame, sent;
    // the Easy-Shift light layer of the active profile, and whether Easy-Shift is down right now
    std::vector<rockcat_color> shift_colors;
    int shift_on = 0, shift_effect = ROCKCAT_EFFECT_FULLY_LIT;
    // an effect on the Easy-Shift layer runs on its own board, its colours are the layers
    kb_fx *shift_fx = kb_fx_new();
    kb_board shift_board;
    bool shift_down = false;
    int effect = ROCKCAT_EFFECT_OFF;
    long long stamp = 0;
    bool rendering = false, shift_taken = false;
    const auto start = clock_type::now();
    auto last_check = start - std::chrono::seconds(10);
    auto last_look = start;
    auto last_frame = start, last_sent = start;

    auto seconds = [&] { return std::chrono::duration<double>(clock_type::now() - start).count(); };
    auto nap = [&](int ms) {
        for (int i = 0; i < ms / 50 && !e->stop; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };
    auto drop = [&](int err) {
        st.last_error = err;
        st.rendering = 0;
        st.profile = 0xff;
        rendering = false;
        shift_taken = shift_down = false;
        rockcat_profile_free(prof);
        prof = nullptr;
        if (dev)
            rockcat_close(dev);
        dev = nullptr;
        set_status(e, st);
    };
    // which effect the profile wants (the one kept on the pc if the keyboard only has the carrier) and its settings.
    // no talking to the keyboard in here, so this is cheap enough to do whenever the config file changes
    auto look = [&] {
        int by_engine = 0;
        rockcat_profile_get_effect(prof, &effect, &by_engine);
        stamp = config_stamp(dev->desc->pid);
        kb_fx_setup(fx, effect, kb_fx_options(dev->desc->pid, static_cast<uint8_t>(st.profile), effect));
        st.effect = static_cast<uint8_t>(effect);
        shift_colors.assign(static_cast<size_t>(board.leds), rockcat_color{0, 0, 0, 0});
        rockcat_shift_layer_get(dev->desc->pid, static_cast<uint8_t>(st.profile), &shift_on, shift_colors.data(), board.leds);
        // only keyboards tell when Easy-Shift goes down
        shift_on = shift_on && dev->desc->keyboard;
        shift_effect = rockcat_shift_layer_effect(dev->desc->pid, static_cast<uint8_t>(st.profile));
        shift_board = board;
        for (int i = 0; i < board.leds; ++i) {
            const rockcat_color &c = shift_colors[static_cast<size_t>(i)];
            shift_board.base[static_cast<size_t>(i)] = c.a ? rockcat_color{255, c.r, c.g, c.b} : rockcat_color{255, 0, 0, 0};
        }
        const uint8_t slot = static_cast<uint8_t>(st.profile | ROCKCAT_SHIFT_LAYER);
        kb_fx_setup(shift_fx, shift_effect, kb_fx_options(dev->desc->pid, slot, shift_effect));
        return effect >= 0 && effect < 32 && ((dev->desc->caps.software_effects >> effect) & 1);
    };

    while (!e->stop) {
        if (!dev) {
            int r = rockcat_open(e->path.c_str(), &dev);
            if (r == ROCKCAT_OK && (!dev->desc || dev->desc->family == &KONE_AIMO_FAMILY || !(dev->desc->caps.features & ROCKCAT_HAS_DIRECT_LIGHT))) {
                rockcat_close(dev);
                dev = nullptr;
                r = ROCKCAT_ERR_UNSUPPORTED;
            }
            if (r != ROCKCAT_OK) {
                drop(r);
                nap(2000);
                continue;
            }
            board = kb_board{};
            board.leds = dev->desc->caps.led_count;
            board.x.assign(static_cast<size_t>(board.leds), -1);
            board.y.assign(static_cast<size_t>(board.leds), -1);
            // a device without a key table (a mouse) just has its lights in a row
            for (int i = 0; i < board.leds && !dev->desc->keyboard; ++i) {
                board.x[static_cast<size_t>(i)] = board.leds > 1 ? static_cast<double>(i) / (board.leds - 1) : 0.5;
                board.y[static_cast<size_t>(i)] = 0.5;
            }
            if (const keyboard_layout *kb = dev->desc->keyboard) {
                // every layout the tables have, so a key that only exists on one of them has its place too.
                // 0 to 1 is from the leftmost key to the rightmost and from the top row to the bottom one
                double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
                for (int pass = 0; pass < 2; ++pass)
                    for (int r2 = 0; r2 < kb->region_count; ++r2)
                        for (int i = 0; i < kb->regions[r2].count; ++i) {
                            const rockcat_key &k = kb->regions[r2].keys[i];
                            if (k.led < 0 || k.led >= board.leds)
                                continue;
                            double cx = k.x + k.w / 2.0, cy = k.y + k.h / 2.0;
                            if (pass == 0) {
                                x0 = std::min(x0, cx);
                                x1 = std::max(x1, cx);
                                y0 = std::min(y0, cy);
                                y1 = std::max(y1, cy);
                            } else if (board.x[static_cast<size_t>(k.led)] < 0) {
                                board.x[static_cast<size_t>(k.led)] = (cx - x0) / std::max(1.0, x1 - x0);
                                board.y[static_cast<size_t>(k.led)] = (cy - y0) / std::max(1.0, y1 - y0);
                            }
                        }
                if (x1 > x0)
                    board.aspect = (y1 - y0) / (x1 - x0);
            }
            st.last_error = ROCKCAT_OK;
            e->reload = true;
        }

        // which profile is active and what does it want. asked again every two seconds (somebody may have switched it
        // with a key or with the cli) and at once when somebody says so
        auto now = clock_type::now();
        bool asked = e->reload.exchange(false);
        if (asked || now - last_check > std::chrono::seconds(2)) {
            last_check = now;
            uint8_t active = 0;
            int r = rockcat_get_active_profile(dev, &active, nullptr);
            if (r != ROCKCAT_OK) {
                drop(r);
                continue;
            }
            if (asked || active != st.profile || !prof) {
                rockcat_profile *p = nullptr;
                r = rockcat_profile_read(dev, active, &p);
                if (r != ROCKCAT_OK) {
                    drop(r);
                    continue;
                }
                rockcat_profile_free(prof);
                prof = p;
                st.profile = active;
                int brightness = 255;
                rockcat_profile_get_int(prof, ROCKCAT_SET_BRIGHTNESS, &brightness);
                board.level = brightness / 255.0;
                board.base.assign(static_cast<size_t>(board.leds), rockcat_color{255, 0, 0, 0});
                for (int i = 0; i < board.leds; ++i) {
                    int on = 0;
                    rockcat_color c{};
                    if (rockcat_profile_get_led(prof, i, &c, &on) == ROCKCAT_OK && on)
                        board.base[static_cast<size_t>(i)] = c;
                }
                bool ours = look();
                // always said again, also when nothing changed: a profile write or the gui may have taken the leds
                // back to the keyboard (or left them with the pc) in the meantime
                r = ours ? rockcat_direct_begin(dev) : rockcat_direct_end(dev);
                if (r != ROCKCAT_OK) {
                    drop(r);
                    continue;
                }
                rendering = ours;
                sent.clear(); // whatever the device shows now, the next frame goes out
                shift_taken = false; // (and an Easy-Shift layer that was showing is gone with that)
            }
        }
        // the settings of the effects live in a file on the pc. when that changes (somebody moved a slider in the gui)
        // they get read again, a few times a second is plenty
        if (prof && now - last_look > std::chrono::milliseconds(200)) {
            last_look = now;
            if (config_stamp(dev->desc->pid) != stamp) {
                bool ours = look();
                if (ours != rendering) {
                    int r = ours ? rockcat_direct_begin(dev) : rockcat_direct_end(dev);
                    if (r != ROCKCAT_OK) {
                        drop(r);
                        continue;
                    }
                    rendering = ours;
                    sent.clear(); // whatever the device shows now, the next frame goes out
                    shift_taken = false; // (and an Easy-Shift layer that was showing is gone with that)
                }
            }
        }

        // what the device has to say. while drawing its only a quick look: on a keyboard a frame takes long enough by
        // itself, a device that swallows a frame in no time gets the rest of a thirtieth of a second spent here
        rockcat_event ev;
        int spare = 33 - static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - last_frame).count());
        int got = rockcat_poll_event(dev, &ev, rendering || shift_taken ? std::max(1, spare) : shift_on ? 30 : 150);
        bool gone = false;
        while (got != 0) {
            if (got < 0) {
                gone = true;
                break;
            }
            if (ev.type == ROCKCAT_EVENT_KEY) {
                kb_fx_key(fx, board, ev.value, ev.pressed != 0, seconds());
                kb_fx_key(shift_fx, shift_board, ev.value, ev.pressed != 0, seconds());
            }
            else if (ev.type == ROCKCAT_EVENT_EASYSHIFT)
                shift_down = ev.pressed != 0;
            else if (ev.type == ROCKCAT_EVENT_CHANGED)
                e->reload = true;
            got = rockcat_poll_event(dev, &ev, 1);
        }
        if (gone) {
            drop(ROCKCAT_ERR_IO);
            nap(1000);
            continue;
        }
        // Easy-Shift held with a light layer: the pc takes the leds for as long as it is held, also from an effect the
        // keyboard draws itself, and gives them back when it is let go
        const bool showing_shift = shift_on && shift_down;
        if (showing_shift != shift_taken) {
            int r = ROCKCAT_OK;
            if (showing_shift && !rendering)
                r = rockcat_direct_begin(dev);
            else if (!showing_shift && !rendering)
                r = rockcat_direct_end(dev);
            if (r != ROCKCAT_OK) {
                drop(r);
                continue;
            }
            shift_taken = showing_shift;
            sent.clear();
        }
        st.rendering = rendering || showing_shift;
        if (!rendering && !showing_shift) {
            set_status(e, st);
            continue;
        }

        const double dt = std::chrono::duration<double>(clock_type::now() - last_frame).count();
        last_frame = clock_type::now();
        frame.assign(static_cast<size_t>(board.leds), rockcat_color{255, 0, 0, 0});
        kb_fx_render(fx, board, seconds(), std::min(dt, 0.25), frame.data());
        const bool shift_moves = shift_effect >= 0 && shift_effect < 32 && ((dev->desc->caps.software_effects >> shift_effect) & 1);
        if (showing_shift && shift_moves) {
            frame.assign(static_cast<size_t>(board.leds), rockcat_color{255, 0, 0, 0});
            kb_fx_render(shift_fx, shift_board, seconds(), std::min(dt, 0.25), frame.data());
        } else if (showing_shift) {
            for (int i = 0; i < board.leds; ++i)
                frame[static_cast<size_t>(i)] = shift_board.base[static_cast<size_t>(i)];
        }
        // a picture that did not change is not sent again (a heatmap nobody types on, a slow colour flow between two
        // steps): less for the device to chew on. once a second it goes out regardless, in case something else
        // painted over it
        bool same = sent.size() == frame.size() && std::memcmp(sent.data(), frame.data(), frame.size() * sizeof frame[0]) == 0;
        if (!same || last_frame - last_sent > std::chrono::seconds(1)) {
            int r = rockcat_direct_frame(dev, frame.data(), board.leds);
            if (r != ROCKCAT_OK) {
                drop(r);
                continue;
            }
            sent = frame;
            last_sent = last_frame;
        }
        {
            std::lock_guard<std::mutex> g(e->status_mutex);
            e->status = st;
            e->frame = frame;
        }
    }
    if (dev) {
        if (rendering || shift_taken)
            rockcat_direct_end(dev);
        rockcat_close(dev);
    }
    rockcat_profile_free(prof);
    kb_fx_free(fx);
    kb_fx_free(shift_fx);
}

} // namespace

extern "C" int rockcat_engine_start(const char *path, rockcat_engine_mode mode, rockcat_engine **out)
{
    if (!out)
        return ROCKCAT_ERR_ARGUMENT;
    *out = nullptr;
    int r = rockcat_init();
    if (r != ROCKCAT_OK)
        return r;
    // the Kone AIMO has a worker of its own (the first one there was, with everything that mouse needs). every other
    // device gets the plain one and a lock of its own, so they can all run next to each other
    bool keyboard = false;
    std::string lock;
    if (path && *path) {
        rockcat_device_info infos[16];
        int n = std::min(std::max(rockcat_enumerate(infos, 16), 0), 16);
        for (int i = 0; i < n; ++i) {
            const device_desc *d = device_find(infos[i].vendor_id, infos[i].product_id);
            if (d && std::strcmp(infos[i].path, path) == 0 && d->family != &KONE_AIMO_FAMILY) {
                char pid[8];
                std::snprintf(pid, sizeof pid, "%04x", infos[i].product_id);
                keyboard = true;
                lock = pid;
            }
        }
    }
    int fd = acquire_lock(lock);
    if (fd < 0)
        return ROCKCAT_ERR_BUSY;
    auto *e = new rockcat_engine;
    e->path = path ? path : "";
    e->mode = mode;
    e->lock_fd = fd;
    e->keyboard = keyboard;
    e->status.running = 1;
    e->status.profile = 0xff;
    e->worker = std::thread(keyboard ? run_keyboard : run, e);
    *out = e;
    return ROCKCAT_OK;
}

extern "C" void rockcat_engine_stop(rockcat_engine *e)
{
    if (!e)
        return;
    e->stop = true;
    if (e->worker.joinable())
        e->worker.join();
    if (e->lock_fd >= 0)
        ::close(e->lock_fd);
    delete e;
}

extern "C" void rockcat_engine_get_status(rockcat_engine *e, rockcat_engine_status *out)
{
    if (!e || !out)
        return;
    std::lock_guard<std::mutex> g(e->status_mutex);
    *out = e->status;
}

extern "C" int rockcat_engine_get_frame(rockcat_engine *e, rockcat_color *out, int max)
{
    if (!e || !out)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> g(e->status_mutex);
    for (size_t i = 0; i < e->frame.size() && static_cast<int>(i) < max; ++i)
        out[i] = e->frame[i];
    return static_cast<int>(e->frame.size());
}

extern "C" void rockcat_engine_reload(rockcat_engine *e)
{
    if (e)
        e->reload = true;
}
