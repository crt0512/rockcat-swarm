// the one door to the hardware. everything above this thinks in reports and has no idea hidapi exists,
// which is what lets a recording stand in for a device nobody here owns
#pragma once

#include "rockcat/rockcat.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct transport {
    virtual ~transport() = default;
    // buf[0] is the report id going in. returns the number of bytes the device answered with, or a ROCKCAT_ERR_*
    virtual int get_feature(uint8_t *buf, size_t len) = 0;
    virtual int set_feature(const uint8_t *buf, size_t len) = 0;
    // interrupt reports. 0 = nothing came within timeout_ms
    virtual int read(uint8_t *buf, size_t len, int timeout_ms) = 0;
    // goes through here so a replay doesnt sit around waiting for a device that isnt there
    virtual void sleep_ms(int ms);
    // only a replay has an opinion: ROCKCAT_OK while everything sent matched the recording
    virtual int verdict(bool) { return ROCKCAT_OK; }
};

// one hidraw node as the kernel lists it, supported device or not
struct hid_node {
    std::string path, product, serial;
    uint16_t vid = 0, pid = 0, release = 0;
    int interface_number = 0;
};

int hid_backend_init();
void hid_backend_exit();
std::vector<hid_node> hid_nodes(uint16_t vid);
// report ids the node declares in its descriptor, empty if it cant be opened or read
std::vector<uint8_t> hid_report_ids(const std::string &path);
std::unique_ptr<transport> transport_open_hid(const std::string &path);

// sim.cpp. what ROCKCAT_SIM can be set to, nullptr for a name nobody knows
std::unique_ptr<transport> transport_sim(const std::string &name, rockcat_device_info *info);

// a pretend device. a family file adds its own at file scope:
//     static const sim_registration sim({"kone-pure", ROCKCAT_PID_..., "ROCCAT Kone Pure", 0, [] { return std::unique_ptr<transport>(new my_sim); }});
// make gets called for every handle that opens it. a device that should keep what was written to it between handles
// (the gui writes, the engine reads) hands out doors to one shared instance, see sim.cpp
struct sim_entry {
    const char *name;  // what ROCKCAT_SIM and the debug menu call it
    uint16_t pid;
    const char *label; // shown as the model name, "(simulated)" gets added
    int interface_number;
    std::unique_ptr<transport> (*make)();
};
struct sim_registration {
    explicit sim_registration(const sim_entry &e);
};
const std::vector<sim_entry> &sim_entries();
// the pretend devices plugged in right now (rockcat_sim_plug), besides the real ones
std::vector<std::string> sims_plugged();

// capture.cpp
std::unique_ptr<transport> transport_record(std::unique_ptr<transport> inner, const std::string &file, const rockcat_device_info &info);
// info gets filled from the header of the recording. nullptr if the file is missing or makes no sense
std::unique_ptr<transport> transport_replay(const std::string &file, rockcat_device_info *info);
