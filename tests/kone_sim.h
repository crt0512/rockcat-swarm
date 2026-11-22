// a pretend Kone AIMO Remastered, good enough to answer what the library asks
// built from docs/PROTOCOL.md and NOT from a real mouse (nobody here owns one), so it proves the library
// keeps saying the same things, not that the mouse agrees with them
#pragma once

#include "transport.h"

#include <cstring>
#include <map>
#include <vector>

struct kone_sim final : transport {
    uint8_t active = 0;
    uint8_t select_a = 0, select_b = 0;
    std::vector<int> status_queue; // what the next status polls say before it goes back to "ready"
    uint8_t settings[5][ROCKCAT_SETTINGS_SIZE];
    uint8_t buttons[5][ROCKCAT_BUTTONS_SIZE];
    std::map<int, std::vector<uint8_t>> macros; // profile * 256 + button -> the two 1024 byte chunks glued together
    int macro_slot = 0;
    uint8_t led_mode[6] = {0x0e, 6, 1, 0, 0, 0xff};
    int sets[256] = {}; // how often each report id got written, so tests can check that nothing extra was sent

    kone_sim()
    {
        for (uint8_t p = 0; p < 5; ++p) {
            rockcat_settings s;
            std::memset(&s, 0, sizeof s);
            s.profile = p;
            s.dpi_enabled_mask = 0x1f;
            s.dpi_active = 2;
            for (int i = 0; i < ROCKCAT_DPI_LEVELS; ++i)
                s.dpi_x[i] = s.dpi_y[i] = static_cast<uint16_t>(400 * (i + 1) + 100 * p);
            s.polling_rate = ROCKCAT_POLLING_1000HZ;
            s.debounce_ms = 8;
            s.led_enable_mask = 0x7ff;
            s.effect = ROCKCAT_EFFECT_FULLY_LIT;
            s.effect_speed = 6;
            s.brightness = 255;
            for (int i = 0; i < ROCKCAT_LED_COUNT; ++i)
                s.leds[i] = rockcat_color{255, static_cast<uint8_t>(20 * i), static_cast<uint8_t>(255 - 20 * i), static_cast<uint8_t>(40 * p)};
            s.color_theme = 0x80;
            // a few bytes the library has no name for, they have to come back out untouched
            s.raw[0x1c] = 0x5a;
            s.raw[0x78] = static_cast<uint8_t>(0xa0 + p);
            rockcat_settings_encode(&s, settings[p]);

            std::memset(buttons[p], 0, sizeof buttons[p]);
            buttons[p][0] = 0x07;
            buttons[p][1] = ROCKCAT_BUTTONS_SIZE;
            buttons[p][2] = p;
            for (int i = 0; i < ROCKCAT_BUTTON_COUNT; ++i)
                buttons[p][3 + 3 * i] = static_cast<uint8_t>(i + 1);
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
            out = {0x05, 4, active, 5};
            break;
        case 0x06:
            if (select_b != 0x80 || select_a > 4)
                return ROCKCAT_ERR_IO;
            out.assign(settings[select_a], settings[select_a] + ROCKCAT_SETTINGS_SIZE);
            break;
        case 0x07:
            if (select_b != 0x90 || select_a > 4)
                return ROCKCAT_ERR_IO;
            out.assign(buttons[select_a], buttons[select_a] + ROCKCAT_BUTTONS_SIZE);
            break;
        case 0x08: {
            // select was [profile | 0x10 or 0x20, button], the high nibble picks the chunk
            int chunk = (select_a & 0x20) ? 1 : 0;
            auto it = macros.find((select_a & 0x0f) * 256 + select_b);
            out.assign(0x402, 0);
            out[0] = 0x08;
            out[1] = static_cast<uint8_t>(chunk + 1);
            if (it != macros.end())
                std::memcpy(out.data() + 2, it->second.data() + chunk * 0x400, 0x400);
            break;
        }
        case 0x09:
            out = {0x09, 9, 105, 0, 0, 0, 0, 0, 0};
            break;
        case 0x0e:
            out.assign(led_mode, led_mode + sizeof led_mode);
            break;
        case 0x0f:
            out = {0x0f, 6, 2, 1, 0x34, 0x12};
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
        ++sets[buf[0]];
        switch (buf[0]) {
        case 0x04:
            select_a = buf[1];
            select_b = buf[2];
            break;
        case 0x05:
            active = buf[2];
            break;
        case 0x06: {
            uint16_t stored = static_cast<uint16_t>(buf[len - 2] | (buf[len - 1] << 8));
            if (len != ROCKCAT_SETTINGS_SIZE || buf[2] > 4 || stored != rockcat_checksum(buf, len - 2)) {
                status_queue = {2};
                break;
            }
            std::memcpy(settings[buf[2]], buf, len);
            // a real mouse takes its time storing a profile, make the library wait for it once
            status_queue = {3};
            break;
        }
        case 0x07:
            if (len == ROCKCAT_BUTTONS_SIZE && buf[2] <= 4)
                std::memcpy(buttons[buf[2]], buf, len);
            break;
        case 0x08:
            // chunk 1 carries profile and button in its header, chunk 2 just belongs to whatever came before
            if (buf[1] == 1)
                macro_slot = buf[2 + 3] * 256 + buf[2 + 4];
            macros[macro_slot].resize(0x800);
            std::memcpy(macros[macro_slot].data() + (buf[1] == 2 ? 0x400 : 0), buf + 2, 0x400);
            break;
        case 0x0e:
            std::memcpy(led_mode, buf, std::min(len, sizeof led_mode));
            break;
        case 0x0d:
        case 0x0f:
            break;
        default:
            return ROCKCAT_ERR_IO;
        }
        return ROCKCAT_OK;
    }

    int read(uint8_t *, size_t, int) override { return 0; }
    void sleep_ms(int) override {}
};
