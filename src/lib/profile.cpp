// the C side of the profile api. checks the arguments once here so the families dont each have to, then hands over

#include "internal.h"

#include <cstring>

namespace {

// backup blob: "RCPF", format version, product id (little endian), profile index, then whatever the family stores
const uint8_t BLOB_MAGIC[5] = {'R', 'C', 'P', 'F', 1};
const size_t BLOB_HEADER = sizeof BLOB_MAGIC + 3;

} // namespace

extern "C" {

const rockcat_caps *rockcat_get_caps(rockcat_device *dev)
{
    return dev && dev->desc ? &dev->desc->caps : nullptr;
}

int rockcat_model_at(int i, uint16_t *product_id, const char **name, const rockcat_caps **caps)
{
    const device_desc *d = device_at(i);
    if (!d)
        return ROCKCAT_ERR_ARGUMENT;
    if (product_id)
        *product_id = d->pid;
    if (name)
        *name = d->name;
    if (caps)
        *caps = &d->caps;
    return ROCKCAT_OK;
}

const char *rockcat_input_label(rockcat_device *dev, int input)
{
    return dev && dev->desc ? dev->desc->family->input_label(input) : "?";
}

const char *rockcat_led_label(rockcat_device *dev, int led)
{
    return dev && dev->desc ? dev->desc->family->led_label(led) : "?";
}

const char *rockcat_layer_label(rockcat_device *dev, int layer)
{
    return dev && dev->desc ? dev->desc->family->layer_label(layer) : "?";
}

int rockcat_profile_read(rockcat_device *dev, int index, rockcat_profile **out)
{
    if (!dev || !out)
        return ROCKCAT_ERR_ARGUMENT;
    *out = nullptr;
    if (!dev->desc)
        return ROCKCAT_ERR_UNSUPPORTED;
    if (index < 0 || index >= dev->desc->caps.profile_count)
        return ROCKCAT_ERR_ARGUMENT;
    rockcat_profile *p = dev->desc->family->new_profile(dev->desc, index);
    int r = p->read(dev);
    if (r != ROCKCAT_OK) {
        delete p;
        return r;
    }
    *out = p;
    return ROCKCAT_OK;
}

int rockcat_profile_write(rockcat_device *dev, rockcat_profile *p)
{
    // a profile read from one model must not end up on another, the reports dont even have the same size
    if (!dev || !p || dev->desc != p->desc)
        return ROCKCAT_ERR_ARGUMENT;
    return p->write(dev);
}

void rockcat_profile_free(rockcat_profile *p)
{
    delete p;
}

int rockcat_profile_index(const rockcat_profile *p)
{
    return p ? p->index : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_set_index(rockcat_profile *p, int index)
{
    return p ? p->set_index(index) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_dpi(const rockcat_profile *p, int level, int *x, int *y, int *enabled)
{
    return p ? p->get_dpi(level, x, y, enabled) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_set_dpi(rockcat_profile *p, int level, int x, int y, int enabled)
{
    return p ? p->set_dpi(level, x, y, enabled) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_int(const rockcat_profile *p, rockcat_setting which, int *value)
{
    return p ? p->get_int(which, value) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_set_int(rockcat_profile *p, rockcat_setting which, int value)
{
    return p ? p->set_int(which, value) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_led(const rockcat_profile *p, int led, rockcat_color *c, int *enabled)
{
    return p ? p->get_led(led, c, enabled) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_set_led(rockcat_profile *p, int led, rockcat_color c, int enabled)
{
    return p ? p->set_led(led, c, enabled) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_input(const rockcat_profile *p, int layer, int input, rockcat_assignment *a)
{
    return p ? p->get_input(layer, input, a) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_set_input(rockcat_profile *p, int layer, int input, const rockcat_assignment *a)
{
    return p ? p->set_input(layer, input, a) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_macro(rockcat_device *dev, const rockcat_profile *p, int layer, int input, rockcat_macro *m)
{
    if (!dev || !p || dev->desc != p->desc)
        return ROCKCAT_ERR_ARGUMENT;
    return p->get_macro(dev, layer, input, m);
}

int rockcat_profile_set_macro(rockcat_profile *p, int layer, int input, const rockcat_macro *m)
{
    return p ? p->set_macro(layer, input, m) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_effect(const rockcat_profile *p, int *effect, int *by_engine)
{
    int e = 0;
    int r = p ? p->get_int(ROCKCAT_SET_EFFECT, &e) : ROCKCAT_ERR_ARGUMENT;
    if (r != ROCKCAT_OK)
        return r;
    int engine = rockcat_effect_is_software(static_cast<uint8_t>(e));
    // AIMO on the device means "the pc draws this", which effect exactly is in the config file
    if (e == ROCKCAT_EFFECT_AIMO) {
        uint8_t picked = ROCKCAT_EFFECT_AIMO;
        rockcat_engine_effect_load(p->desc->pid, static_cast<uint8_t>(p->index), &picked);
        e = picked;
    }
    if (effect)
        *effect = e;
    if (by_engine)
        *by_engine = engine;
    return ROCKCAT_OK;
}

int rockcat_profile_select_effect(rockcat_profile *p, int effect, int by_engine)
{
    if (!p || effect < 0 || effect > 31 || !(p->desc->caps.effects & (1u << effect)))
        return ROCKCAT_ERR_ARGUMENT;
    if (rockcat_effect_is_software(static_cast<uint8_t>(effect)))
        by_engine = 1;
    if (!by_engine)
        return p->set_int(ROCKCAT_SET_EFFECT, effect);
    int r = p->set_int(ROCKCAT_SET_EFFECT, ROCKCAT_EFFECT_AIMO);
    if (r != ROCKCAT_OK)
        return r;
    return rockcat_engine_effect_save(p->desc->pid, static_cast<uint8_t>(p->index), static_cast<uint8_t>(effect));
}

int rockcat_profile_export(const rockcat_profile *p, uint8_t *buf, size_t len)
{
    if (!p)
        return ROCKCAT_ERR_ARGUMENT;
    std::vector<uint8_t> out(BLOB_MAGIC, BLOB_MAGIC + sizeof BLOB_MAGIC);
    out.push_back(static_cast<uint8_t>(p->desc->pid & 0xff));
    out.push_back(static_cast<uint8_t>(p->desc->pid >> 8));
    out.push_back(static_cast<uint8_t>(p->index));
    p->export_payload(out);
    if (buf && len >= out.size())
        std::memcpy(buf, out.data(), out.size());
    return static_cast<int>(out.size());
}

int rockcat_profile_import(rockcat_device *dev, const uint8_t *buf, size_t len, rockcat_profile **out)
{
    if (!dev || !buf || !out)
        return ROCKCAT_ERR_ARGUMENT;
    *out = nullptr;
    if (!dev->desc)
        return ROCKCAT_ERR_UNSUPPORTED;
    if (len < BLOB_HEADER || std::memcmp(buf, BLOB_MAGIC, sizeof BLOB_MAGIC) != 0)
        return ROCKCAT_ERR_ARGUMENT;
    uint16_t pid = static_cast<uint16_t>(buf[5] | (buf[6] << 8));
    int index = buf[7];
    if (pid != dev->desc->pid || index >= dev->desc->caps.profile_count)
        return ROCKCAT_ERR_ARGUMENT;
    rockcat_profile *p = dev->desc->family->new_profile(dev->desc, index);
    int r = p->import_payload(buf + BLOB_HEADER, len - BLOB_HEADER);
    if (r != ROCKCAT_OK) {
        delete p;
        return r;
    }
    *out = p;
    return ROCKCAT_OK;
}

int rockcat_direct_begin(rockcat_device *dev)
{
    if (!dev)
        return ROCKCAT_ERR_ARGUMENT;
    return dev->desc ? dev->desc->family->direct_begin(dev) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_direct_frame(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!dev || !colors)
        return ROCKCAT_ERR_ARGUMENT;
    return dev->desc ? dev->desc->family->direct_frame(dev, colors, count) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_direct_end(rockcat_device *dev)
{
    if (!dev)
        return ROCKCAT_ERR_ARGUMENT;
    return dev->desc ? dev->desc->family->direct_end(dev) : ROCKCAT_ERR_UNSUPPORTED;
}

} // extern "C"
