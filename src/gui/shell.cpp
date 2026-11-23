// the window itself: header, the tab pages (built from what the connected device has), the profile bar with
// Apply / Reload in the footer, auto apply

#include "app.h"

namespace gui {

gboolean auto_apply_fire(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->auto_apply_source = 0;
    if (s->dev && s->loaded)
        on_apply(nullptr, s);
    return G_SOURCE_REMOVE;
}

void schedule_auto_apply(app_state *s)
{
    // whatever got changed is not on the device yet, until somebody saves (or auto apply does)
    if (!s->updating && s->loaded && !s->dirty) {
        s->dirty = true;
        update_profile_buttons(s);
    }
    if (s->updating || !s->loaded || !s->auto_apply ||
        !gtk_switch_get_active(GTK_SWITCH(s->auto_apply)))
        return;
    if (s->auto_apply_source)
        g_source_remove(s->auto_apply_source);
    if (s->engine_source)
        g_source_remove(s->engine_source);
    // debounce: one write after the user stops dragging / typing
    s->auto_apply_source = g_timeout_add(700, auto_apply_fire, s);
}

void on_any_change(GtkWidget *, gpointer data)
{
    schedule_auto_apply(static_cast<app_state *>(data));
}

void on_any_notify(GObject *, GParamSpec *, gpointer data)
{
    schedule_auto_apply(static_cast<app_state *>(data));
}

// Connects every editing widget below `w` to the auto-apply debouncer.
// Widgets tagged "rc-no-autoapply" (app preferences, engine switch) are skipped.
void hook_auto_apply(app_state *s, GtkWidget *w)
{
    if (g_object_get_data(G_OBJECT(w), "rc-no-autoapply"))
        return;
    if (GTK_IS_SPIN_BUTTON(w))
        g_signal_connect(w, "value-changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_ENTRY(w))
        g_signal_connect(w, "changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_COLOR_BUTTON(w))
        g_signal_connect(w, "color-set", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_TOGGLE_BUTTON(w))
        g_signal_connect(w, "toggled", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_COMBO_BOX(w))
        g_signal_connect(w, "changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_RANGE(w))
        g_signal_connect(w, "value-changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_SWITCH(w))
        g_signal_connect(w, "notify::active", G_CALLBACK(on_any_notify), s);
    if (GTK_IS_CONTAINER(w) && !GTK_IS_COMBO_BOX(w) && !GTK_IS_SPIN_BUTTON(w)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *c = children; c; c = c->next)
            hook_auto_apply(s, GTK_WIDGET(c->data));
        g_list_free(children);
    }
}

void on_auto_apply_toggled(GObject *sw, GParamSpec *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    bool on = gtk_switch_get_active(GTK_SWITCH(sw));
    gtk_widget_set_sensitive(s->apply_button, !on && s->dev);
    if (s->updating)
        return;
    store_prefs(s);
    if (on)
        set_status(s, "Auto apply on, every change gets written to the device right away", "connected");
}

// the tab pages, made from what the connected device has. called once with nothing connected (then there is only the
// Settings page with the application boxes) and again every time a device connects
void build_pages(app_state *s)
{
    int was = gtk_notebook_get_current_page(GTK_NOTEBOOK(s->notebook));
    const char *was_id = nullptr;
    if (GtkWidget *page = was >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(s->notebook), was) : nullptr)
        was_id = static_cast<const char *>(g_object_get_data(G_OBJECT(page), "rc-page"));
    std::string keep = was_id ? was_id : "";

    // whatever ticks for a page that is about to go has to stop first
    if (s->preview_source) {
        g_source_remove(s->preview_source);
        s->preview_source = 0;
    }
    if (s->auto_apply_source) {
        g_source_remove(s->auto_apply_source);
        s->auto_apply_source = 0;
    }
    keys_forget(s);
    while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(s->notebook)) > 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(s->notebook), 0);
    // every control of the old pages is gone now, nobody may hold on to one
    s->dpi.clear();
    s->polling.clear();
    s->buttons.clear();
    s->button_names.clear();
    s->led_color.clear();
    s->led_enable.clear();
    s->dpi_lock = s->dpi_panel = s->polling_panel = nullptr;
    s->effect = s->speed = s->brightness = s->group_color = s->theme = s->render_engine = nullptr;
    s->shift_enable = s->shift_color = s->shift_style = s->shift_hint = s->engine_label = s->colors_panel = nullptr;
    s->all_color = s->color_flow = s->idle_minutes = s->idle_style = s->idle_brightness = s->macro_feedback = nullptr;
    s->mouse_light = s->mouse_buttons = s->kb_view = s->kb_hint = s->fx_panel = s->fx_box = nullptr;
    s->idle_panel = s->idle_fx_panel = s->idle_fx_box = nullptr;
    s->es_enable = s->es_row = s->es_panel = s->es_effect = s->es_box = nullptr;
    s->kb_regions.clear();
    if (s->kb_rows)
        g_object_unref(s->kb_rows);
    s->kb_rows = gtk_size_group_new(GTK_SIZE_GROUP_VERTICAL);
    s->kb_layer = 0;
    s->fx_shown = -1;
    s->kb_hover = s->kb_drag = -1;
    s->idle_preview_t0 = -1;
    s->angle_snapping = s->debounce = s->left_handed = s->raw_view = s->tdcu_label = s->calib_button = nullptr;
    for (auto &w : s->theme_swatches)
        w = nullptr;
    s->highlighted_button = -1;

    // pages scroll up and down, never sideways: the window cant get narrower than the page needs, nothing gets cut off
    auto add = [s](const char *id, const char *title, GtkWidget *page) {
        GtkWidget *sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(sw), page);
        g_object_set_data(G_OBJECT(sw), "rc-page", const_cast<char *>(id));
        gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), sw, label(title));
    };
    const rockcat_caps *caps = s->caps;
    add("settings", "SETTINGS", build_settings_page(s));
    // a mouse gets its list of buttons, a keyboard the board to click keys on
    if (caps && caps->input_count > 0 && !is_keyboard(s))
        add("buttons", "BUTTON ASSIGNMENT", build_buttons_page(s));
    else if (caps && is_keyboard(s) && s->kb)
        add("buttons", "KEY ASSIGNMENT", build_keys_page(s));
    // also for boards with one light for everything (MK Glow, Sova membrane): no keys to paint, but effect and brightness
    if (caps && (caps->led_count > 0 || has_setting(s, ROCKCAT_SET_EFFECT) || has_setting(s, ROCKCAT_SET_BRIGHTNESS)))
        add("illumination", "ILLUMINATION", build_illumination_page(s));
    if (caps && caps->profile_count > 0)
        add("advanced", "ADVANCED SETTINGS", build_advanced_page(s));

    // auto apply: hook every editing widget except the ones that only pick something
    for (GtkWidget *w : {s->group_color, s->all_color})
        if (w)
            g_object_set_data(G_OBJECT(w), "rc-no-autoapply", GINT_TO_POINTER(1));
    hook_auto_apply(s, s->notebook);

    // how small the window may get. the footer (profiles and the buttons on the right, one row) needs about that
    gtk_widget_set_size_request(s->window, 1080, 640);
    gtk_widget_show_all(s->notebook);
    set_engine_label(s);
    for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(s->notebook)); ++i) {
        const char *id = static_cast<const char *>(g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(s->notebook), i)), "rc-page"));
        if (id && keep == id)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), i);
    }
}

void on_destroy(GtkWidget *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->hotplug_source)
        g_source_remove(s->hotplug_source);
    if (s->preview_source)
        g_source_remove(s->preview_source);
    if (s->auto_apply_source)
        g_source_remove(s->auto_apply_source);
    if (s->engine)
        rockcat_engine_stop(s->engine);
    for (auto &kb : s->kb_engines)
        rockcat_engine_stop(kb.second);
    if (s->dev)
        rockcat_close(s->dev);
    view_free(s->view);
    keyboard_free(s->kb);
#ifdef ROCKCAT_HAVE_TRAY
    if (s->indicator)
        g_object_unref(s->indicator);
#endif
    keys_forget(s);
    delete s;
}

} // namespace gui

using namespace gui;

GtkWidget *rockcat_window_new(GtkApplication *app, bool force_minimized)
{
    auto *s = new app_state;
    s->window = gtk_application_window_new(app);
    g_signal_connect(s->window, "delete-event", G_CALLBACK(on_delete), s);
    gtk_window_set_title(GTK_WINDOW(s->window), "Rockcat Swarm");
    // the key assignment page listens for keys to record, before anything in the window gets them
    auto on_key = +[](GtkWidget *, GdkEventKey *e, gpointer data) -> gboolean { return keys_on_key(static_cast<app_state *>(data), e); };
    g_signal_connect(s->window, "key-press-event", G_CALLBACK(on_key), s);
    g_signal_connect(s->window, "key-release-event", G_CALLBACK(on_key), s);
    if (GdkPixbuf *icon = gdk_pixbuf_new_from_resource_at_scale("/org/rockcat/swarm/rockcat-swarm.svg", 256, 256, TRUE, nullptr)) {
        gtk_window_set_icon(GTK_WINDOW(s->window), icon);
        g_object_unref(icon);
    }
    gtk_window_set_default_size(GTK_WINDOW(s->window), 1240, 800);
    // below this the pages stop making sense (keyboard and its settings column side by side). bigger is fine, the pages spread out
    gtk_widget_set_size_request(s->window, 1080, 640);
    add_class(s->window, "rockcat");
    g_signal_connect(s->window, "destroy", G_CALLBACK(on_destroy), s);
    g_object_set_data(G_OBJECT(s->window), "rockcat-state", s);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(s->window), root);

    // header
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    add_class(header, "rc-header");
    GtkWidget *logo_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GdkPixbuf *logo = gdk_pixbuf_new_from_resource_at_scale("/org/rockcat/swarm/rockcat-swarm.svg", 44, 44, TRUE, nullptr);
    if (logo) {
        GtkWidget *img = gtk_image_new_from_pixbuf(logo);
        gtk_widget_set_valign(img, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(logo_box), img, FALSE, FALSE, 0);
        g_object_unref(logo);
    }
    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(brand), label("ROCKCAT", "rc-brand"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brand), label("SWARM", "rc-brand-sub"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(logo_box), brand, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), logo_box, FALSE, FALSE, 0);
    s->device_icon = gtk_image_new();
    gtk_widget_set_no_show_all(s->device_icon, TRUE);
    gtk_widget_set_margin_start(s->device_icon, 12);
    gtk_box_pack_start(GTK_BOX(header), s->device_icon, FALSE, FALSE, 0);
    GtkWidget *dev = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    s->device_label = label("NO DEVICE", "rc-device-name");
    gtk_label_set_ellipsize(GTK_LABEL(s->device_label), PANGO_ELLIPSIZE_END);
    s->status_label = label("", "rc-status");
    gtk_box_pack_start(GTK_BOX(dev), s->device_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dev), s->status_label, FALSE, FALSE, 0);
    gtk_widget_set_valign(dev, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(header), dev, TRUE, TRUE, 0);
    // settings of the program itself (tray, about), nothing in there touches a device
    GtkWidget *gear = gtk_button_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_button_set_relief(GTK_BUTTON(gear), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(gear, "Settings of rockcat-swarm itself (tray, about). Nothing in there changes a device");
    gtk_widget_set_valign(gear, GTK_ALIGN_CENTER);
    g_signal_connect(gear, "clicked", G_CALLBACK(on_app_settings), s);
    gtk_box_pack_end(GTK_BOX(header), gear, FALSE, FALSE, 0);
    s->header = header;
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    // one tab per plugged in device, filled by refresh_device_list. out of sight while there is only one
    s->device_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(s->device_bar, "rc-device-bar");
    // in a scroller because some of us own way too many of these things and the bar would otherwise push the window wider
    // than the screen. sideways only, the wheel scrolls it sideways too (gtk does that by itself when there is no vertical bar)
    s->device_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s->device_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(s->device_scroll), TRUE);
    gtk_container_add(GTK_CONTAINER(s->device_scroll), s->device_bar);
    gtk_widget_show(s->device_bar);
    gtk_widget_set_no_show_all(s->device_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(root), s->device_scroll, FALSE, FALSE, 0);

    // pages
    s->notebook = gtk_notebook_new();
    add_class(s->notebook, "rc-tabs");
    build_pages(s);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(s->notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(root), s->notebook, TRUE, TRUE, 0);

    // footer: profiles + actions
    // Two groups side by side, always on one row: the window cant get narrower than that.
    GtkWidget *footer = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(footer), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(footer), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(footer), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(footer), 6);
    add_class(footer, "rc-footer");
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(left, GTK_ALIGN_START);
    gtk_widget_set_halign(right, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(left), title_label("PROFILE"), FALSE, FALSE, 4);
    for (int i = 0; i < N_PROFILES; ++i) {
        GtkWidget *b = gtk_toggle_button_new_with_label("");
        add_class(b, "rc-profile");
        g_object_set_data(G_OBJECT(b), "profile", GINT_TO_POINTER(i));
        g_signal_connect(b, "toggled", G_CALLBACK(on_profile_toggled), s);
        s->profile_buttons[i] = b;
        gtk_box_pack_start(GTK_BOX(left), b, FALSE, FALSE, 0);
    }
    // a tick: make the profile being edited the one the device uses
    s->activate_button = gtk_button_new_from_icon_name("object-select-symbolic", GTK_ICON_SIZE_BUTTON);
    add_class(s->activate_button, "rc-profile");
    gtk_widget_set_tooltip_text(s->activate_button, "Make the profile being edited the active one on the device");
    g_signal_connect(s->activate_button, "clicked", G_CALLBACK(on_activate_profile), s);
    gtk_box_pack_start(GTK_BOX(left), s->activate_button, FALSE, FALSE, 12);

    s->apply_button = gtk_button_new_with_label("SAVE TO DEVICE");
    add_class(s->apply_button, "suggested-action");
    gtk_widget_set_tooltip_text(s->apply_button, "Write the profile being edited to the device");
    g_signal_connect(s->apply_button, "clicked", G_CALLBACK(on_apply), s);
    s->reload_button = gtk_button_new_with_label("READ FROM DEVICE");
    gtk_widget_set_tooltip_text(s->reload_button, "Forget the changes and read the profile from the device again");
    g_signal_connect(s->reload_button, "clicked", G_CALLBACK(on_reload), s);
    s->auto_apply = gtk_switch_new();
    gtk_widget_set_valign(s->auto_apply, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(s->auto_apply, "Write every change to the device automatically");
    GtkWidget *aa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(aa), label("AUTO APPLY", "rc-hint"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(aa), s->auto_apply, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), s->apply_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), s->reload_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), aa, FALSE, FALSE, 8);
    gtk_container_add(GTK_CONTAINER(footer), left);
    gtk_container_add(GTK_CONTAINER(footer), right);
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);

    update_profile_buttons(s);
    g_signal_connect_swapped(s->window, "map", G_CALLBACK(connect_device), s);
    s->hotplug_source = g_timeout_add_seconds(2, hotplug_tick, s);

    // restore application preferences (the two on the Settings page get theirs in build_pages)
    gui_prefs prefs = load_prefs();
    s->updating = true;
    g_signal_connect(s->auto_apply, "notify::active", G_CALLBACK(on_auto_apply_toggled), s);
    gtk_switch_set_active(GTK_SWITCH(s->auto_apply), prefs.auto_apply);
    gtk_widget_set_sensitive(s->apply_button, !prefs.auto_apply);
#ifdef ROCKCAT_HAVE_TRAY
    s->tray_icon = prefs.tray_icon || force_minimized;
    s->tray_style = prefs.tray_style;
    s->tray_enabled = (prefs.tray_icon && prefs.tray) || force_minimized;
#endif
    s->updating = false;

    start_engine(s);
    s->engine_source = g_timeout_add(500, engine_tick, s);

    bool start_hidden = s->tray_enabled && (force_minimized || prefs.start_minimized);
    gtk_widget_show_all(root);
    if (!start_hidden)
        show_window(s);
    else
        tray_sync(s);
    g_object_set_data(G_OBJECT(s->window), "rockcat-hidden", GINT_TO_POINTER(start_hidden));
    return s->window;
}

void rockcat_window_show_page(GtkWidget *window, const char *page)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rockcat-state"));
    if (!s || !page)
        return;
    // remembered too, the pages get built again once the device has been read and the page should still be the one asked for
    for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(s->notebook)); ++i) {
        const char *id = static_cast<const char *>(g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(s->notebook), i)), "rc-page"));
        if (id && std::strcmp(id, page) == 0)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), i);
    }
}

// the debug menu: a bug left of the gear, with every pretend device the library has. ticking one plugs it in, it then
// shows up as a device tab like a real one. for trying the pages of devices nobody here owns
static void on_sim_toggled(GtkCheckMenuItem *item, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const char *name = static_cast<const char *>(g_object_get_data(G_OBJECT(item), "sim"));
    rockcat_sim_plug(name, gtk_check_menu_item_get_active(item));
    refresh_device_list(s);
}

void rockcat_window_set_debug(GtkWidget *window, bool on)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rockcat-state"));
    if (!s || !on || !s->header)
        return;
    GtkWidget *bug = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(bug), gtk_image_new_from_resource("/org/rockcat/swarm/rc-bug.svg"));
    gtk_button_set_relief(GTK_BUTTON(bug), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(bug, "Debug: plug in pretend devices");
    gtk_widget_set_valign(bug, GTK_ALIGN_CENTER);
    GtkWidget *menu = gtk_menu_new();
    for (int i = 0; const char *name = rockcat_sim_name(i); ++i) {
        char text[96];
        std::snprintf(text, sizeof text, "%s  (%s)", rockcat_sim_label(i), name);
        GtkWidget *item = gtk_check_menu_item_new_with_label(text);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), rockcat_sim_plugged(name));
        g_object_set_data(G_OBJECT(item), "sim", const_cast<char *>(name));
        g_signal_connect(item, "toggled", G_CALLBACK(on_sim_toggled), s);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(bug), menu);
    gtk_box_pack_end(GTK_BOX(s->header), bug, FALSE, FALSE, 0);
    gtk_widget_show_all(bug);
}

void rockcat_window_pick_key(GtkWidget *window, int input)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rockcat-state"));
    if (s)
        keys_pick(s, input);
}

void rockcat_window_pick_effect(GtkWidget *window, int effect)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rockcat-state"));
    if (!s || !s->effect)
        return;
    char id[8];
    std::snprintf(id, sizeof id, "%d", effect);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->effect), id);
}

void rockcat_window_edit_profile(GtkWidget *window, int profile)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rockcat-state"));
    if (!s || profile < 0 || profile >= N_PROFILES)
        return;
    // the window may have found its device already (that happens as soon as it shows), then its a plain profile switch
    if (s->dev) {
        s->edit_profile = profile;
        update_profile_buttons(s);
        load_profile(s);
    } else {
        s->want_profile = profile;
    }
}

void rockcat_window_show_device(GtkWidget *window, const char *which)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rockcat-state"));
    if (!s || !which)
        return;
    rockcat_device_info infos[16];
    int n = std::min(std::max(rockcat_enumerate(infos, 16), 0), 16);
    for (int i = 0; i < n; ++i) {
        char pid[8];
        std::snprintf(pid, sizeof pid, "%04x", infos[i].product_id);
        if (g_ascii_strcasecmp(which, pid) != 0 && std::strcmp(which, infos[i].path) != 0)
            continue;
        if (s->dev && std::strcmp(infos[i].path, s->info.path) == 0)
            return;
        s->want_path = infos[i].path;
        // the window may have connected to the first device it found already, then this is a switch
        if (s->dev) {
            close_device(s);
            connect_device(s);
        }
        return;
    }
}

