// simulated devices for when the real one isnt on the desk (or, in the case of the Kone AIMO Remastered, in the building)

#include "sim_kone.h"
#include "sim_kova.h"
#include "sim_ryos.h"

#include <cstdio>

ryos_sim &ryos_sim_shared()
{
    static ryos_sim sim;
    return sim;
}

kova_sim &kova_sim_shared()
{
    static kova_sim sim;
    return sim;
}

namespace {

// a handle on the shared pretend Kova, same idea as the one for the keyboard below
struct kova_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(kova_sim_shared().lock);
        return kova_sim_shared().get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(kova_sim_shared().lock);
        return kova_sim_shared().set_feature(buf, len);
    }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

// a handle on the shared pretend keyboard. takes its lock for every transfer and never while waiting
struct ryos_door final : transport {
    int get_feature(uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(ryos_sim_shared().lock);
        return ryos_sim_shared().get_feature(buf, len);
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        std::lock_guard<std::mutex> g(ryos_sim_shared().lock);
        return ryos_sim_shared().set_feature(buf, len);
    }
    int read(uint8_t *buf, size_t len, int timeout_ms) override
    {
        {
            ryos_sim &sim = ryos_sim_shared();
            std::lock_guard<std::mutex> g(sim.lock);
            if (!sim.inputs.empty()) {
                size_t n = std::min(len, sim.inputs.front().size());
                std::memcpy(buf, sim.inputs.front().data(), n);
                sim.inputs.erase(sim.inputs.begin());
                return static_cast<int>(n);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

} // namespace

std::vector<sim_entry> &sim_table()
{
    static std::vector<sim_entry> table;
    return table;
}

const std::vector<sim_entry> &sim_entries()
{
    return sim_table();
}

sim_registration::sim_registration(const sim_entry &e)
{
    sim_table().push_back(e);
}

namespace {

// the three that came first, before every family brought its own
const sim_registration kone_reg({"kone-aimo-remastered", ROCKCAT_PID_KONE_AIMO_REMASTERED, "ROCCAT Kone AIMO Remastered", 0,
                                 [] { return std::unique_ptr<transport>(new kone_sim); }});
const sim_registration ryos_reg({"ryos-mk-fx", ROCKCAT_PID_RYOS_MK_FX, "ROCCAT Ryos MK FX", 1, [] { return std::unique_ptr<transport>(new ryos_door); }});
const sim_registration kova_reg({"kova-2016", ROCKCAT_PID_KOVA_2016_GRAY, "ROCCAT Kova 2016 (gray)", 0,
                                 [] { return std::unique_ptr<transport>(new kova_door); }});

std::mutex plug_lock;
std::vector<std::string> &plugged()
{
    static std::vector<std::string> names;
    return names;
}

const sim_entry *find_sim(const std::string &name)
{
    for (const sim_entry &e : sim_entries())
        if (name == e.name)
            return &e;
    return nullptr;
}

} // namespace

std::vector<std::string> sims_plugged()
{
    std::lock_guard<std::mutex> g(plug_lock);
    return plugged();
}

std::unique_ptr<transport> transport_sim(const std::string &name, rockcat_device_info *info)
{
    const sim_entry *e = find_sim(name);
    if (!e)
        return nullptr;
    if (info) {
        std::memset(info, 0, sizeof *info);
        std::snprintf(info->path, sizeof info->path, "sim:%s", e->name);
        info->vendor_id = ROCKCAT_VID;
        info->product_id = e->pid;
        info->release = 0x0100;
        info->interface_number = e->interface_number;
        std::snprintf(info->name, sizeof info->name, "%s (simulated)", e->label);
    }
    return e->make();
}

extern "C" const char *rockcat_sim_name(int i)
{
    return i >= 0 && i < static_cast<int>(sim_entries().size()) ? sim_entries()[static_cast<size_t>(i)].name : nullptr;
}

extern "C" const char *rockcat_sim_label(int i)
{
    return i >= 0 && i < static_cast<int>(sim_entries().size()) ? sim_entries()[static_cast<size_t>(i)].label : nullptr;
}

extern "C" int rockcat_sim_plug(const char *name, int plug)
{
    if (!name || !find_sim(name))
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> g(plug_lock);
    auto &names = plugged();
    auto at = std::find(names.begin(), names.end(), std::string(name));
    if (plug && at == names.end())
        names.push_back(name);
    if (!plug && at != names.end())
        names.erase(at);
    return ROCKCAT_OK;
}

extern "C" int rockcat_sim_plugged(const char *name)
{
    if (!name)
        return 0;
    std::lock_guard<std::mutex> g(plug_lock);
    auto &names = plugged();
    return std::find(names.begin(), names.end(), std::string(name)) != names.end();
}
