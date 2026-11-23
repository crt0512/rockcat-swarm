// little helpers every page uses: labels, panels, the status line, colour conversion

#include "app.h"

#include <string>

namespace gui {

GtkWidget *label(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls)
        gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

bool own_button_types(const rockcat_caps *caps)
{
    if (!caps)
        return false;
    for (int t = 0; t < 256; ++t) {
        const char *shared = rockcat_button_type_name(static_cast<uint8_t>(t));
        if (std::strcmp(rockcat_assignment_name(caps, static_cast<uint8_t>(t)), shared ? shared : "?") != 0)
            return true;
    }
    return false;
}

void center_children(GtkWidget *box)
{
    GList *kids = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *k = kids; k; k = k->next)
        gtk_widget_set_valign(GTK_WIDGET(k->data), GTK_ALIGN_CENTER);
    g_list_free(kids);
}

void add_class(GtkWidget *w, const char *cls)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

// the title of a panel, for lining panels up (the first thing in it)
// the spacing between the letters goes in as markup and not as css: gtk3 measures a label with css letter-spacing wrong
// at 2x scale (a third too tall) until something restyles it, so the same title came out at another height on every page
GtkWidget *title_label(const char *text)
{
    GtkWidget *l = label("", "rc-box-title");
    gchar *esc = g_markup_escape_text(text, -1);
    // and the spaces dont break: gtk3 still measured "KEY COLOURS" as two lines high at a small width (single line mode
    // or not) and that panel came out a line taller than "KEYS" next door
    std::string spaced;
    for (const char *c = esc; *c; ++c)
        spaced += *c == ' ' ? std::string("&#160;") : std::string(1, *c);
    gchar *markup = g_strdup_printf("<span letter_spacing=\"1536\">%s</span>", spaced.c_str());
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    g_free(esc);
    return l;
}

GtkWidget *panel_title(GtkWidget *panel)
{
    GList *kids = gtk_container_get_children(GTK_CONTAINER(panel));
    GtkWidget *first = kids ? GTK_WIDGET(kids->data) : nullptr;
    g_list_free(kids);
    return first;
}

GtkWidget *box_panel(const char *title, GtkWidget *content)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    add_class(outer, "rc-box");
    gtk_box_pack_start(GTK_BOX(outer), title_label(title), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), content, TRUE, TRUE, 0);
    return outer;
}

void set_status(app_state *s, const char *text, const char *cls)
{
    gtk_label_set_text(GTK_LABEL(s->status_label), text);
    GtkStyleContext *ctx = gtk_widget_get_style_context(s->status_label);
    gtk_style_context_remove_class(ctx, "connected");
    gtk_style_context_remove_class(ctx, "error");
    if (cls)
        gtk_style_context_add_class(ctx, cls);
}

void report_error(app_state *s, const char *what, int err)
{
    char msg[256];
    std::snprintf(msg, sizeof msg, "%s: %s", what, rockcat_strerror(err));
    set_status(s, msg, "error");
}

GdkRGBA to_rgba(const rockcat_color &c)
{
    return GdkRGBA{c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0};
}

rockcat_color from_rgba(const GdkRGBA &c)
{
    auto cv = [](double v) { return static_cast<uint8_t>(std::lround(std::fmin(std::fmax(v, 0.0), 1.0) * 255)); };
    return rockcat_color{cv(c.alpha), cv(c.red), cv(c.green), cv(c.blue)};
}

void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

} // namespace gui
