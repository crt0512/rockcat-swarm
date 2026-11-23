// a pretend Kova 2016 (gray). its profiles are what a real one answered (tests/captures/kova2016-gray-readonly.cap),
// the rest follows docs/protocol/kova.md. ROCKCAT_SIM=kova-2016 puts it in place of real hardware for cli and gui.
// it forgets everything when the program exits
#pragma once

#include "transport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace kova_sim_tables {
#include "sim_kova_tables.inc"
}

struct kova_sim final : transport {
    uint8_t active = 0;
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    std::vector<uint8_t> settings[5], buttons[5];
    std::map<int, std::vector<uint8_t>> macros; // profile * 256 + slot -> both halves
    int macro_slot = 0;
    uint8_t talk[16] = {}; // the last colour the pc sent, all zero = the mouse draws by itself
    int talks = 0;
    std::mutex lock;

    kova_sim()
    {
        for (int p = 0; p < 5; ++p) {
            settings[p].assign(kova_sim_tables::KOVA_SETTINGS[p], kova_sim_tables::KOVA_SETTINGS[p] + 28);
            buttons[p].assign(kova_sim_tables::KOVA_BUTTONS[p], kova_sim_tables::KOVA_BUTTONS[p] + 75);
        }
    }

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
        case 0x05:
            out = {0x05, 0x04, active, 0x05};
            break;
        case 0x06:
            if (select_b != 0x80 || select_a > 4)
                return ROCKCAT_ERR_IO;
            out = settings[select_a];
            break;
        case 0x07:
            if (select_b != 0x90 || select_a > 4)
                return ROCKCAT_ERR_IO;
            out = buttons[select_a];
            break;
        case 0x08: {
            int half = (select_a & 0x20) ? 1 : 0;
            auto it = macros.find((select_a & 0x0f) * 256 + select_b);
            out.assign(0x402, 0xff);
            out[0] = 0x08;
            out[1] = static_cast<uint8_t>(half + 1);
            if (it != macros.end())
                std::memcpy(out.data() + 2, it->second.data() + half * 0x400, 0x400);
            break;
        }
        case 0x09:
            out = {0x09, 0x08, 0x76, 0x69, 0x07, 0x01, 0x21, 0x14};
            break;
        default:
            return ROCKCAT_ERR_IO;
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            break;
        case 0x05:
            if (len != 4 || buf[2] > 4) {
                status_queue = {2};
                break;
            }
            active = buf[2];
            break;
        case 0x06: {
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            if (len != 28 || buf[2] > 4 || stored != rockcat_checksum(buf, len - 2)) {
                status_queue = {2};
                break;
            }
            settings[buf[2]].assign(buf, buf + len);
            status_queue = {3};
            break;
        }
        case 0x07:
            if (len != 75 || buf[2] > 4) {
                status_queue = {2};
                break;
            }
            buttons[buf[2]].assign(buf, buf + len);
            status_queue = {3};
            break;
        case 0x08:
            if (len != 0x402)
                return ROCKCAT_ERR_IO;
            if (buf[1] == 1)
                macro_slot = buf[2] * 256 + buf[3];
            macros[macro_slot].resize(0x800);
            std::memcpy(macros[macro_slot].data() + (buf[1] == 2 ? 0x400 : 0), buf + 2, 0x400);
            break;
        case 0x10:
            if (len != 16)
                return ROCKCAT_ERR_IO;
            std::memcpy(talk, buf, 16);
            ++talks;
            break;
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

// one per process, like the pretend keyboard: what the gui writes is what the engine reads
kova_sim &kova_sim_shared();
