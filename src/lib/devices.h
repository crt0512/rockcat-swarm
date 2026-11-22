// the device table and what a protocol family has to bring along
// adding a device that speaks a known family is one line in devices.cpp, a new family is one file in families/
#pragma once

#include "rockcat/rockcat.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct device_desc;

// the thing behind the opaque rockcat_profile handle. each family keeps its raw reports in a subclass
// whatever a family doesnt override answers ROCKCAT_ERR_UNSUPPORTED, so a keyboard never has to say "no dpi here" itself
struct rockcat_profile {
    const device_desc *desc;
    int index;

    rockcat_profile(const device_desc *d, int i) : desc(d), index(i) {}
    virtual ~rockcat_profile() = default;

    virtual int read(rockcat_device *dev) = 0;
    // sends only what changed and then forgets that it changed
    virtual int write(rockcat_device *dev) = 0;
    virtual int set_index(int i) = 0;
    virtual void export_payload(std::vector<uint8_t> &out) const = 0;
    virtual int import_payload(const uint8_t *buf, size_t len) = 0;

    virtual int get_dpi(int, int *, int *, int *) const { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int set_dpi(int, int, int, int) { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int get_int(rockcat_setting, int *) const { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int set_int(rockcat_setting, int) { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int get_led(int, rockcat_color *, int *) const { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int set_led(int, rockcat_color, int) { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int get_input(int, int, rockcat_assignment *) const { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int set_input(int, int, const rockcat_assignment *) { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int get_macro(rockcat_device *, int, int, rockcat_macro *) const { return ROCKCAT_ERR_UNSUPPORTED; }
    virtual int set_macro(int, int, const rockcat_macro *) { return ROCKCAT_ERR_UNSUPPORTED; }
};

struct family_ops {
    rockcat_profile *(*new_profile)(const device_desc *desc, int index);
    int (*direct_begin)(rockcat_device *dev);
    int (*direct_frame)(rockcat_device *dev, const rockcat_color *colors, int count);
    int (*direct_end)(rockcat_device *dev);
    const char *(*input_label)(int input);
    const char *(*led_label)(int led);
    const char *(*layer_label)(int layer);
};

struct device_desc {
    uint16_t pid;
    const char *name;
    const family_ops *family;
    rockcat_caps caps;
};

const device_desc *device_find(uint16_t vid, uint16_t pid);
// nullptr once i is past the end
const device_desc *device_at(int i);

extern const family_ops KONE_AIMO_FAMILY;
