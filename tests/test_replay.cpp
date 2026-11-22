// runs one fixed session against the pretend mouse and against a recording of that same session
// if the library ever sends a different byte than it did when the recording was made, this goes red
// the session exists twice, once through the old struct api and once through the profile api, and both have to
// produce the exact same traffic. thats the proof that moving to the new api changed nothing on the wire
//   rockcat-test-replay check FILE    the test
//   rockcat-test-replay parse FILE    only checks that the file is a readable recording
//   rockcat-test-replay diverge FILE  makes sure a replay actually notices when the code goes off script
//   rockcat-test-replay profile       the profile api against the pretend mouse, no recording involved
//   rockcat-test-replay regen FILE    rewrites the recording. only do that when the traffic was MEANT to change

#include "internal.h"
#include "kone_sim.h"

#include <cstdio>
#include <cstring>
#include <string>

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
    CHECK(std::strcmp(rockcat_input_label(dev, 0), "Left button") == 0 && std::strcmp(rockcat_input_label(dev, 12), "?") == 0);

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

int main(int argc, char **argv)
{
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "profile")
        return profile_checks();
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
