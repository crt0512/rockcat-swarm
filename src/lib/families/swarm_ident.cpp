// Swarm era devices we can name but not talk to yet: the product id is out of Swarms module list (MODULES.md), nothing else is known
// well enough to send it to a device. they show up with their name and bcdDevice and thats it, every profile call says unsupported.
// docs/protocol/swarm-ident.md says per device what the modules hinted at, so whoever picks one up next has a head start

#include "../internal.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

struct nothing_profile final : rockcat_profile {
    using rockcat_profile::rockcat_profile;
    rockcat_profile *clone() const override { return new nothing_profile(*this); }
    int read(rockcat_device *) override { return ROCKCAT_ERR_UNSUPPORTED; }
    int write(rockcat_device *) override { return ROCKCAT_ERR_UNSUPPORTED; }
    int set_index(int) override { return ROCKCAT_ERR_UNSUPPORTED; }
    void export_payload(std::vector<uint8_t> &) const override {}
    int import_payload(const uint8_t *, size_t) override { return ROCKCAT_ERR_UNSUPPORTED; }
};

rockcat_profile *new_profile(const device_desc *desc, int index) { return new nothing_profile(desc, index); }

// bcdDevice is all we can say without sending anything
int firmware(rockcat_device *dev, char *buf, size_t len)
{
    std::snprintf(buf, len, "%x.%02x", dev->info.release >> 8, dev->info.release & 0xff);
    return ROCKCAT_OK;
}

int get_active(rockcat_device *, uint8_t *, uint8_t *) { return ROCKCAT_ERR_UNSUPPORTED; }
int set_active(rockcat_device *, uint8_t) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_begin(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_frame(rockcat_device *, const rockcat_color *, int) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_end(rockcat_device *) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_switch(rockcat_device *, bool) { return ROCKCAT_ERR_UNSUPPORTED; }
int direct_active(rockcat_device *) { return 0; }
bool decode_event(const uint8_t *, int, rockcat_event *) { return false; }
const char *label(int) { return "?"; }

const family_ops IDENT_FAMILY = {firmware,      get_active,    set_active,   new_profile, direct_begin, direct_frame, direct_end,
                                 direct_switch, direct_active, decode_event, label,       label,        label};

#define IDENT(kind) {kind, "swarm-ident", ROCKCAT_TESTED_NO, "", 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}, 0, 0, 0, 0, -1}

// report 04 is the control report of every Swarm era device that talks the old way, the node that has it is the right one.
// the ones that go through USBCmdLib.dll may not have it, then the first interface gets used (rockcat.cpp falls back to it)
const device_desc DEVICES[] = {
    {0x305c, "ROCCAT Vulcan 80", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x2fee, "ROCCAT Vulcan TKL AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x30f7, "ROCCAT Vulcan Pro", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x311a, "ROCCAT Vulcan TKL Pro", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x3167, "ROCCAT Vulcan TKL Pro", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x2f4e, "ROCCAT Vulcan II", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x2ee2, "ROCCAT Vulcan II Max", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x2f09, "ROCCAT Vulcan II Mini", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x3aa9, "ROCCAT Vulcan II Mini Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x6978, "ROCCAT Vulcan II Mini Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x300c, "ROCCAT Horde", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x303e, "ROCCAT Horde AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x31e2, "ROCCAT Isku+", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x3214, "ROCCAT Isku+ Force FX", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x3246, "ROCCAT Suora FX", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x3124, "ROCCAT Magma", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x69a0, "ROCCAT Magma Mini", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x314c, "ROCCAT Pyro", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_KEYBOARD)},
    {0x2cf1, "ROCCAT Kova AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2cf2, "ROCCAT Kova AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2cf3, "ROCCAT Kova AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2e24, "ROCCAT Kone EMP", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2dcd, "ROCCAT Kone Pure Owl-Eye", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2dca, "ROCCAT Kone Pure SE", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2dcb, "ROCCAT Kone Pure SEL", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2dd2, "ROCCAT Kone Pure Ultra", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2dd7, "ROCCAT Kone Pure Ultra iCafe", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2e4c, "ROCCAT Leadr", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2e4e, "ROCCAT Leadr", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2c88, "ROCCAT Kone Pro", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2c8e, "ROCCAT Kone Pro Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2c92, "ROCCAT Kone Pro Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2c8b, "ROCCAT Kone XP", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2cb2, "ROCCAT Kone XP Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2cb6, "ROCCAT Kone XP Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2de6, "ROCCAT Burst Core", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2de1, "ROCCAT Burst Pro", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2ca6, "ROCCAT Burst Pro Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2cab, "ROCCAT Burst Pro Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2d00, "ROCCAT Kain 100 AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2d5c, "ROCCAT Kain 120 AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2d5f, "ROCCAT Kain 200 AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2d60, "ROCCAT Kain 200 AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0xafc9, "ROCCAT Kone Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0xafca, "ROCCAT Kone Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2ccc, "ROCCAT Pure Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x3c6e, "ROCCAT Pure Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x2cc8, "ROCCAT Pure SEL", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_MOUSE)},
    {0x343a, "ROCCAT Sense AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_PAD)},
    {0x343b, "ROCCAT Sense AIMO XXL", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_PAD)},
    {0x2cb8, "ROCCAT Kone XP Air dock", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_PAD)},
    {0x39d0, "ROCCAT Khan AIMO", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a34, "ROCCAT Elo 7.1 USB", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a37, "ROCCAT Elo 7.1 Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a39, "ROCCAT Elo 7.1 Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a3b, "ROCCAT Syn Pro Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a3d, "ROCCAT Syn Pro Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a3f, "ROCCAT Syn Max Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    {0x3a41, "ROCCAT Syn Max Air", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
    // a usb microphone, the closest kind we have
    {0x3a56, "ROCCAT Torch", &IDENT_FAMILY, nullptr, 0x04, IDENT(ROCKCAT_KIND_HEADSET)},
};

const device_registration reg(DEVICES, static_cast<int>(sizeof DEVICES / sizeof DEVICES[0]));

// a pretend one, so the gui can be seen dealing with a device it can only name
struct mute_sim final : transport {
    int get_feature(uint8_t *, size_t) override { return ROCKCAT_ERR_IO; }
    int set_feature(const uint8_t *, size_t) override { return ROCKCAT_ERR_IO; }
    int read(uint8_t *, size_t, int timeout_ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    void sleep_ms(int) override {}
};

const sim_registration sim({"vulcan-pro", 0x30f7, "ROCCAT Vulcan Pro", 0, [] { return std::unique_ptr<transport>(new mute_sim); }});

} // namespace
