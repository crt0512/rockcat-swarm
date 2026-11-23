// little helpers every page uses: labels, panels, the status line, colour conversion

#include "app.h"

namespace gui {

GtkWidget *label(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls)
        gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

void add_class(GtkWidget *w, const char *cls)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

// the title of a panel, for lining panels up (the first thing in it)
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
    gtk_box_pack_start(GTK_BOX(outer), label(title, "rc-box-title"), FALSE, FALSE, 0);
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

} // namespace gui
