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
    if (!s->tray_enabled) {
        if (s->indicator)
            app_indicator_set_status(s->indicator, APP_INDICATOR_STATUS_PASSIVE);
        return;
    }
    if (!s->indicator) {
        // The tray host loads icons by name from a directory, so put the
        // bundled SVG where it can find it (works without installation too).
        std::string icon_dir = std::string(g_get_user_cache_dir()) + "/rockcat/icons";
        g_mkdir_with_parents(icon_dir.c_str(), 0755);
        if (GBytes *svg = g_resources_lookup_data("/org/rockcat/swarm/rockcat-swarm.svg", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr)) {
            gsize len = 0;
            const char *data = static_cast<const char *>(g_bytes_get_data(svg, &len));
            g_file_set_contents((icon_dir + "/rockcat-swarm-tray.svg").c_str(), data, static_cast<gssize>(len), nullptr);
            g_bytes_unref(svg);
        }
        s->indicator = app_indicator_new_with_path("rockcat-swarm", "rockcat-swarm-tray", APP_INDICATOR_CATEGORY_HARDWARE,
                                                   icon_dir.c_str());
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
    if (s->pref_tray)
        p.tray = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray));
    if (s->pref_start_min)
        p.start_minimized = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_start_min));
    p.auto_apply = gtk_switch_get_active(GTK_SWITCH(s->auto_apply));
    save_prefs(p);
}

void on_pref_tray(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    bool on = gtk_toggle_button_get_active(b);
    gtk_widget_set_sensitive(s->pref_start_min, on);
    if (s->updating)
        return;
    s->tray_enabled = on;
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
