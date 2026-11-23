// finding the device, reading and writing profiles, hotplug, and the buttons in the footer that do those things

#include "app.h"

namespace gui {

// reads the profile being edited into s->profile. says so itself when that fails
bool read_profile(app_state *s)
{
    if (!s->dev)
        return false;
    GdkWindow *gw = gtk_widget_get_window(s->window);
    GdkCursor *busy = gw ? gdk_cursor_new_from_name(gdk_window_get_display(gw), "wait") : nullptr;
    if (gw)
        gdk_window_set_cursor(gw, busy);
    while (gtk_events_pending())
        gtk_main_iteration();

    rockcat_profile *fresh = nullptr;
    int r = rockcat_profile_read(s->dev, s->edit_profile, &fresh);
    if (r == ROCKCAT_OK) {
        rockcat_profile_free(s->profile);
        s->profile = fresh;
    }

    if (gw)
        gdk_window_set_cursor(gw, nullptr);
    if (busy)
        g_object_unref(busy);

    if (r != ROCKCAT_OK) {
        if (r == ROCKCAT_ERR_IO)
            disconnect(s, "Device disconnected");
        else
            report_error(s, "Reading profile failed", r);
        return false;
    }
    return true;
}

// a keyboard that some tool left in "the pc sets the lights" mode stays dark (or stuck on the last frame) through profile
// switches and applies, until somebody tells it to show its own profile again. the window does that whenever it reads or
// writes one, so opening the gui is always enough to get the lights back. not for devices the lighting engine draws on,
// those are the engines to manage
void lights_to_profile(app_state *s)
{
    if (!s->dev || !s->caps || is_kone(s) || !(s->caps->features & ROCKCAT_HAS_DIRECT_LIGHT))
        return;
    // with an engine on this keyboard its the engines call: it looks at the active profile again and either takes the
    // leds (wave, ripple, heatmap) or hands them back. without one we hand them back ourselves
    if (rockcat_engine *engine = keyboard_engine(s))
        rockcat_engine_reload(engine);
    else
        rockcat_direct_end(s->dev);
}

// the engine of the keyboard that is showing, nullptr if it has none
rockcat_engine *keyboard_engine(app_state *s)
{
    auto it = s->kb_engines.find(s->info.path);
    return it == s->kb_engines.end() ? nullptr : it->second;
}

// puts what was read into the widgets
void show_profile(app_state *s)
{
    lights_to_profile(s);
    s->loaded = true;
    fill(s);
    update_tdcu_label(s);
    char msg[96];
    std::snprintf(msg, sizeof msg, "Connected, editing profile %d", s->edit_profile + 1);
    set_status(s, msg, "connected");
}

bool load_profile(app_state *s)
{
    if (!read_profile(s))
        return false;
    show_profile(s);
    s->dirty = false;
    update_profile_buttons(s);
    return true;
}

// the picture next to the device name. comes out of the asset folder the build was pointed at, a device nobody added a picture for simply gets none
void show_device_icon(app_state *s, int pid)
{
    char path[96];
    std::snprintf(path, sizeof path, "/org/rockcat/swarm/assets/devices/%04x.png", pid);
    GdkPixbuf *pix = pid < 0 ? nullptr : gdk_pixbuf_new_from_resource_at_scale(path, 56, 56, TRUE, nullptr);
    gtk_image_set_from_pixbuf(GTK_IMAGE(s->device_icon), pix);
    gtk_widget_set_visible(s->device_icon, pix != nullptr);
    if (pix)
        g_object_unref(pix);
}

void connect_device(app_state *s)
{
    if (s->dev)
        return;
    s->unsupported_pid = 0;
    s->unsupported_path.clear();
    // the one that was picked in the list if its still there, else whatever comes first
    int r = s->want_path.empty() ? ROCKCAT_ERR_NO_DEVICE : rockcat_open(s->want_path.c_str(), &s->dev);
    if (r != ROCKCAT_OK) {
        s->want_path.clear();
        r = rockcat_open(nullptr, &s->dev);
    }
    if (r != ROCKCAT_OK) {
        s->dev = nullptr;
        gtk_label_set_text(GTK_LABEL(s->device_label), "NO DEVICE");
        set_status(s, r == ROCKCAT_ERR_NO_DEVICE ? "Plug in a ROCCAT device (rockcat-cli list says which ones work)"
                                                : rockcat_strerror(r),
                   r == ROCKCAT_ERR_NO_DEVICE ? nullptr : "error");
        set_controls_sensitive(s, false);
        return;
    }
    rockcat_get_info(s->dev, &s->info);
    s->caps = rockcat_get_caps(s->dev);
    view_free(s->view);
    s->view = view_load(s->info.product_id);
    keyboard_free(s->kb);
    s->kb = nullptr;
    keyboard_load(s);
    char fw[16], title[128];
    rockcat_get_firmware_version(s->dev, fw, sizeof fw);
    s->firmware = fw;
    std::snprintf(title, sizeof title, "%s   |   FW %s", s->info.name, fw);
    gtk_label_set_text(GTK_LABEL(s->device_label), title);
    show_device_icon(s, s->info.product_id);

    uint8_t active = 0;
    s->active_profile = 0;
    if (rockcat_get_active_profile(s->dev, &active, nullptr) == ROCKCAT_OK && active < n_profiles(s))
        s->active_profile = active;
    s->edit_profile = s->want_profile >= 0 && s->want_profile < n_profiles(s) ? s->want_profile : s->active_profile;
    s->want_profile = -1;
    refresh_device_list(s);
    gtk_widget_set_visible(s->activate_button, n_profiles(s) > 1);
    // a device we know by name only: its picture and what we know about it, no profile to read, nothing to send
    if (s->caps && s->caps->profile_count == 0) {
        rockcat_profile_free(s->profile);
        s->profile = nullptr;
        s->loaded = false;
        build_pages(s);
        update_profile_buttons(s);
        set_controls_sensitive(s, false);
        set_status(s, "Known by name only, rockcat cant talk to this one yet", nullptr);
        return;
    }
    // the pages get built from what this device has, and some of that only the profile can say. so: read first, build, then fill
    if (!read_profile(s))
        return;
    build_pages(s);
    update_profile_buttons(s);
    set_controls_sensitive(s, true);
    show_profile(s);
}

// lets go of the device without calling it an error, for switching to another one
void close_device(app_state *s)
{
    if (s->dev)
        rockcat_close(s->dev);
    s->dev = nullptr;
    rockcat_profile_free(s->profile);
    s->profile = nullptr;
    s->loaded = false;
}

// one of the device tabs got clicked. tabs work like the profile buttons: exactly one is down
void on_device_tab(GtkToggleButton *tab, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    const char *path = static_cast<const char *>(g_object_get_data(G_OBJECT(tab), "path"));
    bool current = s->dev && path && std::strcmp(path, s->info.path) == 0;
    if (!gtk_toggle_button_get_active(tab)) {
        // clicking the one that is showing doesnt switch it off
        if (current) {
            s->updating = true;
            gtk_toggle_button_set_active(tab, TRUE);
            s->updating = false;
        }
        return;
    }
    if (!path || current || (s->unsupported_pid && s->unsupported_path == path))
        return;
    // whatever was changed and not applied on the other device is gone, same as when switching profiles
    close_device(s);
    if (std::strncmp(path, "unsupported:", 12) == 0) {
        show_unsupported(s, path);
        return;
    }
    s->unsupported_pid = 0;
    s->unsupported_path.clear();
    s->want_path = path;
    connect_device(s);
}

// a ROCCAT device the library doesnt know yet: its tab shows that it was found, with its picture if we have one, and
// nothing gets sent to it
void show_unsupported(app_state *s, const char *path)
{
    unsigned pid = 0;
    char name[128] = "";
    std::sscanf(path, "unsupported:%x:%127[^\n]", &pid, name);
    s->unsupported_pid = static_cast<int>(pid);
    s->unsupported_path = path;
    s->unsupported_name = name;
    s->caps = nullptr;
    std::memset(&s->info, 0, sizeof s->info);
    gtk_label_set_text(GTK_LABEL(s->device_label), name);
    show_device_icon(s, static_cast<int>(pid));
    build_pages(s);
    set_controls_sensitive(s, false);
    set_status(s, "Found, but rockcat cant talk to this one yet", nullptr);
    refresh_device_list(s);
}

// what is plugged in, as a row of tabs under the header. with one device there is nothing to choose and the row stays away
void refresh_device_list(app_state *s)
{
    rockcat_device_info infos[16];
    int n = std::min(std::max(rockcat_enumerate(infos, 16), 0), 16);
    std::string now;
    for (int i = 0; i < n; ++i)
        now += std::string(infos[i].path) + "\n";
    // ROCCAT devices the library doesnt know yet get a tab too, so nobody wonders where their keyboard went. they show
    // up once per model (a device has several hidraw nodes and nothing to tell two of the same apart by)
    std::vector<std::pair<uint16_t, std::string>> unknown;
    {
        rockcat_device_info raw[48];
        int m = std::min(std::max(rockcat_enumerate_raw(raw, 48), 0), 48);
        for (int i = 0; i < m; ++i) {
            uint16_t pid = 0;
            bool known = false;
            for (int k = 0; rockcat_model_at(k, &pid, nullptr, nullptr) == ROCKCAT_OK; ++k)
                known = known || pid == raw[i].product_id;
            if (known || std::strncmp(raw[i].path, "sim:", 4) == 0)
                continue;
            bool seen = false;
            for (const auto &u : unknown)
                seen = seen || u.first == raw[i].product_id;
            if (!seen) {
                char name[80];
                std::snprintf(name, sizeof name, "%s", raw[i].name[0] ? raw[i].name : "ROCCAT device");
                unknown.push_back({raw[i].product_id, name});
                char key[128];
                std::snprintf(key, sizeof key, "unsupported:%04x:%s\n", raw[i].product_id, name);
                now += key;
            }
        }
    }
    // every keyboard the pc can draw on gets its engine as soon as it shows up and loses it when it goes. a start that
    // fails (somebody else has it, the service or the cli) gets tried again on the next round
    for (int i = 0; i < n; ++i) {
        const rockcat_caps *caps = nullptr;
        uint16_t pid = 0;
        for (int m = 0; rockcat_model_at(m, &pid, nullptr, &caps) == ROCKCAT_OK; ++m)
            if (pid == infos[i].product_id)
                break;
        // (a Kone AIMO has the engine that was there first, s->engine)
        if (pid != infos[i].product_id || !caps || caps->carrier_effect == ROCKCAT_EFFECT_AIMO || !(caps->features & ROCKCAT_HAS_DIRECT_LIGHT))
            continue;
        if (s->kb_engines.count(infos[i].path))
            continue;
        rockcat_engine *engine = nullptr;
        if (rockcat_engine_start(infos[i].path, ROCKCAT_ENGINE_AUTO, &engine) == ROCKCAT_OK)
            s->kb_engines[infos[i].path] = engine;
    }
    for (auto it = s->kb_engines.begin(); it != s->kb_engines.end();) {
        if (now.find(it->first + "\n") == std::string::npos) {
            rockcat_engine_stop(it->second);
            it = s->kb_engines.erase(it);
        } else {
            ++it;
        }
    }
    s->updating = true;
    if (now != s->listed) {
        s->listed = now;
        GList *old = gtk_container_get_children(GTK_CONTAINER(s->device_bar));
        for (GList *c = old; c; c = c->next)
            gtk_widget_destroy(GTK_WIDGET(c->data));
        g_list_free(old);
        for (int i = 0; i < n; ++i) {
            GtkWidget *tab = gtk_toggle_button_new();
            add_class(tab, "rc-device-tab");
            GtkWidget *in = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            char res[96];
            std::snprintf(res, sizeof res, "/org/rockcat/swarm/assets/devices/%04x.png", infos[i].product_id);
            if (GdkPixbuf *pix = gdk_pixbuf_new_from_resource_at_scale(res, 30, 30, TRUE, nullptr)) {
                gtk_box_pack_start(GTK_BOX(in), gtk_image_new_from_pixbuf(pix), FALSE, FALSE, 0);
                g_object_unref(pix);
            }
            // everything here is a ROCCAT, the tab doesnt have to say so every time
            const char *name = infos[i].name;
            if (std::strncmp(name, "ROCCAT ", 7) == 0)
                name += 7;
            gtk_box_pack_start(GTK_BOX(in), label(name), FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(tab), in);
            g_object_set_data_full(G_OBJECT(tab), "path", g_strdup(infos[i].path), g_free);
            g_signal_connect(tab, "toggled", G_CALLBACK(on_device_tab), s);
            gtk_box_pack_start(GTK_BOX(s->device_bar), tab, FALSE, FALSE, 0);
            // (show_all on the bar itself does nothing, the bar is marked to stay out of show_all)
            gtk_widget_show_all(tab);
        }
        for (const auto &u : unknown) {
            GtkWidget *tab = gtk_toggle_button_new();
            add_class(tab, "rc-device-tab");
            GtkWidget *in = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            char res[96];
            std::snprintf(res, sizeof res, "/org/rockcat/swarm/assets/devices/%04x.png", u.first);
            if (GdkPixbuf *pix = gdk_pixbuf_new_from_resource_at_scale(res, 30, 30, TRUE, nullptr)) {
                gtk_box_pack_start(GTK_BOX(in), gtk_image_new_from_pixbuf(pix), FALSE, FALSE, 0);
                g_object_unref(pix);
            }
            std::string name = u.second.rfind("ROCCAT ", 0) == 0 ? u.second.substr(7) : u.second;
            GtkWidget *l = label((name + " (not supported yet)").c_str());
            gtk_widget_set_opacity(l, 0.6);
            gtk_box_pack_start(GTK_BOX(in), l, FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(tab), in);
            char path[128];
            std::snprintf(path, sizeof path, "unsupported:%04x:%s", u.first, u.second.c_str());
            g_object_set_data_full(G_OBJECT(tab), "path", g_strdup(path), g_free);
            g_signal_connect(tab, "toggled", G_CALLBACK(on_device_tab), s);
            gtk_box_pack_start(GTK_BOX(s->device_bar), tab, FALSE, FALSE, 0);
            gtk_widget_show_all(tab);
        }
        gtk_widget_set_visible(s->device_bar, n + static_cast<int>(unknown.size()) > 1);
    }
    GList *tabs = gtk_container_get_children(GTK_CONTAINER(s->device_bar));
    for (GList *c = tabs; c; c = c->next) {
        const char *path = static_cast<const char *>(g_object_get_data(G_OBJECT(c->data), "path"));
        bool on = path && ((s->dev && std::strcmp(path, s->info.path) == 0) || (!s->dev && s->unsupported_pid && s->unsupported_path == path));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->data), on);
    }
    g_list_free(tabs);
    s->updating = false;
}

void disconnect(app_state *s, const char *why)
{
    if (s->dev)
        rockcat_close(s->dev);
    s->dev = nullptr;
    rockcat_profile_free(s->profile);
    s->profile = nullptr;
    s->loaded = false;
    gtk_label_set_text(GTK_LABEL(s->device_label), "NO DEVICE");
    show_device_icon(s, -1);
    set_status(s, why, "error");
    set_controls_sensitive(s, false);
}

gboolean hotplug_tick(gpointer data)
{
    // runs for as long as the window lives: keeps the list of devices current, and connects when there is something
    // to connect to and nothing connected
    auto *s = static_cast<app_state *>(data);
    refresh_device_list(s);
    // a tab of a device we cant talk to is showing on purpose, no s->dev is fine then. unless it got unplugged
    if (s->unsupported_pid) {
        if (s->listed.find(s->unsupported_path + "\n") != std::string::npos)
            return G_SOURCE_CONTINUE;
        s->unsupported_pid = 0;
        s->unsupported_path.clear();
    }
    if (!s->dev && !s->listed.empty())
        connect_device(s);
    return G_SOURCE_CONTINUE;
}

void write_text_macros(app_state *s)
{
    for (int i = 0; i < static_cast<int>(s->buttons.size()); ++i) {
        if (combo_type(s->buttons[i].combo) != ROCKCAT_BT_MACRO)
            continue;
        const char *text = gtk_entry_get_text(GTK_ENTRY(s->buttons[i].text));
        if (!text || !*text)
            continue; // keep whatever macro is stored
        static rockcat_macro m;
        std::memset(&m, 0, sizeof m);
        m.loop = 1;
        std::snprintf(m.set_name, sizeof m.set_name, "rockcat");
        std::snprintf(m.name, sizeof m.name, "button %d", i + 1);
        for (const char *p = text; *p && m.count + 4 <= ROCKCAT_MACRO_KEYSTROKES; ++p) {
            uint8_t usage = 0;
            bool shift = false;
            char ch = *p;
            if (ch >= 'a' && ch <= 'z') usage = static_cast<uint8_t>(0x04 + ch - 'a');
            else if (ch >= 'A' && ch <= 'Z') { usage = static_cast<uint8_t>(0x04 + ch - 'A'); shift = true; }
            else if (ch >= '1' && ch <= '9') usage = static_cast<uint8_t>(0x1e + ch - '1');
            else if (ch == '0') usage = 0x27;
            else if (ch == ' ') usage = 0x2c;
            else if (ch == '.') usage = 0x37;
            else if (ch == ',') usage = 0x36;
            else if (ch == '-') usage = 0x2d;
            else continue;
            if (shift) m.keys[m.count++] = {0xe1, ROCKCAT_KEY_PRESS, 20};
            m.keys[m.count++] = {usage, ROCKCAT_KEY_PRESS, 20};
            m.keys[m.count++] = {usage, ROCKCAT_KEY_RELEASE, 20};
            if (shift) m.keys[m.count++] = {0xe1, ROCKCAT_KEY_RELEASE, 20};
        }
        int r = rockcat_profile_set_macro(s->profile, i / n_inputs(s), i % n_inputs(s), &m);
        if (r == ROCKCAT_OK)
            r = rockcat_profile_write(s->dev, s->profile);
        if (r != ROCKCAT_OK)
            report_error(s, "Writing macro failed", r);
    }
}

void on_apply(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev || !s->loaded || !s->profile)
        return;
    collect(s);
    set_status(s, "Writing to device...", nullptr);
    while (gtk_events_pending())
        gtk_main_iteration();
    // settings and buttons first, the typed macros in a second go after that (write_text_macros), same order this always had
    int r = rockcat_profile_write(s->dev, s->profile);
    if (r != ROCKCAT_OK) {
        if (r == ROCKCAT_ERR_IO)
            disconnect(s, "Device disconnected");
        else
            report_error(s, "Writing failed", r);
        return;
    }
    write_text_macros(s);
    if (s->engine)
        rockcat_engine_reload(s->engine);
    char msg[64];
    std::snprintf(msg, sizeof msg, "Profile %d saved to the %s", s->edit_profile + 1, thing(s));
    set_status(s, msg, "connected");
    // re-read so the raw view and unknown bytes reflect the device state
    load_profile(s);
}

void on_reload(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        connect_device(s);
    else
        load_profile(s);
}

void on_activate_profile(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    int r = rockcat_set_active_profile(s->dev, static_cast<uint8_t>(s->edit_profile));
    if (r != ROCKCAT_OK) {
        report_error(s, "Switching profile failed", r);
        return;
    }
    s->active_profile = s->edit_profile;
    update_profile_buttons(s);
    if (s->engine)
        rockcat_engine_reload(s->engine);
    if (rockcat_engine *engine = keyboard_engine(s))
        rockcat_engine_reload(engine);
    char msg[64];
    std::snprintf(msg, sizeof msg, "Profile %d is now active", s->active_profile + 1);
    set_status(s, msg, "connected");
}

void on_profile_toggled(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    if (!gtk_toggle_button_get_active(b)) {
        // keep exactly one profile selected
        int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "profile"));
        if (idx == s->edit_profile) {
            s->updating = true;
            gtk_toggle_button_set_active(b, TRUE);
            s->updating = false;
        }
        return;
    }
    int want = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "profile"));
    // changes that are not on the device yet get asked about first
    if (!settle_changes(s)) {
        s->updating = true;
        gtk_toggle_button_set_active(b, FALSE);
        s->updating = false;
        update_profile_buttons(s);
        return;
    }
    s->edit_profile = want;
    update_profile_buttons(s);
    load_profile(s);
}

} // namespace gui
