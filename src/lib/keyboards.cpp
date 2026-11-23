// the generated key tables and the calls that hand them out

#include "internal.h"
#include "keyboards.h"

#include "keyboards/ryos_mk_fx.inc"

const keyboard_layout RYOS_MK_FX_KEYBOARD = {RYOS_MK_FX_REGIONS, static_cast<int>(sizeof RYOS_MK_FX_REGIONS / sizeof RYOS_MK_FX_REGIONS[0]), 693, 368};

extern "C" {

int rockcat_keyboard_regions(const rockcat_caps *caps, int *ids, int max)
{
    const device_desc *d = device_of(caps);
    if (!d || !d->keyboard)
        return 0;
    for (int i = 0; i < d->keyboard->region_count && i < max && ids; ++i)
        ids[i] = d->keyboard->regions[i].id;
    return d->keyboard->region_count;
}

const char *rockcat_keyboard_region_name(const rockcat_caps *caps, int region)
{
    const device_desc *d = device_of(caps);
    for (int i = 0; d && d->keyboard && i < d->keyboard->region_count; ++i)
        if (d->keyboard->regions[i].id == region)
            return d->keyboard->regions[i].name;
    return "?";
}

int rockcat_keyboard_size(const rockcat_caps *caps, int *width, int *height)
{
    const device_desc *d = device_of(caps);
    if (!d || !d->keyboard)
        return ROCKCAT_ERR_UNSUPPORTED;
    if (width)
        *width = d->keyboard->picture_width;
    if (height)
        *height = d->keyboard->picture_height;
    return ROCKCAT_OK;
}

int rockcat_keyboard_keys(const rockcat_caps *caps, int region, const rockcat_key **keys)
{
    const device_desc *d = device_of(caps);
    for (int i = 0; d && d->keyboard && i < d->keyboard->region_count; ++i)
        if (d->keyboard->regions[i].id == region) {
            if (keys)
                *keys = d->keyboard->regions[i].keys;
            return d->keyboard->regions[i].count;
        }
    return 0;
}

} // extern "C"
