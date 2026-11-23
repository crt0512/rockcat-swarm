// pretend Ryos MK / MK Glow / MK Pro / TKL / TKL Pro. made from the MODELS table in ryos.cpp and roccat-tools, none of
// these sat on a desk while this was written, so it answers what roccat-tools says the boards answer and not a byte more
// included at the end of ryos.cpp (needs MODELS and the factory tables). one shared keyboard per pid, gui and engine see the same

struct ryos_mk_sim final : transport {
    const model &m;
    uint8_t active = 0;
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> data[PROFILES][MAX_PARTS];
    std::map<int, std::vector<uint8_t>> macros;
    int macro_slot = 0;
    uint8_t custom = 0;
    int frames = 0;
    std::mutex lock;

    static void seal(std::vector<uint8_t> &b)
    {
        uint16_t sum = rockcat_checksum(b.data(), b.size() - 2);
        b[b.size() - 2] = sum & 0xff;
        b[b.size() - 1] = sum >> 8;
    }

    explicit ryos_mk_sim(const model &mod) : m(mod)
    {
        for (uint8_t p = 0; p < PROFILES; ++p) {
            for (int i = 0; i < m.count; ++i) {
                const part &pt = m.parts[i];
                if (!pt.size)
                    continue;
                std::vector<uint8_t> &b = data[p][i];
                b.assign(pt.size, 0);
                b[0] = pt.id;
                b[1] = pt.size & 0xff;
                if (pt.profile_at == 3)
                    b[2] = static_cast<uint8_t>(pt.size >> 8);
                b[pt.profile_at] = p;
            }
            std::memcpy(data[p][P_PRIMARY].data() + 3, FACTORY_PRIMARY, sizeof FACTORY_PRIMARY);
            std::memcpy(data[p][P_FUNCTION].data() + 3, FACTORY_FUNCTION, sizeof FACTORY_FUNCTION);
            if (m.parts[P_MACROKEYS].size)
                std::memcpy(data[p][P_MACROKEYS].data() + 3, FACTORY_MACROKEYS, sizeof FACTORY_MACROKEYS);
            std::memcpy(data[p][P_THUMB].data() + 3, FACTORY_THUMB, sizeof FACTORY_THUMB);
            std::memcpy(data[p][P_EXTRA].data() + 3, FACTORY_EXTRA, sizeof FACTORY_EXTRA);
            std::memcpy(data[p][P_EASYZONE].data() + 4, FACTORY_EASYZONE, sizeof FACTORY_EASYZONE);
            data[p][P_KEYMASK][3] = 0x3f;
            // full brightness, starlit sky after 15 minutes, manual set on odd profiles so both sets get some use
            std::vector<uint8_t> &l = data[p][P_LIGHT];
            l[3] = 5;
            l[5] = 15;
            l[10] = p & 1;
            l[12] = 1;
            l[13] = 0x5a; // nobody knows this one, it has to survive a round trip
            for (int i = P_STORED; i < m.count; ++i) {
                std::vector<uint8_t> &s = data[p][i];
                const uint8_t mode = (m.parts[i].request & 0xf0) == 0xd0 ? 1 : 0;
                if (m.light == LIGHT_SETS) {
                    s[4] = mode;
                    // every layer: effect, fade seconds, then all keys lit except one row per profile, blink on Esc
                    for (int layer = 0; layer < 11; ++layer) {
                        uint8_t *at = s.data() + SET_LAYER + layer * 125;
                        at[0] = mode ? 0 : 1;
                        at[1] = 3;
                        for (int k = 0; k < 123; ++k)
                            at[2 + k] = (k / 24 == p) ? 0 : 1;
                        at[2 + 1] |= 2;
                    }
                } else if (m.light == LIGHT_LAYERS) {
                    s[3] = mode;
                    s[4] = m.parts[i].request & 0x0f;
                    s[TKL_EFFECT] = mode ? 0 : 1;
                    s[6] = 5;
                    s[7] = 5;
                    for (int k = 0; k < LEDS; ++k)
                        s[TKL_KEYS + 2 * k] = (k / 24 == p) ? 0 : 0x80;
                    s[TKL_KEYS + 2 * 1 + 1] = 0x04; // a blink speed on Esc that nobody may touch
                    for (int t = 0; t < 3; ++t)
                        s[TKL_KEYS + 2 * LEDS + t] = 0x80;
                }
            }
            for (int i = 0; i < m.count; ++i)
                if (m.parts[i].size)
                    seal(data[p][i]);
        }
    }

    // report 17 is several parts on these, the select says which one
    int part_of(uint8_t id, uint8_t request) const
    {
        for (int i = 0; i < m.count; ++i)
            if (m.parts[i].size && m.parts[i].id == id && (id != 0x17 || m.parts[i].request == request))
                return i;
        return -1;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        int p = part_of(buf[0], select_b);
        if (p >= 0) {
            if (select_b != m.parts[p].request || select_a >= PROFILES)
                return ROCKCAT_ERR_IO;
            out = data[select_a][p];
        } else {
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
            case 0x05:
                out = {0x05, 0x03, static_cast<uint8_t>(0x50 | active)};
                break;
            case 0x0e: {
                int half = (select_a & 0x20) ? 1 : 0;
                auto it = macros.find((select_a & 0x0f) * 256 + select_b);
                out.assign(0x402, 0xff);
                out[0] = 0x0e;
                out[1] = static_cast<uint8_t>(half + 1);
                if (it != macros.end())
                    std::memcpy(out.data() + 2, it->second.data() + half * 0x400, 0x400);
                break;
            }
            case 0x0f:
                out = {0x0f, 0x08, 0x7a, 0x00, 0x00, 0x01, 0x01, 0x00};
                break;
            case 0x13:
                out = {0x13, 0x08, custom, 0, 0, 0, 0, 0};
                break;
            default:
                return ROCKCAT_ERR_IO;
            }
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        int p = -1;
        // which report 17 this is comes from its own bytes when it gets written: mode (and layer on the TKL)
        if (buf[0] == 0x17 && len > 4) {
            uint8_t mode = m.light == LIGHT_LAYERS ? buf[3] : buf[4];
            uint8_t layer = m.light == LIGHT_LAYERS ? buf[4] : 0;
            p = part_of(0x17, static_cast<uint8_t>((mode ? 0xd0 : 0xc0) | layer));
        } else if (buf[0] != 0x17) {
            p = part_of(buf[0], 0);
        }
        if (p >= 0) {
            const part &pt = m.parts[p];
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            uint8_t profile = len > pt.profile_at ? buf[pt.profile_at] : 0xff;
            if (len != pt.size || profile >= PROFILES || stored != rockcat_checksum(buf, len - 2)) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            data[profile][p].assign(buf, buf + len);
            status_queue = {3};
            return ROCKCAT_OK;
        }
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            break;
        case 0x05:
            active = buf[2] & 0x0f;
            break;
        case 0x0e:
            if (len != 0x402)
                return ROCKCAT_ERR_IO;
            if (buf[1] == 1)
                macro_slot = buf[2] * 256 + buf[3];
            macros[macro_slot].resize(0x800);
            std::memcpy(macros[macro_slot].data() + (buf[1] == 2 ? 0x400 : 0), buf + 2, 0x400);
            break;
        case 0x13:
            custom = buf[2];
            break;
        case 0x18: {
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            if (m.light == LIGHT_RGB || m.light == LIGHT_NONE || m.light == LIGHT_GLOW || len != 20 || stored != rockcat_checksum(buf, len - 2))
                return ROCKCAT_ERR_IO;
            ++frames;
            status_queue = {3};
            break;
        }
        default:
            return ROCKCAT_ERR_IO;
        }
        return ROCKCAT_OK;
    }

    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

ryos_mk_sim &shared_mk_sim(uint16_t pid)
{
    static std::map<uint16_t, ryos_mk_sim> sims;
    static std::mutex guard;
    std::lock_guard<std::mutex> g(guard);
    auto it = sims.find(pid);
    if (it == sims.end())
        it = sims.emplace(std::piecewise_construct, std::forward_as_tuple(pid), std::forward_as_tuple(model_of(pid))).first;
    return it->second;
}

template <uint16_t PID> struct ryos_mk_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        ryos_mk_sim &s = shared_mk_sim(PID);
        std::lock_guard<std::mutex> g(s.lock);
        return s.get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        ryos_mk_sim &s = shared_mk_sim(PID);
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

template <uint16_t PID> std::unique_ptr<transport> make_mk_door()
{
    return std::unique_ptr<transport>(new ryos_mk_door<PID>);
}

const sim_registration sim_mk({"ryos-mk", PID_MK, "ROCCAT Ryos MK", 1, make_mk_door<PID_MK>});
const sim_registration sim_mk_glow({"ryos-mk-glow", PID_MK_GLOW, "ROCCAT Ryos MK Glow", 1, make_mk_door<PID_MK_GLOW>});
const sim_registration sim_mk_pro({"ryos-mk-pro", PID_MK_PRO, "ROCCAT Ryos MK Pro", 1, make_mk_door<PID_MK_PRO>});
const sim_registration sim_tkl({"ryos-tkl", PID_TKL, "ROCCAT Ryos TKL", 1, make_mk_door<PID_TKL>});
const sim_registration sim_tkl_pro({"ryos-tkl-pro", PID_TKL_PRO, "ROCCAT Ryos TKL Pro", 1, make_mk_door<PID_TKL_PRO>});
