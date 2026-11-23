// see reports.h. the status wait mirrors ROCCAT_Swarm_Monitor.exe: every SetFeature is followed by polling report 04
// until the device says "ready" (1) or "invalid" (2) instead of "busy" (3)

#include "internal.h"

#include <algorithm>
#include <cstring>
#include <vector>

int get_raw(rockcat_device *dev, uint8_t *buf, size_t len)
{
    if (!dev || !dev->io || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    uint8_t id = buf[0];
    int r = dev->io->get_feature(buf, len);
    if (r < 0)
        return r;
    if (buf[0] != id)
        return ROCKCAT_ERR_PROTOCOL;
    return ROCKCAT_OK;
}

int wait_ready(rockcat_device *dev)
{
    return wait_ready_after(dev, dev->wait.first_delay_ms);
}

int wait_ready_after(rockcat_device *dev, int first_delay_ms)
{
    dev->io->sleep_ms(first_delay_ms);
    for (int i = 0; i < dev->wait.max_polls; ++i) {
        uint8_t status[3] = {REPORT_CONTROL, 0, 0};
        int r = dev->io->get_feature(status, sizeof status);
        if (r < 0)
            return r;
        switch (status[1]) {
        case 3: // busy
            dev->io->sleep_ms(dev->wait.poll_delay_ms);
            continue;
        case 2:
            return ROCKCAT_ERR_REJECTED;
        default:
            return ROCKCAT_OK;
        }
    }
    return ROCKCAT_ERR_BUSY;
}

int set_raw(rockcat_device *dev, const uint8_t *buf, size_t len, bool wait)
{
    if (!dev || !dev->io || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    int r = dev->io->set_feature(buf, len);
    if (r < 0)
        return r;
    return wait ? wait_ready(dev) : ROCKCAT_OK;
}

int select_data(rockcat_device *dev, uint8_t a, uint8_t b)
{
    const uint8_t sel[3] = {REPORT_CONTROL, a, b};
    int r = set_raw(dev, sel, sizeof sel, false);
    return r == ROCKCAT_OK ? wait_ready_after(dev, dev->wait.select_delay_ms) : r;
}

void put_checksum(uint8_t *buf, size_t len)
{
    uint16_t sum = rockcat_checksum(buf, len - 2);
    buf[len - 2] = sum & 0xff;
    buf[len - 1] = sum >> 8;
}

bool checksum_ok(const uint8_t *buf, size_t len)
{
    uint16_t stored = buf[len - 2] | (buf[len - 1] << 8);
    return stored == rockcat_checksum(buf, len - 2);
}

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }

namespace {

constexpr size_t HALF = 0x400, HALF_REPORT = HALF + 2;
// the macro inside the two halves. the Kone AIMO has the same thing behind a 3 byte header and with a checksum, here there is neither
enum : size_t { M_PROFILE = 0, M_BUTTON = 1, M_LOOP = 2, M_SET_NAME = 3, M_NAME = 43, M_COUNT = 75, M_KEYS = 77, M_SIZE = 77 + 4 * ROCKCAT_MACRO_KEYSTROKES };
static_assert(M_SIZE <= 2 * HALF, "a macro has to fit into its two halves");

} // namespace

namespace {

// the macro without whatever report it travels in: profile, slot, loop, the two names, count, keystrokes
void macro_decode(const uint8_t *b, int profile, int slot, rockcat_macro *m)
{
    std::memset(m, 0, sizeof *m);
    // a slot nobody ever wrote to is erased flash, all ff. thats "no macro" and not 480 presses of key ff
    if (b[M_PROFILE] == 0xff && b[M_BUTTON] == 0xff) {
        m->profile = static_cast<uint8_t>(profile);
        m->button = static_cast<uint8_t>(slot);
        return;
    }
    m->profile = b[M_PROFILE];
    m->button = b[M_BUTTON];
    m->loop = b[M_LOOP];
    std::memcpy(m->set_name, b + M_SET_NAME, 40);
    std::memcpy(m->name, b + M_NAME, 32);
    m->count = std::min<uint16_t>(get16(b + M_COUNT), ROCKCAT_MACRO_KEYSTROKES);
    for (int k = 0; k < m->count; ++k) {
        const uint8_t *e = b + M_KEYS + 4 * k;
        m->keys[k] = rockcat_keystroke{e[0], e[1], get16(e + 2)};
    }
}

void macro_encode(const rockcat_macro &m, uint8_t *b)
{
    b[M_PROFILE] = m.profile;
    b[M_BUTTON] = m.button;
    b[M_LOOP] = m.loop;
    std::memcpy(b + M_SET_NAME, m.set_name, strnlen(m.set_name, 40));
    std::memcpy(b + M_NAME, m.name, strnlen(m.name, 32));
    put16(b + M_COUNT, m.count);
    for (int k = 0; k < m.count; ++k) {
        uint8_t *e = b + M_KEYS + 4 * k;
        e[0] = m.keys[k].key;
        e[1] = m.keys[k].action;
        put16(e + 2, m.keys[k].delay_ms);
    }
}

} // namespace

int macro_halves_read(rockcat_device *dev, uint8_t report, int profile, int slot, rockcat_macro *m)
{
    std::vector<uint8_t> blob(2 * HALF, 0);
    const uint8_t selects[2] = {0x10, 0x20};
    for (int half = 0; half < 2; ++half) {
        int r = select_data(dev, static_cast<uint8_t>(profile | selects[half]), static_cast<uint8_t>(slot));
        if (r != ROCKCAT_OK)
            return r;
        std::vector<uint8_t> rep(HALF_REPORT, 0);
        rep[0] = report;
        r = get_raw(dev, rep.data(), rep.size());
        if (r != ROCKCAT_OK)
            return r;
        std::memcpy(blob.data() + half * HALF, rep.data() + 2, HALF);
    }
    macro_decode(blob.data(), profile, slot, m);
    return ROCKCAT_OK;
}

int macro_halves_write(rockcat_device *dev, uint8_t report, const rockcat_macro &m)
{
    std::vector<uint8_t> blob(2 * HALF, 0);
    uint8_t *b = blob.data();
    macro_encode(m, b);
    for (int half = 0; half < 2; ++half) {
        std::vector<uint8_t> rep(HALF_REPORT, 0);
        rep[0] = report;
        rep[1] = static_cast<uint8_t>(half + 1);
        std::memcpy(rep.data() + 2, b + half * HALF, HALF);
        int r = set_raw(dev, rep.data(), rep.size(), true);
        if (r != ROCKCAT_OK)
            return r;
    }
    return ROCKCAT_OK;
}

int macro_single_read(rockcat_device *dev, uint8_t report, int profile, int slot, rockcat_macro *m)
{
    int r = select_data(dev, static_cast<uint8_t>(profile), static_cast<uint8_t>(slot));
    if (r != ROCKCAT_OK)
        return r;
    std::vector<uint8_t> rep(MACRO_SINGLE_SIZE, 0);
    rep[0] = report;
    r = get_raw(dev, rep.data(), rep.size());
    if (r != ROCKCAT_OK)
        return r;
    // erased flash has no checksum to speak of, a written macro has one
    if (!(rep[3] == 0xff && rep[4] == 0xff) && !checksum_ok(rep.data(), rep.size()))
        return ROCKCAT_ERR_CHECKSUM;
    macro_decode(rep.data() + 3, profile, slot, m);
    return ROCKCAT_OK;
}

int macro_single_write(rockcat_device *dev, uint8_t report, const rockcat_macro &m)
{
    std::vector<uint8_t> rep(MACRO_SINGLE_SIZE, 0);
    rep[0] = report;
    put16(rep.data() + 1, static_cast<uint16_t>(rep.size()));
    macro_encode(m, rep.data() + 3);
    put_checksum(rep.data(), rep.size());
    return set_raw(dev, rep.data(), rep.size(), true);
}
