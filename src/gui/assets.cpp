// device pictures out of the built in assets. the ROCCAT set is webp (tools/assets/to-webp.py), the free set is still png,
// so everything asks for a name without caring and gets whichever of the two the build has.
// webp goes through libwebp ourselves: the gdk-pixbuf webp loader is an extra package plenty of systems dont have

#include "app.h"

#include <webp/decode.h>

namespace gui {

namespace {

GdkPixbuf *from_webp(const std::string &resource)
{
    GBytes *bytes = g_resources_lookup_data(resource.c_str(), G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (!bytes)
        return nullptr;
    gsize len = 0;
    const auto *data = static_cast<const uint8_t *>(g_bytes_get_data(bytes, &len));
    int w = 0, h = 0;
    uint8_t *rgba = WebPDecodeRGBA(data, len, &w, &h);
    g_bytes_unref(bytes);
    if (!rgba)
        return nullptr;
    // the pixbuf keeps libwebps buffer and hands it back when its done, no copy
    return gdk_pixbuf_new_from_data(rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4,
                                    [](guchar *px, gpointer) { WebPFree(px); }, nullptr);
}

} // namespace

GdkPixbuf *asset_pixbuf(const std::string &resource, int width, int height)
{
    // the name may come with .png or .webp on it (view.ini says top.png, it was written back when there was only png)
    std::string base = resource;
    for (const char *ext : {".png", ".webp"})
        if (base.size() > std::strlen(ext) && base.compare(base.size() - std::strlen(ext), std::string::npos, ext) == 0)
            base.resize(base.size() - std::strlen(ext));

    GdkPixbuf *pix = from_webp(base + ".webp");
    if (!pix)
        return width > 0 ? gdk_pixbuf_new_from_resource_at_scale((base + ".png").c_str(), width, height, TRUE, nullptr)
                         : gdk_pixbuf_new_from_resource((base + ".png").c_str(), nullptr);
    if (width <= 0)
        return pix;
    // same as the png loader at_scale with aspect kept: fits into width x height, never stretched
    const int w = gdk_pixbuf_get_width(pix), h = gdk_pixbuf_get_height(pix);
    const double k = std::min(static_cast<double>(width) / w, static_cast<double>(height) / h);
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pix, std::max(1, static_cast<int>(std::lround(w * k))),
                                                std::max(1, static_cast<int>(std::lround(h * k))), GDK_INTERP_HYPER);
    g_object_unref(pix);
    return scaled;
}

cairo_surface_t *asset_surface(const std::string &resource)
{
    GdkPixbuf *pix = asset_pixbuf(resource);
    if (!pix)
        return nullptr;
    cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(pix, 1, nullptr);
    g_object_unref(pix);
    return surface;
}

} // namespace gui
