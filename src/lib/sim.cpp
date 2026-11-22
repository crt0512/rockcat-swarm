// simulated devices for when the real one isnt on the desk (or, in the case of the Kone AIMO Remastered, in the building)

#include "sim_kone.h"

#include <cstdio>

std::unique_ptr<transport> transport_sim(const std::string &name, rockcat_device_info *info)
{
    if (name != "kone-aimo-remastered")
        return nullptr;
    if (info) {
        std::memset(info, 0, sizeof *info);
        std::snprintf(info->path, sizeof info->path, "sim:%s", name.c_str());
        info->vendor_id = ROCKCAT_VID;
        info->product_id = ROCKCAT_PID_KONE_AIMO_REMASTERED;
        info->release = 0x0105;
        std::snprintf(info->name, sizeof info->name, "ROCCAT Kone AIMO Remastered (simulated)");
    }
    return std::unique_ptr<transport>(new kone_sim);
}
