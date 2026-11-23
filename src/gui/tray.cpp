// system tray icon and what closing the window means when there is one

#include "app.h"

namespace gui {

void show_window(app_state *s)
{
    gtk_widget_show(s->window);
    gtk_window_present(GTK_WINDOW(s->window));
    tray_sync(s);
}

void hide_window(app_state *s)
{
    gtk_widget_hide(s->window);
    tray_sync(s);
}

#ifdef ROCKCAT_HAVE_TRAY
std::string tray_icon_dir()
{
    return std::string(g_get_user_cache_dir()) + "/rockcat/icons";
}

// the tray host loads icons by name out of a folder, so the bundled svg gets dropped where it can find it (works without
// installing too). the white and black ones sit next to the coloured one in data/.
// every style gets its own name, a tray host that has seen a name once keeps showing that picture no matter whats in the file
const char *tray_icon_file(int style)
{
    static const char *const names[] = {"rockcat-swarm-tray", "rockcat-swarm-tray-white", "rockcat-swarm-tray-black"};
    style = style >= 0 && style < 3 ? style : 0;
    std::string dir = tray_icon_dir();
    g_mkdir_with_parents(dir.c_str(), 0755);
    static const char *const sources[] = {"rockcat-swarm.svg", "rockcat-swarm-tray-white.svg", "rockcat-swarm-tray-black.svg"};
    if (GBytes *svg = g_resources_lookup_data((std::string("/org/rockcat/swarm/") + sources[style]).c_str(), G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr)) {
        gsize len = 0;
        const char *data = static_cast<const char *>(g_bytes_get_data(svg, &len));
        g_file_set_contents((dir + "/" + names[style] + ".svg").c_str(), data, static_cast<gssize>(len), nullptr);
        g_bytes_unref(svg);
    }
    return names[style];
}

void on_tray_show(GtkMenuItem *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (gtk_widget_get_visible(s->window))
        hide_window(s);
    else
        show_window(s);
}

void on_tray_quit(GtkMenuItem *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->quitting = true;
    gtk_widget_destroy(s->window);
}
#endif

void tray_sync(app_state *s)
{
#ifdef ROCKCAT_HAVE_TRAY
    if (!s->tray_icon) {
        if (s->indicator)
            app_indicator_set_status(s->indicator, APP_INDICATOR_STATUS_PASSIVE);
        return;
    }
    const char *icon = tray_icon_file(s->tray_style);
    if (!s->indicator) {
        s->indicator = app_indicator_new_with_path("rockcat-swarm", icon, APP_INDICATOR_CATEGORY_HARDWARE, tray_icon_dir().c_str());
        app_indicator_set_title(s->indicator, "Rockcat Swarm");
        GtkWidget *menu = gtk_menu_new();
        s->tray_show_item = gtk_menu_item_new_with_label("Show Rockcat Swarm");
        g_signal_connect(s->tray_show_item, "activate", G_CALLBACK(on_tray_show), s);
        GtkWidget *quit = gtk_menu_item_new_with_label("Quit");
        g_signal_connect(quit, "activate", G_CALLBACK(on_tray_quit), s);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), s->tray_show_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
        gtk_widget_show_all(menu);
        app_indicator_set_menu(s->indicator, GTK_MENU(menu));
        // middle click on the icon toggles the window
        app_indicator_set_secondary_activate_target(s->indicator, s->tray_show_item);
    }
    app_indicator_set_icon_full(s->indicator, icon, "Rockcat Swarm");
    app_indicator_set_status(s->indicator, APP_INDICATOR_STATUS_ACTIVE);
    gtk_menu_item_set_label(GTK_MENU_ITEM(s->tray_show_item),
                            gtk_widget_get_visible(s->window) ? "Hide Rockcat Swarm" : "Show Rockcat Swarm");
#else
    (void)s;
#endif
}

gboolean on_delete(GtkWidget *, GdkEvent *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->tray_enabled && !s->quitting) {
        hide_window(s);
        return TRUE; // keep running in the tray
    }
    return FALSE;
}

void store_prefs(app_state *s)
{
    // the tray boxes only exist while the application settings are open, otherwise what the file says stays
    gui_prefs p = load_prefs();
    if (s->pref_tray_icon)
        p.tray_icon = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray_icon));
    if (s->pref_tray_style)
        p.tray_style = gtk_combo_box_get_active(GTK_COMBO_BOX(s->pref_tray_style));
    if (s->pref_tray)
        p.tray = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray));
    if (s->pref_start_min)
        p.start_minimized = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_start_min));
    p.auto_apply = gtk_switch_get_active(GTK_SWITCH(s->auto_apply));
    save_prefs(p);
}

// the boxes below the icon one keep their tick while greyed out, so turning the icon back on brings them back as they were
void tray_boxes_sensitive(app_state *s)
{
    const bool icon = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray_icon));
    gtk_widget_set_sensitive(s->pref_tray, icon);
    gtk_widget_set_sensitive(s->pref_tray_style_row, icon);
    gtk_widget_set_sensitive(s->pref_start_min, icon && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray)));
}

void tray_from_boxes(app_state *s)
{
    s->tray_icon = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray_icon));
    s->tray_enabled = s->tray_icon && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray));
    // no icon = nothing to get the window back from, so it better not be hidden rn
    if (!s->tray_icon && !gtk_widget_get_visible(s->window))
        show_window(s);
    tray_sync(s);
    store_prefs(s);
}

void on_pref_tray_icon(GtkToggleButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    tray_boxes_sensitive(s);
    if (!s->updating)
        tray_from_boxes(s);
}

void on_pref_tray(GtkToggleButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    tray_boxes_sensitive(s);
    if (!s->updating)
        tray_from_boxes(s);
}

void on_pref_tray_style(GtkComboBox *box, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    s->tray_style = gtk_combo_box_get_active(box);
    tray_sync(s);
    store_prefs(s);
}

void on_pref_start_min(GtkToggleButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->updating)
        store_prefs(s);
}

} // namespace gui
