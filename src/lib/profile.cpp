// the C side of the profile api. checks the arguments once here so the families dont each have to, then hands over

#include "internal.h"
#include "keyboards.h"

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

// on a keyboard an input and a led are both simply a key, and the key table knows what its called. the US table gets asked
// because thats the one whose names match what the key sends (Z is Z there), the picture in the gui has its own legends anyway
static const char *key_name(const device_desc *d, int number)
{
    if (!d || !d->keyboard || d->keyboard->region_count == 0)
        return nullptr;
    // US first, then the others: an ISO board has a key next to the left shift that the US table has never heard of
    for (int pass = 0; pass < 2; ++pass)
        for (int r = 0; r < d->keyboard->region_count; ++r) {
            const keyboard_region &region = d->keyboard->regions[r];
            if ((region.id == 0) != (pass == 0))
                continue;
            for (int i = 0; i < region.count; ++i)
                if (region.keys[i].led == number)
                    return region.keys[i].name;
        }
    return nullptr;
}

const char *rockcat_input_label(const rockcat_caps *caps, int input)
{
    const device_desc *d = device_of(caps);
    if (const char *key = key_name(d, input))
        return key;
    return d ? d->family->input_label(input) : "?";
}

const char *rockcat_led_label(const rockcat_caps *caps, int led)
{
    const device_desc *d = device_of(caps);
    if (const char *key = key_name(d, led))
        return key;
    return d ? d->family->led_label(led) : "?";
}

const char *rockcat_layer_label(const rockcat_caps *caps, int layer)
{
    const device_desc *d = device_of(caps);
    return d ? d->family->layer_label(layer) : "?";
}

const char *rockcat_assignment_name(const rockcat_caps *caps, uint8_t type)
{
    const device_desc *d = device_of(caps);
    if (d && d->family->type_label)
        return d->family->type_label(type);
    const char *name = rockcat_button_type_name(type);
    return name ? name : "?";
}

int rockcat_assignment_is_function(const rockcat_caps *caps, uint8_t type)
{
    const device_desc *d = device_of(caps);
    return d && d->family->type_is_function ? d->family->type_is_function(type) : 1;
}

int rockcat_get_firmware_version(rockcat_device *dev, char *buf, size_t len)
{
    if (!dev || !buf || len == 0)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return dev->desc ? dev->desc->family->firmware(dev, buf, len) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_get_active_profile(rockcat_device *dev, uint8_t *profile, uint8_t *count)
{
    if (!dev)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return dev->desc ? dev->desc->family->get_active(dev, profile, count) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_set_active_profile(rockcat_device *dev, uint8_t profile)
{
    if (!dev)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return dev->desc ? dev->desc->family->set_active(dev, profile) : ROCKCAT_ERR_UNSUPPORTED;
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
    std::lock_guard<std::mutex> talking(device_mutex(dev));
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
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return p->write(dev);
}

void rockcat_profile_free(rockcat_profile *p)
{
    delete p;
}

rockcat_profile *rockcat_profile_clone(const rockcat_profile *p)
{
    return p ? p->clone() : nullptr;
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

int rockcat_profile::input_room(int layer, int input) const
{
    rockcat_assignment a;
    int r = get_input(layer, input, &a);
    if (r == ROCKCAT_ERR_UNSUPPORTED)
        return 0;
    if (r != ROCKCAT_OK)
        return r;
    return ROCKCAT_ROOM_KEY | ROCKCAT_ROOM_PARAMS | ((desc->caps.features & ROCKCAT_HAS_MACROS) ? ROCKCAT_ROOM_MACRO : 0);
}

int rockcat_profile_input_room(const rockcat_profile *p, int layer, int input)
{
    return p ? p->input_room(layer, input) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_factory_input(const rockcat_profile *p, int layer, int input, rockcat_assignment *a)
{
    return p && a ? p->factory_input(layer, input, a) : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_reset_inputs(rockcat_profile *p)
{
    return p ? p->reset_inputs() : ROCKCAT_ERR_ARGUMENT;
}

int rockcat_profile_get_macro(rockcat_device *dev, const rockcat_profile *p, int layer, int input, rockcat_macro *m)
{
    if (!dev || !p || dev->desc != p->desc)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return p->get_macro(dev, layer, input, m);
}

int rockcat_profile_set_macro(rockcat_profile *p, int layer, int input, const rockcat_macro *m)
{
    return p ? p->set_macro(layer, input, m) : ROCKCAT_ERR_ARGUMENT;
}

// which effect a profile has is two things on some devices: what the device has stored, and (when that is the "carrier",
// the value that means "the pc draws something") which effect the pc was told to draw, kept in the config file
int rockcat_profile_get_effect(const rockcat_profile *p, int *effect, int *by_engine)
{
    int e = 0;
    int r = p ? p->get_int(ROCKCAT_SET_EFFECT, &e) : ROCKCAT_ERR_ARGUMENT;
    if (r != ROCKCAT_OK)
        return r;
    const rockcat_caps &caps = p->desc->caps;
    uint32_t software = caps.software_effects;
    int engine = e >= 0 && e < 32 && ((software >> e) & 1);
    if (caps.carrier_effect >= 0 && e == caps.carrier_effect) {
        uint8_t picked = static_cast<uint8_t>(e);
        rockcat_engine_effect_load(p->desc->pid, static_cast<uint8_t>(p->index), &picked);
        // nothing remembered (the default that comes back is the Kone's) or something this device cant do: then its just the carrier
        if (picked < 32 && (caps.effects & (1u << picked)))
            e = picked;
        // on a Kone AIMO the carrier always means "the pc draws". elsewhere the carrier can be a plain effect of the
        // device too (fully lit on a Kova), then it depends on what was remembered
        engine = caps.carrier_effect == ROCKCAT_EFFECT_AIMO || ((software >> e) & 1);
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
    const rockcat_caps &caps = p->desc->caps;
    uint32_t software = caps.software_effects;
    if ((software >> effect) & 1)
        by_engine = 1;
    // on a Kone AIMO "by the engine" always goes through the carrier, also for effects the mouse could draw itself.
    // everywhere else an effect goes under its own number if the device has one, and under the carrier if not
    bool kone_way = caps.carrier_effect == ROCKCAT_EFFECT_AIMO;
    int r = (by_engine && kone_way) ? ROCKCAT_ERR_UNSUPPORTED : p->set_int(ROCKCAT_SET_EFFECT, effect);
    if (r == ROCKCAT_ERR_UNSUPPORTED && by_engine && caps.carrier_effect >= 0)
        r = p->set_int(ROCKCAT_SET_EFFECT, caps.carrier_effect);
    if (r != ROCKCAT_OK)
        return r;
    // remembered on the pc whenever the carrier is what the device now has (also when the carrier itself got picked, as
    // the plain effect it is on some devices), so that a stale "draw fire" doesnt wake up later
    int stored = effect;
    p->get_int(ROCKCAT_SET_EFFECT, &stored);
    if (caps.carrier_effect >= 0 && (by_engine || stored == caps.carrier_effect))
        return rockcat_engine_effect_save(p->desc->pid, static_cast<uint8_t>(p->index), static_cast<uint8_t>(effect));
    return ROCKCAT_OK;
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

int rockcat_poll_event(rockcat_device *dev, rockcat_event *ev, int timeout_ms)
{
    if (!dev || !dev->io || !ev)
        return ROCKCAT_ERR_ARGUMENT;
    *ev = rockcat_event{ROCKCAT_EVENT_NONE, 0, 0};
    uint8_t in[64];
    int n = dev->io->read(in, sizeof in, timeout_ms);
    if (n <= 0)
        return n;
    if (dev->desc)
        dev->desc->family->decode_event(in, n, ev);
    return 1;
}

int rockcat_direct_begin(rockcat_device *dev)
{
    if (!dev)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return dev->desc ? dev->desc->family->direct_begin(dev) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_direct_frame(rockcat_device *dev, const rockcat_color *colors, int count)
{
    if (!dev || !colors)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return dev->desc ? dev->desc->family->direct_frame(dev, colors, count) : ROCKCAT_ERR_UNSUPPORTED;
}

int rockcat_direct_end(rockcat_device *dev)
{
    if (!dev)
        return ROCKCAT_ERR_ARGUMENT;
    std::lock_guard<std::mutex> talking(device_mutex(dev));
    return dev->desc ? dev->desc->family->direct_end(dev) : ROCKCAT_ERR_UNSUPPORTED;
}

} // extern "C"
