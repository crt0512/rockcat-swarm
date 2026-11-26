// rockcat-swarm - GTK3 front end.

#include <cstdio>
#include "window.h"

#include "rockcat/rockcat.h"

#include <cstring>

namespace {

const char *g_start_page = nullptr;
gint g_start_profile = 0;
// and choose an effect on the illumination page before the picture (a rockcat_effect number), -1 = leave it
gint g_pick_effect = -1;
// and the window size for it, "1600x1000"
gchar *g_shot_size = nullptr;
// and pick a key on the key assignment page (its number)
gint g_pick_key = -1;
// --debug: the bug menu with the pretend devices
gboolean g_debug = FALSE;
// --plug kova-2016,ryos-mk-fx: pretend devices plugged in from the start
gchar *g_plug = nullptr;
gchar *g_start_device = nullptr;
// for looking at the gui from a script (tools/dev/gui-shot.sh): draw the window into a png after a moment and quit
gchar *g_shot_file = nullptr;
gint g_shot_delay = 1500;
// and press buttons by what is written on them before that, "PREVIEW" or "APPLY,RELOAD". only looks at visible buttons
gchar *g_press = nullptr;

void press_in(GtkWidget *w, const char *text, bool *done)
{
    if (*done || !gtk_widget_get_mapped(w))
        return;
    // (a toggle button gets switched on, the way a click on one that is up would)
    if (GTK_IS_BUTTON(w)) {
        const char *label = gtk_button_get_label(GTK_BUTTON(w));
        // (a stack switcher puts its own label into the button instead of setting the label of the button)
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(w));
        if (!label && child && GTK_IS_LABEL(child))
            label = gtk_label_get_text(GTK_LABEL(child));
        if (label && std::strcmp(label, text) == 0) {
            if (GTK_IS_TOGGLE_BUTTON(w))
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
            else
                gtk_button_clicked(GTK_BUTTON(w));
            *done = true;
            return;
        }
    }
    if (GTK_IS_CONTAINER(w)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *c = children; c; c = c->next)
            press_in(GTK_WIDGET(c->data), text, done);
        g_list_free(children);
    }
}

gboolean press_buttons(gpointer data)
{
    gchar **names = g_strsplit(g_press, ",", -1);
    for (gchar **n = names; *n; ++n) {
        bool done = false;
        press_in(GTK_WIDGET(data), *n, &done);
        if (!done)
            g_printerr("--press: no visible button says '%s'\n", *n);
    }
    g_strfreev(names);
    return G_SOURCE_REMOVE;
}

gboolean take_shot(gpointer data)
{
    GtkWidget *win = GTK_WIDGET(data);
    int w = gtk_widget_get_allocated_width(win), h = gtk_widget_get_allocated_height(win);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    gtk_widget_draw(win, cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, g_shot_file);
    cairo_surface_destroy(surface);
    g_application_quit(g_application_get_default());
    return G_SOURCE_REMOVE;
}
gboolean g_minimized = FALSE;

void on_activate(GtkApplication *app, gpointer)
{
    GList *windows = gtk_application_get_windows(app);
    if (windows) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/org/rockcat/swarm/rockcat.css");
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    if (g_plug) {
        gchar **names = g_strsplit(g_plug, ",", -1);
        for (gchar **n = names; *n; ++n)
            if (**n && rockcat_sim_plug(*n, 1) != ROCKCAT_OK)
                g_printerr("--plug: no pretend device called '%s'\n", *n);
        g_strfreev(names);
    }
    GtkWidget *win = rockcat_window_new(app, g_minimized);
    rockcat_window_set_debug(win, g_debug);
    int sw = 0, sh = 0;
    if (g_shot_size && std::sscanf(g_shot_size, "%dx%d", &sw, &sh) == 2)
        gtk_window_resize(GTK_WINDOW(win), sw, sh);
    if (g_start_device)
        rockcat_window_show_device(win, g_start_device);
    if (g_start_page)
        rockcat_window_show_page(win, g_start_page);
    if (g_start_profile > 0)
        rockcat_window_edit_profile(win, g_start_profile - 1);
    if (g_pick_effect >= 0)
        g_timeout_add(400, [](gpointer w) -> gboolean {
            rockcat_window_pick_effect(GTK_WIDGET(w), g_pick_effect);
            return G_SOURCE_REMOVE;
        }, win);
    if (g_pick_key >= 0)
        g_timeout_add(500, [](gpointer w) -> gboolean {
            rockcat_window_pick_key(GTK_WIDGET(w), g_pick_key);
            return G_SOURCE_REMOVE;
        }, win);
    if (g_press)
        g_timeout_add(700, press_buttons, win);
    if (g_shot_file)
        g_timeout_add(static_cast<guint>(g_shot_delay), take_shot, win);
}

} // namespace

int main(int argc, char **argv)
{
    // libayatana-appindicator warns on every start that it is deprecated
    g_log_set_handler("libayatana-appindicator", G_LOG_LEVEL_WARNING,
                      [](const gchar *domain, GLogLevelFlags level, const gchar *msg, gpointer) {
                          if (!std::strstr(msg, "deprecated"))
                              g_log_default_handler(domain, level, msg, nullptr);
                      }, nullptr);
    // normally a second start just brings the window of the first one to the front. taking a picture must not do that
    // to whoever has the window open, so --shot runs as its own instance
    bool shot = false;
    for (int i = 1; i < argc; ++i)
        shot = shot || std::strncmp(argv[i], "--shot", 6) == 0;
    GtkApplication *app = gtk_application_new("org.rockcat.Swarm", shot ? G_APPLICATION_NON_UNIQUE : G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    static gchar *page = nullptr;
    const GOptionEntry entries[] = {
        {"page", 0, 0, G_OPTION_ARG_STRING, &page,
         "Page to open: settings, buttons, illumination or advanced", "PAGE"},
        {"device", 0, 0, G_OPTION_ARG_STRING, &g_start_device,
         "Device to show when several are plugged in: a product id like 2fda or a /dev/hidraw path", "WHICH"},
        {"profile", 0, 0, G_OPTION_ARG_INT, &g_start_profile,
         "Profile to open for editing (1-5) instead of the active one", "N"},
        {"shot", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &g_shot_file, "Write a picture of the window to FILE and quit", "FILE"},
        {"plug", 0, 0, G_OPTION_ARG_STRING, &g_plug, "Plug in these pretend devices at the start (names as the debug menu shows them)", "NAME,.."},
        {"debug", 0, 0, G_OPTION_ARG_NONE, &g_debug, "Show the debug menu: pretend devices to try the pages with", nullptr},
        {"pick-key", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_INT, &g_pick_key, "Pick this key on the key assignment page first", "N"},
        {"shot-size", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &g_shot_size, "Window size for --shot", "WxH"},
        {"pick-effect", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_INT, &g_pick_effect, "Choose this effect on the illumination page first", "N"},
        {"press", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &g_press, "Press the buttons with these labels first", "A,B"},
        {"shot-delay", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_INT, &g_shot_delay, "Milliseconds to wait before --shot", "MS"},
        {"minimized", 'm', 0, G_OPTION_ARG_NONE, &g_minimized,
         "Start hidden in the system tray (enables the tray for this run)", nullptr},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}};
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(+[](GApplication *, GVariantDict *, gpointer) -> gint {
        g_start_page = page;
        return -1;
    }), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    rockcat_exit();
    return status;
}
