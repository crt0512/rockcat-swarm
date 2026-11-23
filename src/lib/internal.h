// Private definitions shared by the librockcat translation units.
#pragma once

#include "rockcat/rockcat.h"
#include "devices.h"
#include "families/kone_aimo.h"
#include "reports.h"
#include "transport.h"

#include <mutex>
#include <string>

// how long to leave the device alone after a write before asking if its done, and how often to ask
// these are the numbers Swarm uses for the Kone AIMO, other families bring their own
struct wait_params {
    int first_delay_ms = 150;
    // the same wait after a select (which only says what the next read should return and stores nothing)
    int select_delay_ms = 150;
    int poll_delay_ms = 50;
    int max_polls = 100;
};

struct rockcat_device {
    std::unique_ptr<transport> io;
    rockcat_device_info info{};
    // nullptr when the device was opened raw and isnt in the table
    const device_desc *desc = nullptr;
    wait_params wait{};
};

// two handles on the same device (the gui and a lighting engine in the same program) must not talk into each others
// sentences: a select from one and the read that belongs to it have to stay together. everything public that is more
// than one transfer takes this lock, one per device path
std::mutex &device_mutex(rockcat_device *dev);

// config.cpp: host side settings per device and profile
std::string config_get(uint16_t pid, uint8_t profile, const std::string &key);
bool config_set(uint16_t pid, uint8_t profile, const std::string &key, const std::string &value);
long long config_stamp(uint16_t pid);

// builds a device around any transport, the tests use this to plug in a simulated mouse
rockcat_device *device_from_transport(std::unique_ptr<transport> io, const rockcat_device_info &info);
