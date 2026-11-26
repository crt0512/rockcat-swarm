// the Settings page: dpi levels and polling rate if the device has them, application preferences, about

#include "../app.h"

#include <utility>

namespace gui {

void on_dpi_x_changed(GtkSpinButton *spin, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating || !s->dpi_lock || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->dpi_lock)))
        return;
    size_t idx = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "level")));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->dpi[idx].y), gtk_spin_button_get_value(spin));
}

void on_dpi_lock_toggled(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    bool locked = gtk_toggle_button_get_active(b);
    for (auto &row : s->dpi) {
        if (!row.y)
            continue;
        gtk_widget_set_sensitive(row.y, !locked);
        if (locked && !s->updating)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(row.y), gtk_spin_button_get_value(GTK_SPIN_BUTTON(row.x)));
    }
}

GtkWidget *dpi_spin(app_state *s)
{
    GtkWidget *sp = gtk_spin_button_new_with_range(s->caps->dpi_min, s->caps->dpi_max, s->caps->dpi_step);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(sp), TRUE);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sp), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(sp), 6);
    return sp;
}

// who is connected: its picture and what it says about itself. also keeps the settings page from looking half empty
GtkWidget *device_panel(app_state *s)
{
    const rockcat_caps *caps = s->caps;
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    char res[96];
    std::snprintf(res, sizeof res, "/org/rockcat/swarm/assets/devices/%04x", s->info.product_id);
    if (GdkPixbuf *pix = asset_pixbuf(res, 110, 110)) {
        GtkWidget *img = gtk_image_new_from_pixbuf(pix);
        gtk_widget_set_valign(img, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(row), img, FALSE, FALSE, 0);
        g_object_unref(pix);
    }
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    int line = 0;
    auto fact = [&](const char *what, const std::string &value) {
        gtk_grid_attach(GTK_GRID(grid), label(what, "rc-hint"), 0, line, 1, 1);
        GtkWidget *v = label(value.c_str());
        gtk_label_set_selectable(GTK_LABEL(v), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(v), TRUE);
        gtk_label_set_xalign(GTK_LABEL(v), 0);
        gtk_label_set_max_width_chars(GTK_LABEL(v), 34);
        gtk_label_set_width_chars(GTK_LABEL(v), 12);
        gtk_grid_attach(GTK_GRID(grid), v, 1, line++, 1, 1);
    };
    char usb[32], text[96];
    std::snprintf(usb, sizeof usb, "%04x:%04x", s->info.vendor_id, s->info.product_id);
    fact("MODEL", s->info.name);
    fact("FIRMWARE", s->firmware.empty() ? "?" : s->firmware);
    fact("USB ID", usb);
    // a device known by name only has no numbers worth showing
    const bool known = caps->profile_count > 0;
    if (known) {
        if (caps->profile_count == 1)
            std::snprintf(text, sizeof text, "1 (the device keeps one setup)");
        else
            std::snprintf(text, sizeof text, "%d, number %d is active", caps->profile_count, s->active_profile + 1);
        fact("PROFILES", text);
    }
    if (!known) {
    } else if (is_keyboard(s)) {
        int ids[16], n = rockcat_keyboard_regions(caps, ids, 16), keys = 0;
        for (int i = 0; i < n && i < 16; ++i)
            keys = std::max(keys, rockcat_keyboard_keys(caps, ids[i], nullptr));
        std::snprintf(text, sizeof text, "%d, on %d layers", keys, caps->layer_count);
        fact("KEYS", text);
        std::snprintf(text, sizeof text, "one per key%s", caps->features & ROCKCAT_HAS_RGB ? ", any colour" : "");
        fact("LIGHTS", text);
    } else {
        std::snprintf(text, sizeof text, "%d, on %d layers", caps->input_count, caps->layer_count);
        fact("BUTTONS", text);
        std::snprintf(text, sizeof text, "%d%s", caps->led_count, caps->features & ROCKCAT_HAS_RGB ? ", any colour" : "");
        fact("LIGHTS", text);
        if (caps->features & ROCKCAT_HAS_DPI) {
            std::snprintf(text, sizeof text, "%d to %d dpi", caps->dpi_min, caps->dpi_max);
            fact("SENSOR", text);
        }
    }
    std::string can;
    for (auto &f : {std::pair<uint32_t, const char *>{ROCKCAT_HAS_MACROS, "macros"}, {ROCKCAT_HAS_EASYSHIFT, "Easy-Shift[+]"},
                    {ROCKCAT_HAS_DIRECT_LIGHT, "lights from the pc"}, {ROCKCAT_HAS_CALIBRATION, "sensor calibration"}, {ROCKCAT_HAS_LIFT_OFF, "lift-off distance"}})
        if (caps->features & f.first)
            can += (can.empty() ? "" : ", ") + std::string(f.second);
    if (!can.empty())
        fact("ALSO", can);
    // how much to trust what this program does with it
    std::snprintf(text, sizeof text, caps->tested == ROCKCAT_TESTED_HARDWARE ? "tried on a real one (firmware %s)"
                                     : caps->tested == ROCKCAT_TESTED_REPLAY ? "only tried against recordings (firmware %s)"
                                     : !known                                ? "known by name only%s"
                                                                             : "never tried on a real one%s", caps->tested_firmware);
    fact("SUPPORT", text);
    fact("CONNECTED AS", s->info.path);
    gtk_box_pack_start(GTK_BOX(row), grid, TRUE, TRUE, 0);
    return box_panel("DEVICE", row);
}

GtkWidget *build_settings_page(app_state *s)
{
    GtkWidget *page = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(page), 14);
    gtk_grid_set_row_spacing(GTK_GRID(page), 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);
    // two columns of the same width that grow with the window
    gtk_grid_set_column_homogeneous(GTK_GRID(page), TRUE);

    // dpi on the left, polling rate and the device box on the right. a keyboard has neither dpi nor polling, the
    // device box gets the whole width then
    const rockcat_caps *caps = s->caps;
    bool dpi = caps && (caps->features & ROCKCAT_HAS_DPI), polling = caps && caps->polling_hz[0];
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14), *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);

    if (dpi) {
        // a mouse with one sensor value per level has no use for a second column
        bool xy = caps->features & ROCKCAT_HAS_DPI_XY;
        int levels = caps->dpi_levels;
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
        const char *heads[] = {"LEVEL", "ON", "ACTIVE", xy ? "DPI X" : "DPI", "DPI Y"};
        for (int c = 0; c < (xy ? 5 : 4); ++c)
            gtk_grid_attach(GTK_GRID(grid), label(heads[c], "rc-hint"), c, 0, 1, 1);
        GSList *group = nullptr;
        s->dpi.assign(static_cast<size_t>(levels), dpi_row{});
        for (int i = 0; i < levels; ++i) {
            char t[16];
            std::snprintf(t, sizeof t, "%d", i + 1);
            dpi_row &r = s->dpi[static_cast<size_t>(i)];
            r.enabled = gtk_check_button_new();
            r.active = gtk_radio_button_new(group);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(r.active));
            r.x = dpi_spin(s);
            g_object_set_data(G_OBJECT(r.x), "level", GINT_TO_POINTER(i));
            gtk_grid_attach(GTK_GRID(grid), label(t, "rc-value"), 0, i + 1, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), r.enabled, 1, i + 1, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), r.active, 2, i + 1, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), r.x, 3, i + 1, 1, 1);
            if (xy) {
                r.y = dpi_spin(s);
                g_signal_connect(r.x, "value-changed", G_CALLBACK(on_dpi_x_changed), s);
                gtk_grid_attach(GTK_GRID(grid), r.y, 4, i + 1, 1, 1);
            }
        }
        if (xy) {
            s->dpi_lock = gtk_check_button_new_with_label("Same DPI for X and Y");
            g_signal_connect(s->dpi_lock, "toggled", G_CALLBACK(on_dpi_lock_toggled), s);
            gtk_grid_attach(GTK_GRID(grid), s->dpi_lock, 0, levels + 1, 5, 1);
        }
        char range[128];
        std::snprintf(range, sizeof range, "%d - %d DPI in steps of %d. The DPI up/down buttons cycle through enabled levels.",
                      caps->dpi_min, caps->dpi_max, caps->dpi_step);
        GtkWidget *rh = label(range, "rc-hint");
        gtk_label_set_line_wrap(GTK_LABEL(rh), TRUE);
        gtk_label_set_xalign(GTK_LABEL(rh), 0);
        gtk_label_set_max_width_chars(GTK_LABEL(rh), 50);
        gtk_grid_attach(GTK_GRID(grid), rh, 0, levels + 2, 5, 1);
        s->dpi_panel = box_panel("DPI SWITCHER", grid);
        gtk_box_pack_start(GTK_BOX(left), s->dpi_panel, !polling, !polling, 0);
    }

    if (polling) {
        GtkWidget *pbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GSList *group = nullptr;
        for (int i = 0; i < 8 && caps->polling_hz[i]; ++i) {
            char text[16];
            std::snprintf(text, sizeof text, "%d Hz", caps->polling_hz[i]);
            GtkWidget *b = gtk_radio_button_new_with_label(group, text);
            group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b));
            g_object_set_data(G_OBJECT(b), "hz", GINT_TO_POINTER(caps->polling_hz[i]));
            s->polling.push_back(b);
            gtk_box_pack_start(GTK_BOX(pbox), b, FALSE, FALSE, 0);
        }
        s->polling_panel = box_panel("POLLING RATE", pbox);
        gtk_box_pack_start(GTK_BOX(left), s->polling_panel, TRUE, TRUE, 0);
    }

    if (caps && s->dev) {
        // a mouse: dpi and polling rate on the left, the device and its profiles on the right. a keyboard has neither
        // dpi nor polling, its device box and profile box sit next to each other
        // known by name only: no profiles to save, a word on how to help instead
        GtkWidget *dp = device_panel(s), *fp = caps->profile_count > 0 ? files_panel(s) : nullptr;
        if (!fp) {
            GtkWidget *h = label("rockcat-swarm knows this device by its name so far and doesnt send it anything. A recording of what "
                                 "the device says helps getting it supported, docs/CAPTURES.md says how to make one.", "rc-hint");
            gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
            gtk_label_set_xalign(GTK_LABEL(h), 0);
            fp = box_panel("NOT SUPPORTED YET", h);
        }
        if (dpi || polling) {
            gtk_box_pack_start(GTK_BOX(right), dp, FALSE, FALSE, 0);
            // the last box of each column takes what is left, so both columns end at the same height
            gtk_box_pack_start(GTK_BOX(right), fp, TRUE, TRUE, 0);
            gtk_grid_attach(GTK_GRID(page), left, 0, 0, 1, 1);
            gtk_grid_attach(GTK_GRID(page), right, 1, 0, 1, 1);
        } else {
            gtk_widget_set_valign(dp, GTK_ALIGN_START);
            gtk_widget_set_valign(fp, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(page), dp, 0, 0, 1, 1);
            gtk_grid_attach(GTK_GRID(page), fp, 1, 0, 1, 1);
            gtk_widget_destroy(left);
            gtk_widget_destroy(right);
        }
    } else if (s->unsupported_pid) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
        char res[96];
        std::snprintf(res, sizeof res, "/org/rockcat/swarm/assets/devices/%04x", s->unsupported_pid);
        if (GdkPixbuf *pix = asset_pixbuf(res, 110, 110)) {
            GtkWidget *img = gtk_image_new_from_pixbuf(pix);
            gtk_widget_set_valign(img, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(row), img, FALSE, FALSE, 0);
            g_object_unref(pix);
        }
        char text[512];
        std::snprintf(text, sizeof text, "%s (usb id 1e7d:%04x) is plugged in, but rockcat-swarm cant talk to it yet, so it leaves it alone. "
                      "It's on the list. A recording of what the device says helps getting it supported, docs/CAPTURES.md says how to make one.",
                      s->unsupported_name.c_str(), s->unsupported_pid);
        GtkWidget *h = label(text);
        gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
        gtk_label_set_xalign(GTK_LABEL(h), 0);
        gtk_box_pack_start(GTK_BOX(row), h, TRUE, TRUE, 0);
        gtk_grid_attach(GTK_GRID(page), box_panel("NOT SUPPORTED YET", row), 0, 0, 2, 1);
        gtk_widget_destroy(left);
        gtk_widget_destroy(right);
    } else {
        GtkWidget *none = label("No ROCCAT device found. Plug one in, it shows up here by itself.", "rc-hint");
        gtk_widget_set_margin_top(none, 40);
        gtk_grid_attach(GTK_GRID(page), none, 0, 0, 2, 1);
        gtk_widget_destroy(left);
        gtk_widget_destroy(right);
    }

    return page;
}

} // namespace gui
