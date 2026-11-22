// Private definitions shared by the librockcat translation units.
#pragma once

#include "rockcat/rockcat.h"

#include <hidapi/hidapi.h>

struct rockcat_device {
    hid_device *hid = nullptr;
    rockcat_device_info info{};
};
