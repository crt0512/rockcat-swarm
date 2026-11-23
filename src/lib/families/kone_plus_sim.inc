// pretend Kone[+], XTD, XTD Optical, Pure, Pure Optical and Pure Military. they answer like docs/protocol/kone-plus.md says
// (select, status, sizes, checksums) and start out with the roccat-tools defaults. one shared mouse per name, so the gui and an
// engine in the same program see the same thing. they forget it all on exit, unless ROCKCAT_SIM_STATE points at a directory:
// then every write lands in DIR/<name>.bin and the next run picks it up (handy for poking at it with the cli one command at a time)
// included at the end of kone_plus.cpp, it needs the variant tables from there

namespace {

constexpr int DEFAULT_DPI_100[5] = {400, 800, 1600, 3200, 6000}, DEFAULT_DPI_200[5] = {800, 1600, 3200, 6400, 8200},
              DEFAULT_DPI_5000[5] = {400, 800, 1600, 3200, 5000};

struct kone_plus_sim {
    const char *name;
    const variant &v;
    uint8_t active = 0, select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> settings[PROFILES], buttons[PROFILES];
    std::map<int, std::vector<uint8_t>> macros; // profile * 256 + button -> the whole report
    uint8_t talk[16] = {};
    std::mutex lock;

    kone_plus_sim(const char *n, const variant &var) : name(n), v(var)
    {
        for (int p = 0; p < PROFILES; ++p) {
            settings[p] = factory_settings(p);
            buttons[p] = factory_buttons(p);
        }
        load();
    }

    std::vector<uint8_t> factory_settings(int p) const
    {
        std::vector<uint8_t> s(v.settings_size, 0);
        s[0] = REPORT_SETTINGS;
        s[1] = static_cast<uint8_t>(v.settings_size);
        s[2] = static_cast<uint8_t>(p);
        s[4] = s[5] = 6; // sensitivity, 6 is the middle
        s[6] = 0x1f;
        // what the default rmp files of roccat-tools have
        const int *dpi = DEFAULT_DPI_100;
        if (&v == &KONE_XTD_V || &v == &KONE_PURE_V)
            dpi = DEFAULT_DPI_200;
        else if (&v == &KONE_XTD_OPTICAL_V || &v == &KONE_PURE_MILITARY_V)
            dpi = DEFAULT_DPI_5000;
        for (int l = 0; l < 5; ++l) {
            uint8_t raw = 0;
            if (v.cpi_table) {
                const int pick[5] = {0, 1, 2, 4, 6};
                raw = PURE_OPTICAL_CODE[pick[l]];
            } else {
                raw = static_cast<uint8_t>(dpi[l] / v.dpi_unit);
            }
            s[static_cast<size_t>(v.o_cpi_x + l)] = raw;
            if (v.o_cpi_y >= 0)
                s[static_cast<size_t>(v.o_cpi_y + l)] = raw;
        }
        s[static_cast<size_t>(v.o_startup)] = 1;
        s[static_cast<size_t>(v.o_polling)] = 3;
        s[static_cast<size_t>(v.o_lights_on)] = static_cast<uint8_t>((1 << v.leds) - 1);
        s[static_cast<size_t>(v.o_flow)] = 1;
        s[static_cast<size_t>(v.o_effect)] = v.effects == EFFECTS_PURE ? 2 : 3;
        s[static_cast<size_t>(v.o_speed)] = 2;
        // palette entry 0x13, a blue (00 64 fa)
        for (int l = 0; l < v.leds; ++l) {
            uint8_t *c = s.data() + v.o_light + 4 * l;
            c[0] = 0x13;
            c[1] = 0x00;
            c[2] = v.blue_green ? 0xfa : 0x64;
            c[3] = v.blue_green ? 0x64 : 0xfa;
        }
        if (v.o_snap >= 0) {
            s[static_cast<size_t>(v.o_snap)] = 2;
            s[static_cast<size_t>(v.o_align)] = 30;
        }
        if (v.settings_checksum)
            put_checksum(s.data(), s.size());
        return s;
    }

    std::vector<uint8_t> factory_buttons(int p) const
    {
        std::vector<uint8_t> b(buttons_size(v), 0);
        b[0] = REPORT_BUTTONS;
        b[1] = static_cast<uint8_t>(b.size());
        b[2] = static_cast<uint8_t>(p);
        for (int i = 0; i < 2 * v.inputs; ++i)
            b[static_cast<size_t>(3 + 3 * i)] = v.factory[i];
        put_checksum(b.data(), b.size());
        return b;
    }

    std::string state_file() const
    {
        const char *dir = std::getenv("ROCKCAT_SIM_STATE");
        return dir && *dir ? std::string(dir) + "/" + name + ".bin" : std::string();
    }

    // the file is just everything one after the other, a macro as [profile button] + its report
    void load()
    {
        std::string path = state_file();
        if (path.empty())
            return;
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            return;
        std::vector<uint8_t> all;
        uint8_t chunk[4096];
        for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;)
            all.insert(all.end(), chunk, chunk + n);
        std::fclose(f);
        size_t need = 1 + PROFILES * (v.settings_size + buttons_size(v));
        if (all.size() < need)
            return;
        size_t at = 0;
        active = all[at++];
        for (int p = 0; p < PROFILES; ++p) {
            settings[p].assign(all.begin() + static_cast<long>(at), all.begin() + static_cast<long>(at + v.settings_size));
            at += v.settings_size;
            buttons[p].assign(all.begin() + static_cast<long>(at), all.begin() + static_cast<long>(at + buttons_size(v)));
            at += buttons_size(v);
        }
        while (at + 2 + M_SIZE <= all.size()) {
            int key = all[at] * 256 + all[at + 1];
            macros[key].assign(all.begin() + static_cast<long>(at + 2), all.begin() + static_cast<long>(at + 2 + M_SIZE));
            at += 2 + M_SIZE;
        }
    }

    void save() const
    {
        std::string path = state_file();
        if (path.empty())
            return;
        std::vector<uint8_t> all{active};
        for (int p = 0; p < PROFILES; ++p) {
            all.insert(all.end(), settings[p].begin(), settings[p].end());
            all.insert(all.end(), buttons[p].begin(), buttons[p].end());
        }
        for (const auto &m : macros) {
            all.push_back(static_cast<uint8_t>(m.first / 256));
            all.push_back(static_cast<uint8_t>(m.first % 256));
            all.insert(all.end(), m.second.begin(), m.second.end());
        }
        if (FILE *f = std::fopen(path.c_str(), "wb")) {
            std::fwrite(all.data(), 1, all.size(), f);
            std::fclose(f);
        }
    }

    int get_feature(uint8_t *buf, size_t len)
    {
        std::vector<uint8_t> out;
        switch (buf[0]) {
        case 0x04: {
            int st = 1;
            if (!status_queue.empty()) {
                st = status_queue.front();
                status_queue.erase(status_queue.begin());
            }
            out = {0x04, static_cast<uint8_t>(st), 0};
            break;
        }
        case REPORT_PROFILE:
            out = {REPORT_PROFILE, 0x03, active};
            break;
        case REPORT_SETTINGS:
            if (select_b != SELECT_SETTINGS || select_a >= PROFILES)
                return ROCKCAT_ERR_IO;
            out = settings[select_a];
            break;
        case REPORT_BUTTONS:
            if (select_b != SELECT_BUTTONS || select_a >= PROFILES)
                return ROCKCAT_ERR_IO;
            out = buttons[select_a];
            break;
        case REPORT_MACRO: {
            if (select_a >= PROFILES || select_b >= 2 * v.inputs)
                return ROCKCAT_ERR_IO;
            auto it = macros.find(select_a * 256 + select_b);
            // a slot nobody wrote to: erased flash, all ff
            if (it == macros.end()) {
                out.assign(M_SIZE, 0xff);
                out[0] = REPORT_MACRO;
            } else {
                out = it->second;
            }
            break;
        }
        case REPORT_INFO:
            out = {REPORT_INFO, 0x06, 100, 1, 0, 0};
            break;
        case REPORT_TALK:
            out.assign(talk, talk + 16);
            out[0] = REPORT_TALK;
            out[1] = 0x10;
            break;
        default:
            return ROCKCAT_ERR_IO;
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    bool sum_ok(const uint8_t *buf, size_t len) const
    {
        return static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8)) == rockcat_checksum(buf, len - 2);
    }

    int set_feature(const uint8_t *buf, size_t len)
    {
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            return ROCKCAT_OK;
        case REPORT_PROFILE:
            if (len != 3 || buf[1] != 3 || buf[2] >= PROFILES) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            active = buf[2];
            break;
        case REPORT_SETTINGS:
            if (len != v.settings_size || buf[1] != len || buf[2] >= PROFILES || (v.settings_checksum && !sum_ok(buf, len))) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            settings[buf[2]].assign(buf, buf + len);
            status_queue = {3};
            break;
        case REPORT_BUTTONS:
            if (len != buttons_size(v) || buf[1] != len || buf[2] >= PROFILES || !sum_ok(buf, len)) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            buttons[buf[2]].assign(buf, buf + len);
            status_queue = {3};
            break;
        case REPORT_MACRO:
            if (len != M_SIZE)
                return ROCKCAT_ERR_IO;
            if (get16(buf + 1) != M_SIZE || buf[M_PROFILE] >= PROFILES || buf[M_BUTTON] >= 2 * v.inputs || !sum_ok(buf, len)) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            macros[buf[M_PROFILE] * 256 + buf[M_BUTTON]].assign(buf, buf + len);
            status_queue = {3, 3};
            break;
        case REPORT_TALK:
            if (len != 16 || buf[1] != 0x10) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            std::memcpy(talk, buf, 16);
            return ROCKCAT_OK;
        default:
            return ROCKCAT_ERR_IO;
        }
        save();
        return ROCKCAT_OK;
    }
};

kone_plus_sim &shared_sim(const char *name, const variant &v)
{
    static std::mutex table_lock;
    static std::map<std::string, std::unique_ptr<kone_plus_sim>> sims;
    std::lock_guard<std::mutex> g(table_lock);
    auto &slot = sims[name];
    if (!slot)
        slot.reset(new kone_plus_sim(name, v));
    return *slot;
}

// one handle on a shared pretend mouse, takes its lock for every transfer and never while waiting
template <const char *NAME, const variant &V>
struct kone_plus_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        kone_plus_sim &s = shared_sim(NAME, V);
        std::lock_guard<std::mutex> g(s.lock);
        return s.get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        kone_plus_sim &s = shared_sim(NAME, V);
        std::lock_guard<std::mutex> g(s.lock);
        return s.set_feature(buf, len);
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

constexpr char SIM_KONE_PLUS[] = "kone-plus", SIM_KONE_XTD[] = "kone-xtd", SIM_KONE_XTD_OPTICAL[] = "kone-xtd-optical",
               SIM_KONE_PURE[] = "kone-pure", SIM_KONE_PURE_OPTICAL[] = "kone-pure-optical", SIM_KONE_PURE_MILITARY[] = "kone-pure-military";

template <const char *NAME, const variant &V>
std::unique_ptr<transport> make_door()
{
    return std::unique_ptr<transport>(new kone_plus_door<NAME, V>);
}

const sim_registration sim_plus({SIM_KONE_PLUS, 0x2d51, "ROCCAT Kone[+]", 0, make_door<SIM_KONE_PLUS, KONE_PLUS_V>});
const sim_registration sim_xtd({SIM_KONE_XTD, 0x2e22, "ROCCAT Kone XTD", 0, make_door<SIM_KONE_XTD, KONE_XTD_V>});
const sim_registration sim_xtd_optical({SIM_KONE_XTD_OPTICAL, 0x2e23, "ROCCAT Kone XTD Optical", 0,
                                        make_door<SIM_KONE_XTD_OPTICAL, KONE_XTD_OPTICAL_V>});
const sim_registration sim_pure({SIM_KONE_PURE, 0x2dbe, "ROCCAT Kone Pure", 0, make_door<SIM_KONE_PURE, KONE_PURE_V>});
const sim_registration sim_pure_optical({SIM_KONE_PURE_OPTICAL, 0x2db4, "ROCCAT Kone Pure Optical", 0,
                                         make_door<SIM_KONE_PURE_OPTICAL, KONE_PURE_OPTICAL_V>});
const sim_registration sim_pure_military({SIM_KONE_PURE_MILITARY, 0x2dbf, "ROCCAT Kone Pure Military", 0,
                                          make_door<SIM_KONE_PURE_MILITARY, KONE_PURE_MILITARY_V>});

} // namespace
