// the way every Swarm era ROCCAT device wants to be talked to, mice and keyboards alike:
// feature reports, a "select" on report 04 before reading per profile data, and polling 04 after a write until it stops saying busy
#pragma once

#include "rockcat/rockcat.h"

#include <cstddef>
#include <cstdint>

constexpr uint8_t REPORT_CONTROL = 0x04;

// buf[0] is the report id. complains with ROCKCAT_ERR_PROTOCOL when something else comes back
int get_raw(rockcat_device *dev, uint8_t *buf, size_t len);
// wait = ask the device afterwards whether it took it (1 ready, 2 didnt like the data, 3 busy)
int set_raw(rockcat_device *dev, const uint8_t *buf, size_t len, bool wait);
int wait_ready(rockcat_device *dev);
int wait_ready_after(rockcat_device *dev, int first_delay_ms);
// [04 a b] + status wait. what a and b mean is up to the family, usually profile and "which report next"
int select_data(rockcat_device *dev, uint8_t a, uint8_t b);
// last two bytes = sum of everything before them, little endian
void put_checksum(uint8_t *buf, size_t len);
bool checksum_ok(const uint8_t *buf, size_t len);
uint16_t get16(const uint8_t *p);
void put16(uint8_t *p, uint16_t v);

// a macro the way the Kova 2016 and the Ryos MK FX keep it: too big for one report, so it travels as two halves of 1024 bytes
// in the same report id. reading wants [04 profile|10 slot] and [04 profile|20 slot] in front, writing just sends [id 01 ...] and [id 02 ...]
// a slot nobody ever wrote to comes back as an empty macro and not as an error
int macro_halves_read(rockcat_device *dev, uint8_t report, int profile, int slot, rockcat_macro *m);
int macro_halves_write(rockcat_device *dev, uint8_t report, const rockcat_macro &m);
