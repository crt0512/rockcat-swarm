// Private definitions shared by the librockcat translation units.
#pragma once

#include "rockcat/rockcat.h"
#include "devices.h"
#include "transport.h"

// how long to leave the device alone after a write before asking if its done, and how often to ask
// these are the numbers Swarm uses for the Kone AIMO, other families bring their own
struct wait_params {
    int first_delay_ms = 150;
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

// builds a device around any transport, the tests use this to plug in a simulated mouse
rockcat_device *device_from_transport(std::unique_ptr<transport> io, const rockcat_device_info &info);
