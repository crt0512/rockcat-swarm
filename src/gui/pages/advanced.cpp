// the Advanced settings page: debounce, angle snapping, left handed and sensor calibration where the device has them,
// and the raw profile for everybody

#include "../app.h"

namespace gui {

void update_tdcu_label(app_state *s)
{
    if (!s->dev || !s->tdcu_label)
        return;
    rockcat_tdcu t;
    char msg[128];
    if (rockcat_read_tdcu(s->dev, &t) == ROCKCAT_OK)
        std::snprintf(msg, sizeof msg, t.mode == 2 ? "Calibrated for your surface (value 0x%04x)"
                                                   : "Factory calibration (value 0x%04x)", t.value);
    else
        std::snprintf(msg, sizeof msg, "Could not read the sensor calibration");
    gtk_label_set_text(GTK_LABEL(s->tdcu_label), msg);
}

struct calib_state {
    app_state *app;
    GtkWidget *dialog, *text, *progress, *start, *save, *discard;
    guint timer = 0;
    int ticks = 0;
    bool running = false;
};

void calib_set_text(calib_state *c, const char *t)
{
    gtk_label_set_text(GTK_LABEL(c->text), t);
}

gboolean calib_tick(gpointer data)
{
    auto *c = static_cast<calib_state *>(data);
    ++c->ticks;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(c->progress));
    if (c->ticks < 10) // give the sensor a moment to enter calibration
        return G_SOURCE_CONTINUE;
    int finished = 0;
    int r = rockcat_calibration_poll(c->app->dev, &finished);
    if (r != ROCKCAT_OK || c->ticks > 600) {
        c->timer = 0;
        c->running = false;
        rockcat_calibration_finish(c->app->dev, 0);
        calib_set_text(c, r != ROCKCAT_OK ? "Calibration failed. Try again on a clean, even surface."
                                         : "Calibration timed out and was discarded.");
        gtk_widget_set_sensitive(c->start, TRUE);
        return G_SOURCE_REMOVE;
    }
    if (!finished)
        return G_SOURCE_CONTINUE;
    c->timer = 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->progress), 1.0);
    calib_set_text(c, "Calibration complete. Save it to the mouse, or discard it to keep the previous calibration.");
    gtk_widget_set_sensitive(c->save, TRUE);
    gtk_widget_set_sensitive(c->discard, TRUE);
    return G_SOURCE_REMOVE;
}

void on_calib_start(GtkButton *, gpointer data)
{
    auto *c = static_cast<calib_state *>(data);
    int r = rockcat_calibration_start(c->app->dev);
    if (r != ROCKCAT_OK) {
        calib_set_text(c, rockcat_strerror(r));
        return;
    }
    c->running = true;
    c->ticks = 0;
    gtk_widget_set_sensitive(c->start, FALSE);
    calib_set_text(c, "Calibrating\u2026 keep moving the mouse in large circles and figure-eights.");
    c->timer = g_timeout_add(100, calib_tick, c);
}

void calib_finish(calib_state *c, bool save)
{
    int r = rockcat_calibration_finish(c->app->dev, save ? 1 : 0);
    c->running = false;
    if (r != ROCKCAT_OK)
        report_error(c->app, "Calibration", r);
    else
        set_status(c->app, save ? "Sensor calibration saved" : "Sensor calibration discarded", "connected");
    gtk_widget_destroy(c->dialog);
}

void on_calib_save(GtkButton *, gpointer data) { calib_finish(static_cast<calib_state *>(data), true); }
void on_calib_discard(GtkButton *, gpointer data) { calib_finish(static_cast<calib_state *>(data), false); }

void on_calib_destroy(GtkWidget *, gpointer data)
{
    auto *c = static_cast<calib_state *>(data);
    if (c->timer)
        g_source_remove(c->timer);
    if (c->running && c->app->dev) // closed mid-way: keep the old calibration
        rockcat_calibration_finish(c->app->dev, 0);
    update_tdcu_label(c->app);
    delete c;
}

void on_calibrate(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    auto *c = new calib_state;
    c->app = s;
    c->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(c->dialog), "Sensor calibration");
    gtk_window_set_transient_for(GTK_WINDOW(c->dialog), GTK_WINDOW(s->window));
    gtk_window_set_modal(GTK_WINDOW(c->dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(c->dialog), 460, -1);
    add_class(c->dialog, "rockcat");
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(c->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 16);
    gtk_box_set_spacing(GTK_BOX(area), 12);
    gtk_box_pack_start(GTK_BOX(area), label("TRACKING & DISTANCE CONTROL UNIT", "rc-box-title"), FALSE, FALSE, 0);
    c->text = label("Put the mouse on the surface you play on. Press START, then keep moving the mouse "
                    "in large circles and figure-eights until calibration completes (about 10-20 s).");
    gtk_label_set_line_wrap(GTK_LABEL(c->text), TRUE);
    gtk_box_pack_start(GTK_BOX(area), c->text, FALSE, FALSE, 0);
    c->progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(area), c->progress, FALSE, FALSE, 0);
    GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    c->start = gtk_button_new_with_label("START");
    add_class(c->start, "suggested-action");
    c->save = gtk_button_new_with_label("SAVE");
    c->discard = gtk_button_new_with_label("DISCARD");
    gtk_widget_set_sensitive(c->save, FALSE);
    gtk_widget_set_sensitive(c->discard, FALSE);
    gtk_box_pack_start(GTK_BOX(bb), c->start, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bb), c->save, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bb), c->discard, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), bb, FALSE, FALSE, 0);
    g_signal_connect(c->start, "clicked", G_CALLBACK(on_calib_start), c);
    g_signal_connect(c->save, "clicked", G_CALLBACK(on_calib_save), c);
    g_signal_connect(c->discard, "clicked", G_CALLBACK(on_calib_discard), c);
    g_signal_connect(c->dialog, "destroy", G_CALLBACK(on_calib_destroy), c);
    gtk_widget_show_all(c->dialog);
}

GtkWidget *build_advanced_page(app_state *s)
{
    const rockcat_caps *caps = s->caps;
    GtkWidget *page = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(page), 14);
    gtk_grid_set_row_spacing(GTK_GRID(page), 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);
    // small boxes two in a row, in the order they come. the wide ones go underneath
    gtk_grid_set_column_homogeneous(GTK_GRID(page), TRUE);
    int small = 0;
    GtkWidget *last = nullptr;
    auto place = [&](GtkWidget *panel) {
        gtk_widget_set_hexpand(panel, TRUE);
        gtk_grid_attach(GTK_GRID(page), panel, small % 2, small / 2, 1, 1);
        last = panel;
        ++small;
    };

    if (has_setting(s, ROCKCAT_SET_ANGLE_SNAPPING)) {
        GtkWidget *ab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        s->angle_snapping = gtk_switch_new();
        gtk_box_pack_start(GTK_BOX(ab), s->angle_snapping, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ab), label("Straighten nearly horizontal / vertical movements", "rc-hint"), FALSE, FALSE, 0);
        place(box_panel("ANGLE SNAPPING", ab));
    }
    if (has_setting(s, ROCKCAT_SET_DEBOUNCE_MS)) {
        GtkWidget *db = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        s->debounce = gtk_spin_button_new_with_range(0, 30, 1);
        gtk_box_pack_start(GTK_BOX(db), s->debounce, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(db), label("ms  (factory: 8)", "rc-hint"), FALSE, FALSE, 0);
        place(box_panel("DEBOUNCE TIME", db));
    }
    if (has_setting(s, ROCKCAT_SET_LEFT_HANDED)) {
        GtkWidget *lb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        s->left_handed = gtk_switch_new();
        gtk_box_pack_start(GTK_BOX(lb), s->left_handed, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(lb), label("Swap what the mouse takes for its left and right side", "rc-hint"), FALSE, FALSE, 0);
        place(box_panel("LEFT HANDED", lb));
    }
    // one box alone in its row takes the whole row instead of leaving half of it empty
    if (small % 2 == 1 && last)
        gtk_container_child_set(GTK_CONTAINER(page), last, "width", 2, nullptr);
    int row = (small + 1) / 2;

    s->raw_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(s->raw_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(s->raw_view), TRUE);
    gtk_widget_set_hexpand(s->raw_view, TRUE);
    // on the Kone this always was the settings report with the buttons behind it and the title said only half of that
    gtk_grid_attach(GTK_GRID(page), box_panel(is_kone(s) ? "RAW SETTINGS REPORT 0x06 (as last read)" : "RAW PROFILE (as last read)", s->raw_view), 0, row++, 2, 1);

    if (caps->features & ROCKCAT_HAS_CALIBRATION) {
        GtkWidget *cb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        s->calib_button = gtk_button_new_with_label("CALIBRATE SENSOR…");
        g_signal_connect(s->calib_button, "clicked", G_CALLBACK(on_calibrate), s);
        s->tdcu_label = label("", "rc-hint");
        gtk_box_pack_start(GTK_BOX(cb), s->calib_button, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(cb), s->tdcu_label, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(page), box_panel("SENSOR CALIBRATION (TDCU)", cb), 0, row++, 2, 1);
    }
    return page;
}

} // namespace gui
