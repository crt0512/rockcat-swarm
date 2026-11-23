// pretend Nyth, Tyon, Kiro and Savu, one class driven by the same LAYOUTS as the real code. starts on the factory reports,
// answers selects, sizes and checksums the way roccat-tools says the mice do. included at the end of classic.cpp
// one shared mouse per pid, gui and engine see the same one

struct classic_sim final : transport {
    const layout &L;
    uint8_t active = 0;
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> settings[5], buttons[5], extra[5];
    std::map<int, std::vector<uint8_t>> macros; // profile * 256 + slot -> macro body (halves glued, or the single report)
    int macro_slot = 0;
    std::mutex lock;

    static void seal(std::vector<uint8_t> &b, int at)
    {
        if (at >= 0)
            put16(b.data() + at, sum16(b.data(), at));
    }

    explicit classic_sim(const layout &l) : L(l)
    {
        for (uint8_t p = 0; p < 5; ++p) {
            settings[p].assign(L.factory_settings, L.factory_settings + L.settings_size);
            if (L.buttons_size) {
                buttons[p].assign(L.buttons_size, 0);
                if (L.factory_buttons)
                    buttons[p].assign(L.factory_buttons, L.factory_buttons + L.buttons_size);
                buttons[p][0] = REPORT_BUTTONS;
                buttons[p][1] = static_cast<uint8_t>(L.buttons_size);
                if (L.factory_types)
                    for (int i = 0; i < L.inputs * L.layers; ++i)
                        buttons[p][L.buttons_at + 3 * i] = L.factory_types[i];
            }
            if (L.profile_at >= 0) {
                settings[p][L.profile_at] = p;
                if (L.buttons_size)
                    buttons[p][L.profile_at] = p;
            }
            // something to tell the profiles apart by: the active dpi level moves along
            settings[p][L.dpi_active_at] = static_cast<uint8_t>(p % L.dpi_levels);
            seal(settings[p], L.settings_sum_at);
            if (L.buttons_size)
                seal(buttons[p], L.buttons_sum_at);
            if (L.extra.size) {
                extra[p].assign(L.extra.size, 0);
                extra[p][0] = L.extra.id;
                extra[p][1] = static_cast<uint8_t>(L.extra.size);
                extra[p][2] = p;
            }
        }
    }

    bool selected(uint8_t request) const { return select_b == request && select_a < 5; }

    int get_feature(uint8_t *buf, size_t len) override
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
            out = {REPORT_PROFILE, static_cast<uint8_t>(L.profiles == PROFILE_4 ? 4 : 3), active, 5};
            break;
        case REPORT_SETTINGS:
            if (L.sel_settings && !selected(L.sel_settings))
                return ROCKCAT_ERR_IO;
            out = settings[L.sel_settings ? select_a : 0];
            break;
        case REPORT_BUTTONS:
            if (!L.buttons_size || !selected(L.sel_buttons))
                return ROCKCAT_ERR_IO;
            out = buttons[select_a];
            break;
        case REPORT_MACRO: {
            if (L.macros == MACRO_NONE)
                return ROCKCAT_ERR_IO;
            const bool halves = L.macros == MACRO_HALVES;
            const int profile = halves ? select_a & 0x0f : select_a;
            auto it = macros.find(profile * 256 + select_b);
            if (halves) {
                const int half = (select_a & 0x20) ? 1 : 0;
                out.assign(0x402, 0xff);
                out[0] = REPORT_MACRO;
                out[1] = static_cast<uint8_t>(half + 1);
                if (it != macros.end())
                    std::memcpy(out.data() + 2, it->second.data() + half * 0x400, 0x400);
            } else if (it != macros.end()) {
                out = it->second;
            } else {
                out.assign(MACRO_SINGLE_SIZE, 0xff);
                out[0] = REPORT_MACRO;
            }
            break;
        }
        case REPORT_INFO:
            out.assign(L.info_size, 0);
            out[0] = REPORT_INFO;
            out[1] = L.info_size;
            out[2] = 0x6e; // 1.10
            break;
        default:
            if (L.extra.size && buf[0] == L.extra.id && selected(L.extra.request)) {
                out = extra[select_a];
                break;
            }
            return ROCKCAT_ERR_IO;
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    // a write with the wrong size, profile or checksum gets "no" from the status, like the real ones do
    int take(std::vector<uint8_t> *store, const uint8_t *buf, size_t len, size_t size, int sum_at)
    {
        const int profile = L.profile_at >= 0 && len > static_cast<size_t>(L.profile_at) ? buf[L.profile_at] : 0;
        const bool sum_ok = sum_at < 0 || (len > static_cast<size_t>(sum_at + 1) && get16(buf + sum_at) == sum16(buf, sum_at));
        if (len != size || profile >= 5 || !sum_ok) {
            status_queue = {2};
            return ROCKCAT_OK;
        }
        store[profile].assign(buf, buf + len);
        status_queue = {3};
        return ROCKCAT_OK;
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            return ROCKCAT_OK;
        case REPORT_PROFILE:
            if (L.profiles == PROFILE_ONE || len < 3 || buf[2] >= 5)
                return ROCKCAT_ERR_IO;
            active = buf[2];
            return ROCKCAT_OK;
        case REPORT_SETTINGS:
            return take(settings, buf, len, L.settings_size, L.settings_sum_at);
        case REPORT_BUTTONS:
            if (!L.buttons_size)
                return ROCKCAT_ERR_IO;
            return take(buttons, buf, len, L.buttons_size, L.buttons_sum_at);
        case REPORT_MACRO:
            if (L.macros == MACRO_HALVES) {
                if (len != 0x402)
                    return ROCKCAT_ERR_IO;
                if (buf[1] == 1)
                    macro_slot = buf[2] * 256 + buf[3];
                macros[macro_slot].resize(0x800);
                std::memcpy(macros[macro_slot].data() + (buf[1] == 2 ? 0x400 : 0), buf + 2, 0x400);
                return ROCKCAT_OK;
            }
            if (L.macros == MACRO_SINGLE && len == MACRO_SINGLE_SIZE && get16(buf + len - 2) == sum16(buf, static_cast<int>(len - 2))) {
                macros[buf[3] * 256 + buf[4]].assign(buf, buf + len);
                status_queue = {3};
                return ROCKCAT_OK;
            }
            return ROCKCAT_ERR_IO;
        default:
            if (L.extra.size && buf[0] == L.extra.id)
                return take(extra, buf, len, L.extra.size, -1);
            return ROCKCAT_ERR_IO;
        }
    }

    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

classic_sim &shared_classic(uint16_t pid)
{
    static std::map<uint16_t, classic_sim> sims;
    static std::mutex guard;
    std::lock_guard<std::mutex> g(guard);
    auto it = sims.find(pid);
    if (it == sims.end())
        it = sims.emplace(std::piecewise_construct, std::forward_as_tuple(pid), std::forward_as_tuple(layout_of(pid))).first;
    return it->second;
}

template <uint16_t PID> struct classic_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        classic_sim &s = shared_classic(PID);
        std::lock_guard<std::mutex> g(s.lock);
        return s.get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        classic_sim &s = shared_classic(PID);
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

template <uint16_t PID> std::unique_ptr<transport> make_classic_door()
{
    return std::unique_ptr<transport>(new classic_door<PID>);
}

const sim_registration sim_nyth({"nyth", 0x2e7c, "ROCCAT Nyth Black", 0, make_classic_door<0x2e7c>});
const sim_registration sim_tyon({"tyon", 0x2e4a, "ROCCAT Tyon Black", 0, make_classic_door<0x2e4a>});
const sim_registration sim_kiro({"kiro", 0x2c38, "ROCCAT Kiro", 0, make_classic_door<0x2c38>});
const sim_registration sim_savu({"savu", 0x2d5a, "ROCCAT Savu", 0, make_classic_door<0x2d5a>});
const sim_registration sim_owl({"kone-pure-owl-eye", 0x2dcd, "ROCCAT Kone Pure Owl-Eye", 0, make_classic_door<0x2dcd>});
const sim_registration sim_leadr({"leadr", 0x2e4c, "ROCCAT Leadr", 0, make_classic_door<0x2e4c>});
const sim_registration sim_kova_aimo({"kova-aimo", 0x2cf1, "ROCCAT Kova AIMO", 0, make_classic_door<0x2cf1>});
const sim_registration sim_emp({"kone-emp", 0x2e24, "ROCCAT Kone EMP", 0, make_classic_door<0x2e24>});
