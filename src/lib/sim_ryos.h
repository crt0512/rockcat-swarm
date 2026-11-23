// a pretend Ryos MK FX. unlike the pretend Kone this one is modelled on a keyboard that sat on the desk: sizes, selects and
// the factory key layout are what the real one answered (tests/captures/ryos-mk-fx-readonly.cap, docs/protocol/ryos.md)
// ROCKCAT_SIM=ryos-mk-fx puts it in place of real hardware for cli and gui. it forgets everything when the program exits
#pragma once

#include "transport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace ryos_sim_tables {
#include "families/ryos_tables.inc"
}

struct ryos_sim final : transport {
    struct part {
        uint8_t id, request;
        uint16_t size;
        uint8_t profile_at;
    };
    static constexpr int PARTS = 9;
    static constexpr part PART[PARTS] = {{0x06, 0xa0, 125, 2}, {0x07, 0xa1, 95, 2}, {0x08, 0xa2, 35, 2}, {0x09, 0xa3, 23, 2}, {0x0a, 0xa4, 8, 2},
                                         {0x0b, 0xa5, 294, 3}, {0x0c, 0xb0, 6, 2},  {0x0d, 0xb1, 32, 2}, {0x17, 0xb3, 239, 2}};

    uint8_t active = 0;
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue; // what the next status polls say before it goes back to "ready"
    std::vector<uint8_t> data[5][PARTS];
    std::map<int, std::vector<uint8_t>> macros; // profile * 256 + slot -> the two halves glued together
    int macro_slot = 0;
    uint8_t custom = 0; // 1 while the pc owns the leds
    int frames = 0;
    int sets[256] = {}; // how often each report id got written

    static void seal(std::vector<uint8_t> &b)
    {
        uint16_t sum = rockcat_checksum(b.data(), b.size() - 2);
        b[b.size() - 2] = sum & 0xff;
        b[b.size() - 1] = sum >> 8;
    }

    ryos_sim()
    {
        using namespace ryos_sim_tables;
        for (uint8_t p = 0; p < 5; ++p) {
            for (int i = 0; i < PARTS; ++i) {
                std::vector<uint8_t> &b = data[p][i];
                b.assign(PART[i].size, 0);
                b[0] = PART[i].id;
                b[1] = PART[i].size & 0xff;
                if (PART[i].profile_at == 3)
                    b[2] = static_cast<uint8_t>(PART[i].size >> 8);
                b[PART[i].profile_at] = p;
            }
            std::memcpy(data[p][0].data() + 3, FACTORY_PRIMARY, sizeof FACTORY_PRIMARY);
            std::memcpy(data[p][1].data() + 3, FACTORY_FUNCTION, sizeof FACTORY_FUNCTION);
            std::memcpy(data[p][2].data() + 3, FACTORY_MACROKEYS, sizeof FACTORY_MACROKEYS);
            std::memcpy(data[p][3].data() + 3, FACTORY_THUMB, sizeof FACTORY_THUMB);
            std::memcpy(data[p][4].data() + 3, FACTORY_EXTRA, sizeof FACTORY_EXTRA);
            std::memcpy(data[p][5].data() + 4, FACTORY_EASYZONE, sizeof FACTORY_EASYZONE);
            data[p][6][3] = 0x3f;
            // lights: full brightness, starlit sky after 15 minutes, fully lit in one colour that differs per profile
            const uint8_t rgb[3] = {static_cast<uint8_t>(p == 0 ? 255 : 0), static_cast<uint8_t>(p == 1 ? 255 : 40 * p), static_cast<uint8_t>(p >= 2 ? 255 : 0)};
            std::vector<uint8_t> &l = data[p][7];
            l[3] = 5;
            l[5] = 15;
            l[7] = 1;
            l[9] = 2;
            l[12] = 1;
            // a byte the library has no name for, it has to come back out untouched
            l[13] = 0x1e;
            std::vector<uint8_t> &s = data[p][8];
            std::fill(s.begin() + 3, s.begin() + 3 + 15, 0xff);
            std::fill(s.begin() + 3 + 15, s.begin() + 3 + 195, 0x11);
            for (int ch = 0; ch < 3; ++ch) {
                uint16_t hw = COLOUR_TO_HARDWARE[rgb[ch]];
                l[14 + 2 * ch] = hw & 0xff;
                l[15 + 2 * ch] = static_cast<uint8_t>(hw >> 8);
                s[3 + 195 + 7 * ch] = hw & 0xff;
                s[3 + 216 + 4 * ch] = static_cast<uint8_t>(hw >> 8);
            }
            for (int i = 0; i < PARTS; ++i)
                seal(data[p][i]);
        }
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
            // the keyboard only hands out what was selected right before
            if (select_b != PART[p].request || select_a > 4)
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
                // a slot nobody wrote to is erased flash
                out.assign(0x402, 0xff);
                out[0] = 0x0e;
                out[1] = static_cast<uint8_t>(half + 1);
                if (it != macros.end())
                    std::memcpy(out.data() + 2, it->second.data() + half * 0x400, 0x400);
                break;
            }
            case 0x0f:
                out = {0x0f, 0x08, 0x83, 0x6d, 0x00, 0x01, 0x01, 0x00};
                break;
            case 0x13:
                // byte 6 = "took your last frame"
                out = {0x13, 0x08, custom, 0, 0, 0, 1, 0};
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
        ++sets[buf[0]];
        int p = part_of(buf[0]);
        if (p >= 0) {
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            uint8_t profile = len > PART[p].profile_at ? buf[PART[p].profile_at] : 0xff;
            if (len != PART[p].size || profile > 4 || stored != rockcat_checksum(buf, len - 2)) {
                status_queue = {2};
                return ROCKCAT_OK;
            }
            data[profile][p].assign(buf, buf + len);
            // storing takes a moment on the real thing, make the library wait for it once
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
            // the first half carries profile and slot, the second just belongs to whatever came before
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
            if (len != 232 || stored != rockcat_checksum(buf, len - 2))
                return ROCKCAT_ERR_IO;
            ++frames;
            break;
        }
        default:
            return ROCKCAT_ERR_IO;
        }
        return ROCKCAT_OK;
    }

    // input reports a test wants the "keyboard" to send, oldest first
    std::vector<std::vector<uint8_t>> inputs;
    // only matters for the shared one below, which a gui and an engine talk to from two threads
    std::mutex lock;

    int read(uint8_t *buf, size_t len, int timeout_ms) override
    {
        if (!inputs.empty()) {
            size_t n = std::min(len, inputs.front().size());
            std::memcpy(buf, inputs.front().data(), n);
            inputs.erase(inputs.begin());
            return static_cast<int>(n);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

// the one pretend keyboard everybody in this process gets when they ask for ROCKCAT_SIM=ryos-mk-fx. one and not one each,
// so that what the gui writes is what the lighting engine reads, the way it is with a real keyboard on the desk
ryos_sim &ryos_sim_shared();

