// runs one fixed session against the pretend mouse and against a recording of that same session
// if the library ever sends a different byte than it did when the recording was made, this goes red
// the session exists twice, once through the old struct api and once through the profile api, and both have to
// produce the exact same traffic. thats the proof that moving to the new api changed nothing on the wire
//   rockcat-test-replay check FILE    the test
//   rockcat-test-replay parse FILE    only checks that the file is a readable recording
//   rockcat-test-replay diverge FILE  makes sure a replay actually notices when the code goes off script
//   rockcat-test-replay profile       the profile api against the pretend mouse, no recording involved
//   rockcat-test-replay events        input reports turned into events
//   rockcat-test-replay render [FILE] every effect rendered, compared with FILE or printed if there is none
//   rockcat-test-replay regen FILE    rewrites the recording. only do that when the traffic was MEANT to change

#include "internal.h"
#include "keyboards.h"
#include "sim_kone.h"
#include "sim_ryos.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <unistd.h>

static std::string g_log;

static void note(const char *what, int r)
{
    g_log += what;
    g_log += '=';
    g_log += std::to_string(r);
    g_log += '\n';
}

static void note_bytes(const char *what, const void *data, size_t len)
{
    char tmp[4];
    g_log += what;
    g_log += ':';
    for (size_t i = 0; i < len; ++i) {
        std::snprintf(tmp, sizeof tmp, "%02x", static_cast<const uint8_t *>(data)[i]);
        g_log += tmp;
    }
    g_log += '\n';
}

static rockcat_macro test_macro()
{
    rockcat_macro m;
    std::memset(&m, 0, sizeof m);
    m.profile = 1;
    m.button = 8;
    m.loop = 1;
    m.mode = ROCKCAT_MACRO_ON_PRESS;
    std::snprintf(m.set_name, sizeof m.set_name, "test set");
    std::snprintf(m.name, sizeof m.name, "gg");
    m.count = 4;
    m.keys[0] = rockcat_keystroke{0x0a, ROCKCAT_KEY_PRESS, 20};
    m.keys[1] = rockcat_keystroke{0x0a, ROCKCAT_KEY_RELEASE, 20};
    m.keys[2] = rockcat_keystroke{0x0a, ROCKCAT_KEY_PRESS, 20};
    m.keys[3] = rockcat_keystroke{0x0a, ROCKCAT_KEY_RELEASE, 0};
    return m;
}

static void test_colors(rockcat_color colors[ROCKCAT_LED_COUNT])
{
    for (int i = 0; i < ROCKCAT_LED_COUNT; ++i)
        colors[i] = rockcat_color{200, static_cast<uint8_t>(i * 23), 255, static_cast<uint8_t>(255 - i * 23)};
}

// touches every kind of transfer the library knows: plain reads, select + read, writes with a status wait, chunked macros
static std::string session_old(rockcat_device *dev)
{
    g_log.clear();
    char fw[16] = "";
    note("firmware", rockcat_get_firmware_version(dev, fw, sizeof fw));
    g_log += fw;
    g_log += '\n';

    uint8_t active = 0xff, count = 0;
    note("active", rockcat_get_active_profile(dev, &active, &count));
    note_bytes("active", &active, 1);

    rockcat_settings s[ROCKCAT_PROFILE_COUNT];
    rockcat_buttons b[ROCKCAT_PROFILE_COUNT];
    for (uint8_t p = 0; p < ROCKCAT_PROFILE_COUNT; ++p) {
        note("settings", rockcat_read_settings(dev, p, &s[p]));
        note_bytes("settings", s[p].raw, sizeof s[p].raw);
        note("buttons", rockcat_read_buttons(dev, p, &b[p]));
        note_bytes("buttons", &b[p], sizeof b[p]);
    }

    s[1].dpi_x[0] = s[1].dpi_y[0] = 1200;
    s[1].dpi_active = 0;
    s[1].leds[ROCKCAT_LED_WHEEL] = rockcat_color{255, 255, 0, 0};
    s[1].effect = ROCKCAT_EFFECT_BREATHING;
    note("write settings", rockcat_write_settings(dev, &s[1]));
    note("settings again", rockcat_read_settings(dev, 1, &s[1]));
    note_bytes("settings again", s[1].raw, sizeof s[1].raw);
    note("buttons again", rockcat_read_buttons(dev, 1, &b[1]));

    note("activate", rockcat_set_active_profile(dev, 1));
    note("active", rockcat_get_active_profile(dev, &active, &count));
    note_bytes("active", &active, 1);

    b[1].buttons[8] = rockcat_button{0x40, 0x04, 0x01};
    note("write buttons", rockcat_write_buttons(dev, &b[1]));
    note("settings once more", rockcat_read_settings(dev, 1, &s[1]));
    note("buttons once more", rockcat_read_buttons(dev, 1, &b[1]));
    note_bytes("buttons once more", &b[1], sizeof b[1]);

    static rockcat_macro m, mback;
    m = test_macro();
    note("write macro", rockcat_write_macro(dev, &m));
    note("macro again", rockcat_read_macro(dev, 1, 8, &mback));
    note_bytes("macro again", &mback, offsetof(rockcat_macro, keys) + 4 * sizeof(rockcat_keystroke));

    rockcat_tdcu t;
    note("tdcu", rockcat_read_tdcu(dev, &t));
    note_bytes("tdcu", &t, sizeof t);

    rockcat_color colors[ROCKCAT_LED_COUNT];
    test_colors(colors);
    note("direct on", rockcat_set_direct_lighting(dev, 1));
    note("direct colors", rockcat_set_direct_colors(dev, colors));
    note("direct off", rockcat_set_direct_lighting(dev, 0));
    return g_log;
}

// the same session, step for step, through the profile api
static std::string session_new(rockcat_device *dev)
{
    g_log.clear();
    char fw[16] = "";
    note("firmware", rockcat_get_firmware_version(dev, fw, sizeof fw));
    g_log += fw;
    g_log += '\n';

    uint8_t active = 0xff, count = 0;
    note("active", rockcat_get_active_profile(dev, &active, &count));
    note_bytes("active", &active, 1);

    rockcat_profile *p[ROCKCAT_PROFILE_COUNT] = {};
    for (int i = 0; i < ROCKCAT_PROFILE_COUNT; ++i) {
        note("read", rockcat_profile_read(dev, i, &p[i]));
        int x = 0, y = 0, on = 0;
        note("dpi", p[i] ? rockcat_profile_get_dpi(p[i], 0, &x, &y, &on) : -1);
        note("dpi x", x);
    }
    if (!p[1])
        return g_log;

    note("set dpi", rockcat_profile_set_dpi(p[1], 0, 1200, 1200, 1));
    note("set active level", rockcat_profile_set_int(p[1], ROCKCAT_SET_DPI_ACTIVE, 0));
    note("set led", rockcat_profile_set_led(p[1], ROCKCAT_LED_WHEEL, rockcat_color{255, 255, 0, 0}, 1));
    note("set effect", rockcat_profile_set_int(p[1], ROCKCAT_SET_EFFECT, ROCKCAT_EFFECT_BREATHING));
    note("write settings", rockcat_profile_write(dev, p[1]));
    rockcat_profile *again = nullptr;
    note("read again", rockcat_profile_read(dev, 1, &again));
    int x = 0, effect = 0;
    rockcat_color wheel{};
    note("dpi", again ? rockcat_profile_get_dpi(again, 0, &x, nullptr, nullptr) : -1);
    note("dpi x", x);
    note("effect", again ? rockcat_profile_get_int(again, ROCKCAT_SET_EFFECT, &effect) : -1);
    note("effect is", effect);
    note("led", again ? rockcat_profile_get_led(again, ROCKCAT_LED_WHEEL, &wheel, nullptr) : -1);
    note_bytes("led", &wheel, sizeof wheel);

    note("activate", rockcat_set_active_profile(dev, 1));
    note("active", rockcat_get_active_profile(dev, &active, &count));
    note_bytes("active", &active, 1);

    rockcat_assignment a{0x40, 0x04, 0x01};
    note("set input", again ? rockcat_profile_set_input(again, 0, 8, &a) : -1);
    note("write buttons", again ? rockcat_profile_write(dev, again) : -1);
    rockcat_profile *once_more = nullptr;
    note("read once more", rockcat_profile_read(dev, 1, &once_more));
    rockcat_assignment aback{};
    note("input", once_more ? rockcat_profile_get_input(once_more, 0, 8, &aback) : -1);
    note_bytes("input", &aback, sizeof aback);

    static rockcat_macro m, mback;
    m = test_macro();
    note("set macro", once_more ? rockcat_profile_set_macro(once_more, 0, 8, &m) : -1);
    note("write macro", once_more ? rockcat_profile_write(dev, once_more) : -1);
    note("macro again", once_more ? rockcat_profile_get_macro(dev, once_more, 0, 8, &mback) : -1);
    note_bytes("macro again", &mback, offsetof(rockcat_macro, keys) + 4 * sizeof(rockcat_keystroke));

    rockcat_tdcu t;
    note("tdcu", rockcat_read_tdcu(dev, &t));
    note_bytes("tdcu", &t, sizeof t);

    rockcat_color colors[ROCKCAT_LED_COUNT];
    test_colors(colors);
    note("direct on", rockcat_direct_begin(dev));
    note("direct colors", rockcat_direct_frame(dev, colors, ROCKCAT_LED_COUNT));
    note("direct off", rockcat_direct_end(dev));

    for (rockcat_profile *q : p)
        rockcat_profile_free(q);
    rockcat_profile_free(again);
    rockcat_profile_free(once_more);
    return g_log;
}

static rockcat_device_info sim_info()
{
    rockcat_device_info info;
    std::memset(&info, 0, sizeof info);
    info.vendor_id = ROCKCAT_VID;
    info.product_id = ROCKCAT_PID_KONE_AIMO_REMASTERED;
    info.release = 0x0105;
    std::snprintf(info.name, sizeof info.name, "ROCCAT Kone AIMO Remastered (simulated)");
    return info;
}

static int fail(const char *what)
{
    std::fprintf(stderr, "test_replay: %s\n", what);
    return 1;
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// things about the profile api that a recording cant show: what does NOT get sent, what gets refused, what survives
static int profile_checks()
{
    int failures = 0;
    kone_sim *sim = new kone_sim;
    rockcat_device *dev = device_from_transport(std::unique_ptr<transport>(sim), sim_info());
    const rockcat_caps *caps = rockcat_get_caps(dev);
    CHECK(caps && caps->kind == ROCKCAT_KIND_MOUSE && caps->profile_count == 5 && caps->led_count == 11);
    CHECK(caps && caps->input_count == 12 && caps->layer_count == 2);
    CHECK(std::strcmp(rockcat_input_label(caps, 0), "Left button") == 0 && std::strcmp(rockcat_input_label(caps, 12), "?") == 0);

    rockcat_profile *p = nullptr;
    CHECK(rockcat_profile_read(dev, 5, &p) == ROCKCAT_ERR_ARGUMENT && !p);
    CHECK(rockcat_profile_read(dev, 2, &p) == ROCKCAT_OK && p && rockcat_profile_index(p) == 2);
    if (!p)
        return 1;

    // writing a profile nobody touched sends nothing at all
    std::memset(sim->sets, 0, sizeof sim->sets);
    CHECK(rockcat_profile_write(dev, p) == ROCKCAT_OK);
    CHECK(sim->sets[0x06] == 0 && sim->sets[0x07] == 0 && sim->sets[0x08] == 0);

    // out of range gets refused and doesnt count as a change
    CHECK(rockcat_profile_set_dpi(p, 5, 800, 800, 1) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_dpi(p, 0, 50, 800, 1) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_dpi(p, 0, 800, 16050, 1) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_int(p, ROCKCAT_SET_POLLING_HZ, 333) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_int(p, ROCKCAT_SET_EFFECT, 11) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_int(p, ROCKCAT_SET_COLOR_THEME, 10) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_led(p, 11, rockcat_color{255, 1, 2, 3}, 1) == ROCKCAT_ERR_ARGUMENT);
    rockcat_assignment a{ROCKCAT_BT_MUTE, 0, 0};
    CHECK(rockcat_profile_set_input(p, 2, 0, &a) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_input(p, 0, 12, &a) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_write(dev, p) == ROCKCAT_OK);
    CHECK(sim->sets[0x06] == 0 && sim->sets[0x07] == 0);

    // a settings change sends the settings report and leaves the buttons alone, and the other way around
    CHECK(rockcat_profile_set_dpi(p, 3, 3275, 1649, 0) == ROCKCAT_OK);
    CHECK(rockcat_profile_set_int(p, ROCKCAT_SET_POLLING_HZ, 500) == ROCKCAT_OK);
    CHECK(rockcat_profile_set_int(p, ROCKCAT_SET_COLOR_THEME, 4) == ROCKCAT_OK);
    CHECK(rockcat_profile_write(dev, p) == ROCKCAT_OK);
    CHECK(sim->sets[0x06] == 1 && sim->sets[0x07] == 0);
    CHECK(rockcat_profile_set_input(p, 1, 3, &a) == ROCKCAT_OK);
    CHECK(rockcat_profile_write(dev, p) == ROCKCAT_OK);
    CHECK(sim->sets[0x06] == 1 && sim->sets[0x07] == 1);
    // a clone is its own thing: changing it changes nothing on the original and nothing on the mouse
    rockcat_profile *copy = rockcat_profile_clone(p);
    CHECK(copy && rockcat_profile_set_int(copy, ROCKCAT_SET_BRIGHTNESS, 7) == ROCKCAT_OK);
    int bright = 0;
    CHECK(rockcat_profile_get_int(p, ROCKCAT_SET_BRIGHTNESS, &bright) == ROCKCAT_OK && bright == 255);
    CHECK(rockcat_profile_write(dev, p) == ROCKCAT_OK && sim->sets[0x06] == 1);
    rockcat_profile_free(copy);
    rockcat_profile_free(p);

    // what the mouse has now: our changes in, the bytes we have no name for exactly as they were
    const uint8_t *raw = sim->settings[2];
    CHECK(raw[0x1c] == 0x5a && raw[0x78] == 0xa2);
    CHECK(raw[0x1a] == ROCKCAT_POLLING_500HZ && raw[0x74] == 0x84);
    CHECK(sim->buttons[2][3 + 3 * 15] == ROCKCAT_BT_MUTE);
    CHECK(rockcat_profile_read(dev, 2, &p) == ROCKCAT_OK);
    int x = 0, y = 0, on = 1, hz = 0, theme = 0;
    CHECK(rockcat_profile_get_dpi(p, 3, &x, &y, &on) == ROCKCAT_OK && x == 3250 && y == 1600 && on == 0);
    CHECK(rockcat_profile_get_int(p, ROCKCAT_SET_POLLING_HZ, &hz) == ROCKCAT_OK && hz == 500);
    CHECK(rockcat_profile_get_int(p, ROCKCAT_SET_COLOR_THEME, &theme) == ROCKCAT_OK && theme == 4);

    // backup of profile 3, restored into slot 5: everything gets sent, to the new slot, unknown bytes included
    uint8_t blob[512];
    int n = rockcat_profile_export(p, nullptr, 0);
    CHECK(n > 0 && n <= static_cast<int>(sizeof blob) && rockcat_profile_export(p, blob, sizeof blob) == n);
    rockcat_profile *q = nullptr;
    CHECK(rockcat_profile_import(dev, blob, static_cast<size_t>(n), &q) == ROCKCAT_OK && q);
    CHECK(q && rockcat_profile_set_index(q, 4) == ROCKCAT_OK);
    std::memset(sim->sets, 0, sizeof sim->sets);
    CHECK(q && rockcat_profile_write(dev, q) == ROCKCAT_OK);
    CHECK(sim->sets[0x06] == 1 && sim->sets[0x07] == 1);
    CHECK(sim->settings[4][0x02] == 4 && sim->settings[4][0x1c] == 0x5a && sim->settings[4][0x78] == 0xa2 && sim->settings[4][0x74] == 0x84);
    CHECK(sim->buttons[4][2] == 4 && sim->buttons[4][3 + 3 * 15] == ROCKCAT_BT_MUTE);
    blob[5] ^= 0xff; // now it claims to be from some other model
    rockcat_profile *wrong = nullptr;
    CHECK(rockcat_profile_import(dev, blob, static_cast<size_t>(n), &wrong) == ROCKCAT_ERR_ARGUMENT && !wrong);
    rockcat_profile_free(p);
    rockcat_profile_free(q);

    // a device thats not in the table (opened raw) has no caps and no profiles, but doesnt crash either
    rockcat_device_info unknown = sim_info();
    unknown.product_id = 0x1234;
    rockcat_device *raw_dev = device_from_transport(std::unique_ptr<transport>(new kone_sim), unknown);
    CHECK(rockcat_get_caps(raw_dev) == nullptr);
    CHECK(rockcat_profile_read(raw_dev, 0, &p) == ROCKCAT_ERR_UNSUPPORTED);
    CHECK(rockcat_direct_begin(raw_dev) == ROCKCAT_ERR_UNSUPPORTED);
    rockcat_close(raw_dev);
    rockcat_close(dev);
    return failures ? 1 : 0;
}

// every effect at a handful of moments, as text. compared against tests/render_golden.txt so the effects keep looking
// the way they did (the numbers in there came out of the renderer as upstream wrote it, before it learned about other devices)
static int render_check(const char *golden)
{
    std::string got;
    rockcat_device *dev = device_from_transport(std::unique_ptr<transport>(new kone_sim), sim_info());
    for (int profile : {0, 3}) {
        rockcat_profile *p = nullptr;
        if (rockcat_profile_read(dev, profile, &p) != ROCKCAT_OK)
            return fail("cant read a profile from the simulated mouse");
        unsigned mask = profile ? 0x5b7 : 0x7ff;
        for (int led = 0; led < ROCKCAT_LED_COUNT; ++led) {
            rockcat_color c{};
            rockcat_profile_get_led(p, led, &c, nullptr);
            rockcat_profile_set_led(p, led, c, (mask >> led) & 1);
        }
        rockcat_profile_set_int(p, ROCKCAT_SET_EFFECT_SPEED, profile ? 9 : 6);
        rockcat_profile_set_int(p, ROCKCAT_SET_BRIGHTNESS, profile ? 140 : 255);
        rockcat_profile_set_int(p, ROCKCAT_SET_COLOR_THEME, profile ? 7 : 0);
        for (int effect = 0; effect <= ROCKCAT_EFFECT_COLORWAVE; ++effect)
            for (double t : {0.0, 0.37, 1.9, 12.25})
                for (double activity : {0.0, 0.6}) {
                    rockcat_color out[ROCKCAT_LED_COUNT];
                    if (rockcat_profile_render(p, effect, t, activity, out, ROCKCAT_LED_COUNT) != ROCKCAT_LED_COUNT)
                        return fail("render didnt return 11 leds");
                    char line[160];
                    std::snprintf(line, sizeof line, "p%d e%02d t%.2f a%.1f:", profile, effect, t, activity);
                    got += line;
                    for (const rockcat_color &c : out) {
                        std::snprintf(line, sizeof line, " %02x%02x%02x%02x", c.a, c.r, c.g, c.b);
                        got += line;
                    }
                    got += '\n';
                }
        rockcat_profile_free(p);
    }
    rockcat_close(dev);
    if (!golden) {
        std::fputs(got.c_str(), stdout);
        return 0;
    }
    std::string want;
    if (FILE *f = std::fopen(golden, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            want.append(buf, n);
        std::fclose(f);
    }
    return want == got ? 0 : fail("the effects render differently than they used to. if that was the plan: rockcat-test-replay render > tests/render_golden.txt");
}

// input reports as the engine comments document them, fed through the simulated mouse
static int event_checks()
{
    int failures = 0;
    kone_sim *sim = new kone_sim;
    rockcat_device *dev = device_from_transport(std::unique_ptr<transport>(sim), sim_info());
    sim->inputs = {{0x01, 0x03, 0x10, 0x00},       // mouse report, left + right down
                   {0x03, 0x00, 0xee, 0x02, 0x00}, // Easy-Shift pressed
                   {0x03, 0x00, 0xee, 0x00, 0x00}, // and released
                   {0x03, 0x00, 0xf2, 0x02, 0x00}, // host action 2 (brightness down) pressed
                   {0x03, 0x00, 0xf2, 0x02, 0x01}, // released
                   {0x03, 0x00, 0xb0, 0x01, 0x00}, // something changed on the device
                   {0x03, 0x00, 0xef, 0x00, 0x00}, // the one 0x03 report that means nothing
                   {0x07, 0x00}};                  // not an event at all
    rockcat_event ev;
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_ACTIVITY && ev.value == 0x03);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_EASYSHIFT && ev.pressed == 1);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_EASYSHIFT && ev.pressed == 0);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_HOST_ACTION && ev.value == 2 && ev.pressed == 1);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_HOST_ACTION && ev.value == 2 && ev.pressed == 0);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_CHANGED);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_NONE);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_NONE);
    CHECK(rockcat_poll_event(dev, &ev, 10) == 0 && ev.type == ROCKCAT_EVENT_NONE);
    rockcat_close(dev);
    return failures ? 1 : 0;
}

// what a real Ryos MK FX sent when its owner held Caps Lock (Easy-Shift) and then the middle thumb key (open driver)
static int ryos_event_checks()
{
    int failures = 0;
    kone_sim *sim = new kone_sim;
    rockcat_device_info info = sim_info();
    info.product_id = ROCKCAT_PID_RYOS_MK_FX;
    rockcat_device *dev = device_from_transport(std::unique_ptr<transport>(sim), info);
    sim->inputs = {{0x03, 0x00, 0x0a, 0xff, 0x00}, {0x03, 0x00, 0xfd, 0x01, 0x00}, {0x03, 0x00, 0x0a, 0xff, 0x01},
                   {0x03, 0x00, 0xfd, 0x00, 0x00}, {0x03, 0x00, 0x10, 0x01, 0x00}, {0x03, 0x00, 0x10, 0x00, 0x00}};
    rockcat_event ev;
    for (int pressed : {1, 1, 0, 0})
        CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_EASYSHIFT && ev.pressed == pressed);
    for (int pressed : {1, 0})
        CHECK(rockcat_poll_event(dev, &ev, 10) == 1 && ev.type == ROCKCAT_EVENT_HOST_ACTION && ev.value == 0x10 && ev.pressed == pressed);
    rockcat_close(dev);
    return failures ? 1 : 0;
}

// the lighting engine on a (pretend) keyboard: it has to follow the active profile, take the leds for wave, ripple and
// heatmap and give them back for everything else, and turn the key presses the keyboard reports into light
static int keyboard_engine_checks()
{
    int failures = 0;
    setenv("ROCKCAT_SIM", "ryos-mk-fx", 1);
    std::string runtime = "/tmp/rockcat-test-" + std::to_string(getpid());
    mkdir(runtime.c_str(), 0700);
    setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
    ryos_sim &sim = ryos_sim_shared();
    rockcat_device *dev = nullptr;
    CHECK(rockcat_open(nullptr, &dev) == ROCKCAT_OK && dev);
    if (!dev)
        return 1;
    rockcat_device_info info;
    rockcat_get_info(dev, &info);
    auto set_effect = [&](int effect) {
        rockcat_profile *p = nullptr;
        CHECK(rockcat_profile_read(dev, 0, &p) == ROCKCAT_OK && p);
        CHECK(rockcat_profile_select_effect(p, effect, 0) == ROCKCAT_OK);
        CHECK(rockcat_profile_write(dev, p) == ROCKCAT_OK);
        rockcat_profile_free(p);
    };
    // waits until the engine says what we are waiting for, a second at most
    auto until = [&](rockcat_engine *e, bool rendering, int effect) {
        for (int i = 0; i < 100; ++i) {
            rockcat_engine_status st;
            rockcat_engine_get_status(e, &st);
            if (st.rendering == rendering && st.effect == effect && st.profile == 0)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    rockcat_engine *engine = nullptr;
    CHECK(rockcat_engine_start(info.path, ROCKCAT_ENGINE_AUTO, &engine) == ROCKCAT_OK && engine);
    if (!engine)
        return 1;
    // one per keyboard, a second one gets turned away
    rockcat_engine *second = nullptr;
    CHECK(rockcat_engine_start(info.path, ROCKCAT_ENGINE_AUTO, &second) == ROCKCAT_ERR_BUSY && !second);
    // fully lit is the keyboards own business
    CHECK(until(engine, false, ROCKCAT_EFFECT_FULLY_LIT));
    {
        std::lock_guard<std::mutex> g(sim.lock);
        CHECK(sim.custom == 0 && sim.frames == 0);
    }

    // wave: the engine takes the leds and the board is not one colour
    set_effect(ROCKCAT_EFFECT_WAVE);
    rockcat_engine_reload(engine);
    CHECK(until(engine, true, ROCKCAT_EFFECT_WAVE));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    rockcat_color frame[120];
    CHECK(rockcat_engine_get_frame(engine, frame, 120) == 120);
    CHECK(frame[1].r != frame[107].r || frame[1].g != frame[107].g || frame[1].b != frame[107].b); // Esc and Num . are far apart
    CHECK(frame[71].r == 0 && frame[71].g == 0 && frame[71].b == 0);                             // a slot without a key stays dark
    {
        std::lock_guard<std::mutex> g(sim.lock);
        CHECK(sim.custom == 1 && sim.frames > 0);
    }

    // ripple: quiet until a key gets pressed, then the key and what is around it lights up
    set_effect(ROCKCAT_EFFECT_RIPPLE);
    rockcat_engine_reload(engine);
    CHECK(until(engine, true, ROCKCAT_EFFECT_RIPPLE));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(rockcat_engine_get_frame(engine, frame, 120) == 120);
    int quiet = frame[61].r; // A, red on profile 1 of the pretend keyboard
    {
        std::lock_guard<std::mutex> g(sim.lock);
        sim.inputs.push_back({0x03, 0x00, 0xfb, 61 + 16, 0x01});
        sim.inputs.push_back({0x03, 0x00, 0xfb, 61 + 16, 0x00});
    }
    int loud = 0;
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        rockcat_engine_get_frame(engine, frame, 120);
        loud = std::max<int>(loud, frame[61].r);
    }
    CHECK(quiet > 0 && quiet < 80 && loud > 200);

    // heatmap: the key that got pressed is warmer than one that didnt
    set_effect(ROCKCAT_EFFECT_HEATMAP);
    rockcat_engine_reload(engine);
    CHECK(until(engine, true, ROCKCAT_EFFECT_HEATMAP));
    {
        std::lock_guard<std::mutex> g(sim.lock);
        for (int i = 0; i < 6; ++i)
            sim.inputs.push_back({0x03, 0x00, 0xfb, 61 + 16, 0x01});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(rockcat_engine_get_frame(engine, frame, 120) == 120);
    CHECK(frame[61].r > 200 && frame[62].r < 60 && frame[62].b > 20);

    // and back to something the keyboard does itself: hands off
    set_effect(ROCKCAT_EFFECT_BREATHING);
    rockcat_engine_reload(engine);
    CHECK(until(engine, false, ROCKCAT_EFFECT_BREATHING));
    {
        std::lock_guard<std::mutex> g(sim.lock);
        CHECK(sim.custom == 0);
    }

    // the Easy-Shift light layer: while Easy-Shift is held the pc shows its colours, also over an effect the
    // keyboard draws itself, and lets go again when it is released
    {
        rockcat_color layer[120];
        for (rockcat_color &c : layer)
            c = rockcat_color{0, 0, 0, 0};
        layer[61] = rockcat_color{255, 0, 255, 0};
        CHECK(rockcat_shift_layer_set(ROCKCAT_PID_RYOS_MK_FX, 0, 1, layer, 120) == ROCKCAT_OK);
        int on = 0;
        rockcat_color back[120];
        CHECK(rockcat_shift_layer_get(ROCKCAT_PID_RYOS_MK_FX, 0, &on, back, 120) == ROCKCAT_OK && on == 1);
        CHECK(back[61].g == 255 && back[61].a == 255 && back[60].a == 0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the engine notices the config by itself
    {
        std::lock_guard<std::mutex> g(sim.lock);
        sim.inputs.push_back({0x03, 0x00, 0xfd, 0x01, 0x00});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> g(sim.lock);
        CHECK(sim.custom == 1);
    }
    CHECK(rockcat_engine_get_frame(engine, frame, 120) == 120);
    CHECK(frame[61].g == 255 && frame[61].r == 0 && frame[60].g == 0);
    {
        std::lock_guard<std::mutex> g(sim.lock);
        sim.inputs.push_back({0x03, 0x00, 0xfd, 0x00, 0x00});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> g(sim.lock);
        CHECK(sim.custom == 0);
    }
    rockcat_engine_stop(engine);
    rockcat_close(dev);
    unlink((runtime + "/rockcat-engine-2fda.lock").c_str());
    rmdir(runtime.c_str());
    return failures ? 1 : 0;
}

// a real Kova 2016 Gray was read out once (tools/capture/kova2016.sh). this reads that recording back through the family code,
// so every number checked here is something an actual mouse said and not something we made up
static int kova_checks(const char *file)
{
    int failures = 0;
    rockcat_device_info info;
    std::unique_ptr<transport> rep = transport_replay(file, &info);
    if (!rep)
        return fail("cant read the recording");
    rockcat_device *dev = device_from_transport(std::move(rep), info);
    const rockcat_caps *caps = rockcat_get_caps(dev);
    CHECK(caps && std::strcmp(caps->family, "kova") == 0 && caps->led_count == 2 && caps->profile_count == 5);
    if (!caps)
        return 1;

    char fw[16] = "";
    CHECK(rockcat_get_firmware_version(dev, fw, sizeof fw) == ROCKCAT_OK && std::strcmp(fw, "1.18") == 0);
    uint8_t active = 9, count = 0;
    CHECK(rockcat_get_active_profile(dev, &active, &count) == ROCKCAT_OK && active == 0 && count == 5);

    rockcat_profile *p[5] = {};
    for (int i = 0; i < 5; ++i)
        CHECK(rockcat_profile_read(dev, i, &p[i]) == ROCKCAT_OK && p[i]);
    // nothing but what the recording has was asked for, and all of it was used
    CHECK(rockcat_replay_verdict(dev, 1) == ROCKCAT_OK);
    if (!p[0] || !p[1] || !p[4])
        return 1;

    // profile 1 is the one its owner actually uses: two dpi levels enabled, own colour on both lights
    const int dpi0[5] = {400, 800, 1000, 1600, 2250};
    for (int i = 0; i < 5; ++i) {
        int x = 0, y = 0, on = -1;
        CHECK(rockcat_profile_get_dpi(p[0], i, &x, &y, &on) == ROCKCAT_OK && x == dpi0[i] && y == x && on == (i == 1 || i == 2));
    }
    int v = -1;
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_DPI_ACTIVE, &v) == ROCKCAT_OK && v == 2);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_POLLING_HZ, &v) == ROCKCAT_OK && v == 1000);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_EFFECT, &v) == ROCKCAT_OK && v == ROCKCAT_EFFECT_FULLY_LIT);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_LEFT_HANDED, &v) == ROCKCAT_OK && v == 0);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_COLOR_FLOW, &v) == ROCKCAT_OK && v == 0);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_BRIGHTNESS, &v) == ROCKCAT_ERR_UNSUPPORTED);
    for (int led = 0; led < 2; ++led) {
        rockcat_color c{};
        int on = 0;
        CHECK(rockcat_profile_get_led(p[0], led, &c, &on) == ROCKCAT_OK && c.r == 0 && c.g == 0 && c.b == 0xff && on == 1);
    }
    CHECK(rockcat_profile_get_led(p[0], 2, nullptr, nullptr) == ROCKCAT_ERR_ARGUMENT);
    rockcat_assignment a{};
    CHECK(rockcat_profile_get_input(p[0], 0, 0, &a) == ROCKCAT_OK && a.type == ROCKCAT_BT_CLICK);
    CHECK(rockcat_profile_get_input(p[0], 0, 5, &a) == ROCKCAT_OK && a.type == ROCKCAT_BT_EASYSHIFT);
    // profile 2 has a shortcut on a side button: type 5 with modifier and key
    CHECK(rockcat_profile_get_input(p[1], 0, 7, &a) == ROCKCAT_OK && a.type == ROCKCAT_BT_SHORTCUT && a.param1 == 0x00 && a.param2 == 0x1c);
    // profile 5 looks untouched: factory dpi steps, all five enabled
    const int dpi4[5] = {400, 800, 1600, 3500, 7000};
    for (int i = 0; i < 5; ++i) {
        int x = 0, on = 0;
        CHECK(rockcat_profile_get_dpi(p[4], i, &x, nullptr, &on) == ROCKCAT_OK && x == dpi4[i] && on == 1);
    }

    // changes stay in the object and come out as the bytes roccat-tools documents
    CHECK(rockcat_profile_set_dpi(p[4], 0, 400, 800, 1) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_dpi(p[4], 0, 200, 200, 1) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_dpi(p[4], 0, 1200, 1200, 0) == ROCKCAT_OK);
    CHECK(rockcat_profile_set_int(p[4], ROCKCAT_SET_EFFECT, ROCKCAT_EFFECT_AIMO) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_int(p[4], ROCKCAT_SET_EFFECT, ROCKCAT_EFFECT_BREATHING) == ROCKCAT_OK);
    CHECK(rockcat_profile_set_led(p[4], 1, rockcat_color{255, 0x11, 0x22, 0x33}, 0) == ROCKCAT_OK);
    uint8_t blob[256];
    int n = rockcat_profile_export(p[4], blob, sizeof blob);
    CHECK(n == 8 + 28 + 75);
    const uint8_t *st = blob + 8;
    CHECK(st[6] == 0x1e && st[7] == 24 && st[16] == 3);
    CHECK(st[14] == (0x03 | 0x20) - 0x02 && st[22] == 0x02 && st[23] == 0x11 && st[24] == 0x22 && st[25] == 0x33);
    CHECK(st[4] == 0x06 && st[5] == 0xff && st[18] == 0x02);
    for (rockcat_profile *q : p)
        rockcat_profile_free(q);
    rockcat_close(dev);
    return failures ? 1 : 0;
}

// a real Ryos MK FX was read out once (tools/capture/ryos-mk-fx.sh), all five profiles. same idea as for the Kova: what gets
// checked here is what the keyboard said. profile 1 is in use, 2 to 4 look half set up, 5 has an empty key table
static int ryos_checks(const char *file)
{
    int failures = 0;
    rockcat_device_info info;
    std::unique_ptr<transport> rep = transport_replay(file, &info);
    if (!rep)
        return fail("cant read the recording");
    rockcat_device *dev = device_from_transport(std::move(rep), info);
    const rockcat_caps *caps = rockcat_get_caps(dev);
    CHECK(caps && caps->kind == ROCKCAT_KIND_KEYBOARD && std::strcmp(caps->family, "ryos") == 0 && caps->led_count == 120 && caps->input_count == 124);
    if (!caps)
        return 1;

    char fw[16] = "";
    CHECK(rockcat_get_firmware_version(dev, fw, sizeof fw) == ROCKCAT_OK && std::strcmp(fw, "1.31") == 0);
    uint8_t active = 9, count = 0;
    CHECK(rockcat_get_active_profile(dev, &active, &count) == ROCKCAT_OK && active == 0 && count == 5);
    rockcat_profile *p[5] = {};
    for (int i = 0; i < 5; ++i)
        CHECK(rockcat_profile_read(dev, i, &p[i]) == ROCKCAT_OK && p[i]);
    // nothing but what the recording has was asked for, and all of it was used
    CHECK(rockcat_replay_verdict(dev, 1) == ROCKCAT_OK);
    if (!p[0] || !p[1] || !p[4])
        return 1;

    // lights of profile 1: fully lit, every key the same red, 15 minutes until the starlit sky comes
    int v = -1;
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_EFFECT, &v) == ROCKCAT_OK && v == ROCKCAT_EFFECT_FULLY_LIT);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_EFFECT_SPEED, &v) == ROCKCAT_OK && v == 3);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_BRIGHTNESS, &v) == ROCKCAT_OK && v == 255);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_IDLE_MINUTES, &v) == ROCKCAT_OK && v == 15);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_IDLE_STYLE, &v) == ROCKCAT_OK && v == ROCKCAT_IDLE_STARLIT_SKY);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_IDLE_BRIGHTNESS, &v) == ROCKCAT_OK && v == 0);
    CHECK(rockcat_profile_get_int(p[0], ROCKCAT_SET_POLLING_HZ, &v) == ROCKCAT_ERR_UNSUPPORTED);
    CHECK(rockcat_profile_get_dpi(p[0], 0, nullptr, nullptr, nullptr) == ROCKCAT_ERR_UNSUPPORTED);
    for (int led : {0, 61, 119}) {
        rockcat_color c{};
        int on = 0;
        CHECK(rockcat_profile_get_led(p[0], led, &c, &on) == ROCKCAT_OK && c.r == 0xff && c.g == 0 && c.b == 0 && on == 1);
    }
    CHECK(rockcat_profile_get_led(p[0], 120, nullptr, nullptr) == ROCKCAT_ERR_ARGUMENT);
    // profile 2 says "ripple", one of the effects Swarm drew from the pc. the engine draws that one, so it can be picked
    // and is stored under the keyboards own number for it (6), not parked as AIMO the way a Kone does it. the two
    // nobody draws yet (equalizer, game preset) can not
    int by_engine = 0;
    CHECK(rockcat_profile_get_effect(p[1], &v, &by_engine) == ROCKCAT_OK && v == ROCKCAT_EFFECT_RIPPLE && by_engine == 1);
    CHECK(rockcat_profile_get_int(p[1], ROCKCAT_SET_BRIGHTNESS, &v) == ROCKCAT_OK && v == 51);
    CHECK(rockcat_profile_select_effect(p[1], ROCKCAT_EFFECT_WAVE, 0) == ROCKCAT_OK);
    CHECK(rockcat_profile_get_int(p[1], ROCKCAT_SET_EFFECT, &v) == ROCKCAT_OK && v == ROCKCAT_EFFECT_WAVE);
    // fire is ours and the keyboard has no number for it: it gets "game preset" (the carrier) and the pc remembers the rest
    CHECK(rockcat_profile_set_int(p[1], ROCKCAT_SET_EFFECT, ROCKCAT_EFFECT_FIRE) == ROCKCAT_ERR_UNSUPPORTED);
    CHECK(rockcat_profile_select_effect(p[1], ROCKCAT_EFFECT_FIRE, 0) == ROCKCAT_OK);
    CHECK(rockcat_profile_get_int(p[1], ROCKCAT_SET_EFFECT, &v) == ROCKCAT_OK && v == ROCKCAT_EFFECT_GAME_PRESET);
    CHECK(rockcat_profile_get_effect(p[1], &v, &by_engine) == ROCKCAT_OK && v == ROCKCAT_EFFECT_FIRE && by_engine == 1);
    CHECK(rockcat_profile_select_effect(p[1], ROCKCAT_EFFECT_SNAKE, 0) == ROCKCAT_ERR_ARGUMENT);

    // keys of profile 1, which is the factory layout: every kind of key sits in another report
    const struct {
        int layer, input, type;
    } keys[] = {
        {0, 1, 0x29},                   // Esc, plain key in the primary report
        {0, 61, 0x04},                  // A
        {1, 61, 0x04},                  // A with Easy-Shift: still A out of the box
        {0, 2, 0x3a},                   // F1, function report
        {2, 2, ROCKCAT_KF_MUTE},        // and with Fn held, the one printed on the key (mute)
        {0, 0, ROCKCAT_KF_SHIFT_1},     // M1 is shift+1 and ctrl+1
        {1, 0, ROCKCAT_KF_CTRL_1},
        {0, 96, ROCKCAT_KF_SHIFT_1 + 4}, // M5
        {0, 121, ROCKCAT_KF_PROFILE_DOWN}, // T1 to T3
        {0, 122, ROCKCAT_KF_OPEN_DRIVER},
        {0, 123, ROCKCAT_KF_PROFILE_UP},
        {1, 121, 0x39},
        {0, 60, ROCKCAT_KF_EASYSHIFT},  // Caps Lock is the Easy-Shift key
        {0, 115, ROCKCAT_KF_FN},
    };
    rockcat_assignment a{};
    for (const auto &k : keys)
        CHECK(rockcat_profile_get_input(p[0], k.layer, k.input, &a) == ROCKCAT_OK && a.type == k.type && a.param1 == 0 && a.param2 == 0);
    CHECK(rockcat_profile_get_input(p[0], 1, 1, &a) == ROCKCAT_ERR_UNSUPPORTED);   // Esc has no Easy-Shift side
    CHECK(rockcat_profile_get_input(p[0], 1, 60, &a) == ROCKCAT_ERR_UNSUPPORTED);
    CHECK(rockcat_profile_get_input(p[0], 0, 120, &a) == ROCKCAT_ERR_ARGUMENT);    // the gap in front of the thumb keys
    CHECK(rockcat_profile_get_input(p[0], 0, 71, &a) == ROCKCAT_ERR_ARGUMENT);     // a matrix slot without a key
    CHECK(rockcat_profile_get_input(p[0], 0, 124, &a) == ROCKCAT_ERR_ARGUMENT);
    // Fn is a layer only the function keys have, and they in turn have nothing of their own with Easy-Shift
    CHECK(rockcat_profile_get_input(p[0], 2, 1, &a) == ROCKCAT_ERR_UNSUPPORTED);
    CHECK(rockcat_profile_get_input(p[0], 1, 2, &a) == ROCKCAT_ERR_UNSUPPORTED);
    CHECK(rockcat_profile_get_input(p[0], 3, 2, &a) == ROCKCAT_ERR_ARGUMENT);
    CHECK(caps->layer_count == 3 && std::strcmp(rockcat_layer_label(caps, 2), "Fn") == 0);
    // profile 5: no key does anything, the reports are there and empty
    CHECK(rockcat_profile_get_input(p[4], 0, 61, &a) == ROCKCAT_OK && a.type == 0);
    CHECK(rockcat_profile_get_input(p[4], 0, 2, &a) == ROCKCAT_OK && a.type == 0);
    // names: keys come out of the key table, functions out of the family
    CHECK(std::strcmp(rockcat_input_label(caps, 1), "Esc") == 0 && std::strcmp(rockcat_input_label(caps, 121), "T1") == 0);
    CHECK(std::strcmp(rockcat_input_label(caps, 120), "?") == 0);
    CHECK(std::strcmp(rockcat_assignment_name(caps, ROCKCAT_KF_SHIFT_1), "shift-1") == 0);
    CHECK(std::strcmp(rockcat_assignment_name(caps, 0x04), "a") == 0 && std::strcmp(rockcat_assignment_name(caps, 0xff), "easyshift") == 0);

    // changes. a one byte key cant take parameters, the three byte ones can
    rockcat_assignment shortcut{ROCKCAT_KF_SHORTCUT, 0x01, 0x06};
    CHECK(rockcat_profile_set_input(p[4], 0, 61, &shortcut) == ROCKCAT_ERR_ARGUMENT);
    CHECK(rockcat_profile_set_input(p[4], 1, 61, &shortcut) == ROCKCAT_OK);
    CHECK(rockcat_profile_get_input(p[4], 1, 61, &a) == ROCKCAT_OK && a.type == ROCKCAT_KF_SHORTCUT && a.param1 == 1 && a.param2 == 6);
    rockcat_assignment b{0x05, 0, 0};
    CHECK(rockcat_profile_set_input(p[4], 0, 61, &b) == ROCKCAT_OK);
    CHECK(rockcat_profile_reset_inputs(p[4]) == ROCKCAT_OK);
    for (const auto &k : keys)
        CHECK(rockcat_profile_get_input(p[4], k.layer, k.input, &a) == ROCKCAT_OK && a.type == k.type);
    // heartbeat is something this keyboard does itself, so it must not get parked as "AIMO, drawn by the pc" like on the Kone
    by_engine = 1;
    CHECK(rockcat_profile_select_effect(p[4], ROCKCAT_EFFECT_HEARTBEAT, 0) == ROCKCAT_OK);
    CHECK(rockcat_profile_get_effect(p[4], &v, &by_engine) == ROCKCAT_OK && v == ROCKCAT_EFFECT_HEARTBEAT && by_engine == 0);
    CHECK(rockcat_profile_set_int(p[4], ROCKCAT_SET_BRIGHTNESS, 100) == ROCKCAT_OK);
    CHECK(rockcat_profile_get_int(p[4], ROCKCAT_SET_BRIGHTNESS, &v) == ROCKCAT_OK && v == 102); // 2 of 5 steps
    CHECK(rockcat_profile_set_int(p[4], ROCKCAT_SET_EFFECT_SPEED, 0) == ROCKCAT_ERR_ARGUMENT);

    // colours. seven different values per channel fit exactly, and they survive the trip through the reports
    const uint8_t seven[7] = {0, 40, 80, 120, 160, 200, 255};
    for (int led = 0; led < 120; ++led)
        CHECK(rockcat_profile_set_led(p[4], led, rockcat_color{255, seven[led % 7], seven[(led / 7) % 7], seven[(led / 49) % 7]}, led != 5) == ROCKCAT_OK);
    static uint8_t blob[2048];
    int n = rockcat_profile_export(p[4], blob, sizeof blob);
    CHECK(n == 8 + 857);
    rockcat_profile *back = nullptr;
    CHECK(n > 0 && rockcat_profile_import(dev, blob, static_cast<size_t>(n), &back) == ROCKCAT_OK && back);
    // (a slot without a key comes back black, thats checked further down. here its about the keys)
    auto real = [caps](int led) { return std::strcmp(rockcat_led_label(caps, led), "?") != 0; };
    for (int led = 0; back && led < 120; ++led) {
        rockcat_color c{};
        int on = -1;
        CHECK(rockcat_profile_get_led(back, led, &c, &on) == ROCKCAT_OK && (on == (led != 5) || !real(led)));
        if (led != 5 && real(led))
            CHECK(c.r == seven[led % 7] && c.g == seven[(led / 7) % 7] && c.b == seven[(led / 49) % 7]);
    }
    rockcat_profile_free(back);
    // more values than the keyboard has room for: every key ends up near what was asked, none way off
    for (int led = 0; led < 120; ++led)
        rockcat_profile_set_led(p[4], led, rockcat_color{255, static_cast<uint8_t>(led * 2), static_cast<uint8_t>(255 - led * 2), 7}, 1);
    n = rockcat_profile_export(p[4], blob, sizeof blob);
    back = nullptr;
    CHECK(rockcat_profile_import(dev, blob, static_cast<size_t>(n), &back) == ROCKCAT_OK && back);
    for (int led = 0; back && led < 120; ++led) {
        rockcat_color c{};
        CHECK(rockcat_profile_get_led(back, led, &c, nullptr) == ROCKCAT_OK);
        if (real(led))
            CHECK(std::abs(c.r - led * 2) <= 30 && std::abs(c.g - (255 - led * 2)) <= 30 && std::abs(c.b - 7) <= 7);
    }
    rockcat_profile_free(back);
    // the nine slots of the light matrix that have no key stay dark whatever gets asked for (lighting them makes other
    // keys glow on a real keyboard). every key was just set, so look at what would be sent: slot 71 and its neighbour 70
    {
        const uint8_t *stored = blob + 8 + 125 + 95 + 35 + 23 + 8 + 294 + 6 + 32, *layer = stored + 3;
        CHECK(stored[0] == 0x17);
        auto number = [layer](int channel, int key) { const uint8_t b = layer[15 + 60 * channel + key / 2]; return key & 1 ? b >> 4 : b & 0x0f; };
        auto pwm = [&](int channel, int key) { return layer[195 + 7 * channel + number(channel, key) - 1]; };
        for (int slot : {32, 71, 91, 100, 101, 102, 111, 113, 119})
            CHECK(number(0, slot) >= 1 && pwm(0, slot) == 0 && pwm(1, slot) == 0 && pwm(2, slot) == 0);
        CHECK(pwm(0, 70) != 0 || pwm(1, 70) != 0);
    }
    // all keys the same again: thats plain mode, one colour in the light settings (byte 6 of report 0d is the mode)
    const uint8_t *light = blob + 8 + 125 + 95 + 35 + 23 + 8 + 294 + 6;
    CHECK(light[0] == 0x0d && light[6] == 1);
    for (int led = 0; led < 120; ++led)
        rockcat_profile_set_led(p[4], led, rockcat_color{255, 0, 0x80, 0xff}, 1);
    n = rockcat_profile_export(p[4], blob, sizeof blob);
    CHECK(light[6] == 0 && light[14] == 0x00 && light[15] == 0x0f && light[18] == 0x3f && light[19] == 0x0f);
    // and whatever we write has its checksum right, the keyboard throws it away otherwise
    CHECK((light[30] | (light[31] << 8)) == rockcat_checksum(light, 30));

    for (rockcat_profile *q : p)
        rockcat_profile_free(q);
    rockcat_close(dev);
    return failures ? 1 : 0;
}

// the key tables come out of a generator, so check the result and not the generator: counts, the anchors we know, nothing off the picture
static int keyboard_checks()
{
    int failures = 0;
    const keyboard_layout &kb = RYOS_MK_FX_KEYBOARD;
    CHECK(kb.region_count == 5);
    for (int r = 0; r < kb.region_count; ++r) {
        const keyboard_region &reg = kb.regions[r];
        // ISO boards have one key more than ANSI, the one next to the left shift
        CHECK(reg.count == (reg.id == 0 ? 112 : 113));
        bool seen[256] = {};
        for (int i = 0; i < reg.count; ++i) {
            const rockcat_key &k = reg.keys[i];
            CHECK(k.name && *k.name && k.led >= 0 && k.led < 124 && !seen[k.led]);
            seen[k.led & 0xff] = true;
            CHECK(k.x >= 0 && k.y >= 0 && k.w > 0 && k.h > 0 && k.x + k.w <= kb.picture_width && k.y + k.h <= kb.picture_height);
        }
        CHECK(std::strcmp(reg.keys[0].name, "Esc") == 0 && reg.keys[0].led == 1);
    }
    // nothing in the device table is a keyboard yet, so the public calls have to say "not a keyboard" and not fall over
    const rockcat_caps *mouse = nullptr;
    rockcat_model_at(0, nullptr, nullptr, &mouse);
    CHECK(rockcat_keyboard_regions(mouse, nullptr, 0) == 0 && rockcat_keyboard_keys(mouse, 1, nullptr) == 0);
    return failures ? 1 : 0;
}

// the library keeps a few things in the users config folder (which effect the pc draws, its settings). a test has no
// business in there, it gets a folder of its own that is gone again when its done
struct own_config {
    std::string dir = "/tmp/rockcat-test-config-" + std::to_string(getpid());
    own_config()
    {
        mkdir(dir.c_str(), 0700);
        setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    }
    ~own_config()
    {
        std::string inner = dir + "/rockcat";
        for (const char *name : {"/2e2c.ini", "/2cee.ini", "/2fda.ini", "/2e2c.ini.tmp", "/2cee.ini.tmp", "/2fda.ini.tmp"})
            unlink((inner + name).c_str());
        rmdir(inner.c_str());
        rmdir(dir.c_str());
    }
};

int main(int argc, char **argv)
{
    own_config config;
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "profile")
        return profile_checks();
    if (mode == "render")
        return render_check(argc > 2 ? argv[2] : nullptr);
    if (mode == "events")
        return event_checks() + ryos_event_checks() ? 1 : 0;
    if (mode == "keyboard")
        return keyboard_checks();
    if (mode == "kova")
        return argc > 2 ? kova_checks(argv[2]) : fail("kova needs the recording");
    if (mode == "kb-engine")
        return keyboard_engine_checks();
    if (mode == "ryos")
        return argc > 2 ? ryos_checks(argv[2]) : fail("ryos needs the recording");
    if (argc != 3)
        return fail("usage: rockcat-test-replay check|parse|diverge|regen FILE, or rockcat-test-replay profile");
    std::string file = argv[2];

    if (mode == "regen") {
        std::remove(file.c_str());
        rockcat_device *dev = device_from_transport(transport_record(std::unique_ptr<transport>(new kone_sim), file, sim_info()), sim_info());
        session_old(dev);
        rockcat_close(dev);
        std::printf("wrote %s\n", file.c_str());
        return 0;
    }

    rockcat_device_info info;
    std::unique_ptr<transport> rep = transport_replay(file, &info);
    if (!rep)
        return fail("cant read the recording");
    // enough for the recordings of devices the library cant talk to yet, they only have to stay readable
    if (mode == "parse")
        return info.vendor_id == ROCKCAT_VID ? 0 : fail("recording has no sane device line");
    if (info.product_id != ROCKCAT_PID_KONE_AIMO_REMASTERED)
        return fail("thats a recording of some other device");
    rockcat_device *replayed = device_from_transport(std::move(rep), info);

    if (mode == "diverge") {
        // the recording starts with a firmware version read, a profile switch is not what it expects
        int r = rockcat_set_active_profile(replayed, 3);
        int v = rockcat_replay_verdict(replayed, 0);
        rockcat_close(replayed);
        return r != ROCKCAT_OK && v != ROCKCAT_OK ? 0 : fail("the replay let wrong traffic through");
    }
    if (mode != "check")
        return fail("usage: rockcat-test-replay check|parse|diverge|regen FILE, or rockcat-test-replay profile");

    struct {
        const char *name;
        std::string (*run)(rockcat_device *);
    } sessions[] = {{"old api", session_old}, {"profile api", session_new}};
    for (auto &s : sessions) {
        rockcat_device *sim = device_from_transport(std::unique_ptr<transport>(new kone_sim), sim_info());
        std::string want = s.run(sim);
        rockcat_close(sim);
        if (!replayed)
            replayed = device_from_transport(transport_replay(file, &info), info);
        std::string got = s.run(replayed);
        int verdict = rockcat_replay_verdict(replayed, 1);
        rockcat_close(replayed);
        replayed = nullptr;

        std::fprintf(stderr, "%s:\n", s.name);
        if (want.find("=-") != std::string::npos)
            return fail("the session against the simulated mouse had errors, fix that first");
        if (verdict != ROCKCAT_OK)
            return fail("traffic no longer matches the recording");
        if (want != got)
            return fail("same traffic but different results, the decoding changed");
    }
    return 0;
}
