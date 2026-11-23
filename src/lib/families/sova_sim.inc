// pretend Sova, membrane and MK. starts out with the roccat-tools factory reports (which match the real membrane where
// nobody changed anything), answers selects, sizes and checksums like the real one did when it was read out
// included at the end of sova.cpp. one shared keyboard per pid, so the gui and an engine see the same

struct sova_sim final : transport {
    uint8_t active = 0;
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> data[PROFILES][PARTS];
    std::map<int, std::vector<uint8_t>> macros; // profile * 256 + slot -> whole report
    std::mutex lock;

    sova_sim()
    {
        for (uint8_t p = 0; p < PROFILES; ++p)
            for (int i = 0; i < PARTS; ++i) {
                std::vector<uint8_t> &b = data[p][i];
                b.assign(FACTORY[i], FACTORY[i] + PART[i].size);
                b[PART[i].profile_at] = p;
                // something to tell the profiles apart by: brightness goes down with every profile
                if (i == P_LIGHT)
                    b[L_BRIGHTNESS] = static_cast<uint8_t>(5 - p);
                put_checksum_v(b);
            }
    }

    static void put_checksum_v(std::vector<uint8_t> &b)
    {
        uint16_t sum = rockcat_checksum(b.data(), b.size() - 2);
        b[b.size() - 2] = sum & 0xff;
        b[b.size() - 1] = sum >> 8;
    }

    static int part_of(uint8_t id)
    {
        for (int i = 0; i < PARTS; ++i)
            if (PART[i].id == id)
                return i;
        return -1;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        int p = part_of(buf[0]);
        if (p >= 0) {
            if (select_b != PART[p].request || select_a >= PROFILES)
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
                out = {0x05, 0x04, active, 0x01};
                break;
            case 0x0e: {
                auto it = macros.find(select_a * 256 + select_b);
                if (it != macros.end()) {
                    out = it->second;
                } else {
                    out.assign(MACRO_SINGLE_SIZE, 0xff);
                    out[0] = 0x0e;
                }
                break;
            }
            case 0x0f:
                out = {0x0f, 0x08, 0x7f, 0x6b, 0x00, 0x01, 0x01, 0x00};
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
        int p = part_of(buf[0]);
        if (p >= 0) {
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            uint8_t profile = len > PART[p].profile_at ? buf[PART[p].profile_at] : 0xff;
            if (len != PART[p].size || profile >= PROFILES || stored != rockcat_checksum(buf, len - 2)) {
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
            if (len != 4 || buf[2] >= PROFILES)
                return ROCKCAT_ERR_IO;
            active = buf[2];
            break;
        case 0x0e: {
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            if (len != MACRO_SINGLE_SIZE || stored != rockcat_checksum(buf, len - 2) || buf[3] >= PROFILES) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            macros[buf[3] * 256 + buf[4]].assign(buf, buf + len);
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

sova_sim &shared_sova(uint16_t pid)
{
    static std::map<uint16_t, sova_sim> sims;
    static std::mutex guard;
    std::lock_guard<std::mutex> g(guard);
    return sims[pid];
}

template <uint16_t PID> struct sova_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        sova_sim &s = shared_sova(PID);
        std::lock_guard<std::mutex> g(s.lock);
        return s.get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        sova_sim &s = shared_sova(PID);
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

template <uint16_t PID> std::unique_ptr<transport> make_sova_door()
{
    return std::unique_ptr<transport>(new sova_door<PID>);
}

const sim_registration sim_membrane({"sova-membrane", 0x2f76, "ROCCAT Sova Membrane", 1, make_sova_door<0x2f76>});
const sim_registration sim_mk({"sova-mk", 0x2f94, "ROCCAT Sova MK", 1, make_sova_door<0x2f94>});
