// a pretend Vulcan 120 AIMO, TKL AIMO or Vulcan 80, built from the firmware (VULCAN_AIMO_v1.36.bin, VULCAN_80_V1.19.bin) and Swarms default
// profiles, never compared with a real one. ROCKCAT_SIM=vulcan-120-aimo / vulcan-tkl-aimo / vulcan-80 puts it in place of real hardware. like the real
// thing it keeps one set of reports and not five, and it refuses what we think the firmware refuses: wrong size or checksum, a light
// report with speed outside 1..11 or (AIMO) brightness outside 1..69, (80) brightness level above 9
#pragma once

#include "transport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

struct vulcan_sim final : transport {
    struct part {
        uint8_t id, request;
        uint16_t size;
    };
    static constexpr int PARTS = 7;
    static constexpr part PART[PARTS] = {{0x06, 0xa0, 133}, {0x07, 0xa1, 95}, {0x09, 0xa3, 43}, {0x0a, 0xa4, 8},
                                         {0x0b, 0xa5, 65},  {0x0d, 0xb1, 443}, {0x08, 0xa2, 35}};

    std::vector<uint8_t> data[PARTS];
    uint16_t sz[PARTS]; // per board: 0 = the board doesnt have it (09 on the Horde, 08 everywhere else)
    uint8_t profile_report[4] = {0x05, 0x04, 0x00, 0x04};
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue;
    int sets[256] = {};
    std::mutex lock;

    static void seal(std::vector<uint8_t> &b)
    {
        uint16_t sum = rockcat_checksum(b.data(), b.size() - 2);
        b[b.size() - 2] = sum & 0xff;
        b[b.size() - 1] = sum >> 8;
    }

    uint16_t size(int i) const { return sz[i]; }
    uint16_t light_size() const { return sz[5]; }

    // light: 443 on the AIMO boards, 156 on the Vulcan 80, 58 on the Horde. fkeys: 95, 143 on the TKL. keys: 133, 149 on the Horde.
    // r08 only for the Horde (its M keys), which has no 09
    vulcan_sim(const uint8_t *r06, const uint8_t *r07, const uint8_t *r0a, const uint8_t *r0b, const uint8_t *r0d, uint16_t light = 443,
               uint16_t fkeys = 95, uint16_t keys = 133, const uint8_t *r08 = nullptr)
    {
        const uint8_t *src[PARTS] = {r06, r07, nullptr, r0a, r0b, r0d, r08};
        const uint16_t sizes[PARTS] = {keys, fkeys, static_cast<uint16_t>(r08 ? 0 : 43), 8, 65, light, static_cast<uint16_t>(r08 ? 35 : 0)};
        std::copy(sizes, sizes + PARTS, sz);
        for (int i = 0; i < PARTS; ++i) {
            if (!sz[i])
                continue;
            if (src[i]) {
                data[i].assign(src[i], src[i] + size(i));
            } else {
                // 09 out of the box is twelve empty entries as far as Swarm is concerned
                data[i].assign(PART[i].size, 0);
                data[i][0] = PART[i].id;
                data[i][1] = static_cast<uint8_t>(PART[i].size);
            }
            seal(data[i]);
        }
    }

    int index_of(uint8_t id) const
    {
        for (int i = 0; i < PARTS; ++i)
            if (PART[i].id == id && sz[i])
                return i;
        return -1;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        std::vector<uint8_t> out;
        if (buf[0] == 0x04) {
            int st = 1;
            if (!status_queue.empty()) {
                st = status_queue.front();
                status_queue.erase(status_queue.begin());
            }
            out = {0x04, static_cast<uint8_t>(st), 0};
        } else if (buf[0] == 0x05) {
            out.assign(profile_report, profile_report + 4);
        } else {
            int i = index_of(buf[0]);
            if (i < 0 || PART[i].request != select_b)
                return ROCKCAT_ERR_IO;
            out = data[i];
        }
        size_t n = std::min(len, out.size());
        std::memcpy(buf, out.data(), n);
        return static_cast<int>(n);
    }

    int set_feature(const uint8_t *buf, size_t len) override
    {
        ++sets[buf[0]];
        if (buf[0] == 0x04) {
            select_a = buf[1];
            select_b = buf[2];
            return ROCKCAT_OK;
        }
        if (buf[0] == 0x05) {
            if (len != 4)
                return ROCKCAT_ERR_IO;
            std::memcpy(profile_report, buf, 4);
            return ROCKCAT_OK;
        }
        int i = index_of(buf[0]);
        if (i < 0 || len != size(i))
            return ROCKCAT_ERR_IO;
        std::vector<uint8_t> in(buf, buf + len);
        uint16_t stored = static_cast<uint16_t>(in[len - 2] | (in[len - 1] << 8));
        bool ok = stored == rockcat_checksum(in.data(), len - 2);
        if (buf[0] == 0x0d && light_size() != 58 && (in[5] == 0 || in[5] >= 0x0c))
            ok = false;
        if (buf[0] == 0x0d && light_size() == 443 && (in[7] == 0 || in[7] >= 0x46))
            ok = false;
        if (buf[0] == 0x0d && light_size() == 156 && in[11] > 9)
            ok = false;
        if (!ok) {
            status_queue = {2};
            return ROCKCAT_OK;
        }
        data[i] = in;
        status_queue = {3};
        return ROCKCAT_OK;
    }

    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};
