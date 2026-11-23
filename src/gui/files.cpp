// the PROFILES AND BACKUPS box on the settings page (profile files, backups), the question about unsaved changes, and the
// application settings window behind the button in the header

#include "app.h"

#include <cstdio>
#include <vector>

namespace gui {

namespace {

// a file chooser that fits whatever desktop this runs on. returns the path picked or "" (and frees everything)
std::string pick_file(app_state *s, bool save, const char *title, const char *suggest)
{
    GtkFileChooserNative *fc = gtk_file_chooser_native_new(title, GTK_WINDOW(s->window),
                                                           save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
                                                           save ? "Save" : "Open", "Cancel");
    if (save) {
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), suggest);
    }
    std::string path;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        gchar *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (p)
            path = p;
        g_free(p);
    }
    g_object_unref(fc);
    return path;
}

// "kova-2016" for file names, out of the model name
std::string short_name(app_state *s)
{
    std::string out;
    for (const char *c = s->info.name; c && *c; ++c) {
        char ch = static_cast<char>(g_ascii_tolower(*c));
        if (g_ascii_isalnum(ch))
            out += ch;
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.rfind("roccat-", 0) == 0)
        out.erase(0, 7);
    return out.empty() ? "device" : out;
}

bool ask(app_state *s, const char *question, const char *detail, const char *yes)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(s->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", question);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detail);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL, yes, GTK_RESPONSE_ACCEPT, nullptr);
    bool ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(d);
    return ok;
}

} // namespace

// before the profile being edited gets swapped for another one: changes that are not on the device get asked about.
// false = stay where we are
bool settle_changes(app_state *s)
{
    if (!s->dirty || !s->dev)
        return true;
    char q[96];
    std::snprintf(q, sizeof q, "Profile %d has changes that are not on the %s yet", s->edit_profile + 1, thing(s));
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(s->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", q);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "Save them to the %s first, or throw them away?", thing(s));
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL, "Throw away", GTK_RESPONSE_REJECT, "Save to device",
                           GTK_RESPONSE_ACCEPT, nullptr);
    int answer = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (answer == GTK_RESPONSE_ACCEPT) {
        on_apply(nullptr, s);
        return !s->dirty;
    }
    if (answer == GTK_RESPONSE_REJECT) {
        s->dirty = false;
        return true;
    }
    return false;
}

void on_export_profile(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev || !s->profile)
        return;
    char name[96];
    std::snprintf(name, sizeof name, "%s-profile%d.rcpf", short_name(s).c_str(), s->edit_profile + 1);
    std::string path = pick_file(s, true, "Export profile", name);
    if (path.empty())
        return;
    // what is on the screen, unsaved changes included
    collect(s);
    int n = rockcat_profile_export(s->profile, nullptr, 0);
    std::vector<uint8_t> blob(n > 0 ? static_cast<size_t>(n) : 0);
    if (n > 0)
        rockcat_profile_export(s->profile, blob.data(), blob.size());
    FILE *f = n > 0 ? std::fopen(path.c_str(), "wb") : nullptr;
    bool ok = f && std::fwrite(blob.data(), 1, blob.size(), f) == blob.size();
    if (f)
        ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        report_error(s, "Exporting failed", n < 0 ? n : ROCKCAT_ERR_ACCESS);
        return;
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "Profile %d exported", s->edit_profile + 1);
    set_status(s, msg, "connected");
}

void on_import_profile(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    std::string path = pick_file(s, false, "Import profile", nullptr);
    if (path.empty())
        return;
    gchar *buf = nullptr;
    gsize len = 0;
    if (!g_file_get_contents(path.c_str(), &buf, &len, nullptr)) {
        report_error(s, "Importing failed", ROCKCAT_ERR_ACCESS);
        return;
    }
    rockcat_profile *p = nullptr;
    int r = rockcat_profile_import(s->dev, reinterpret_cast<const uint8_t *>(buf), len, &p);
    g_free(buf);
    if (r == ROCKCAT_OK)
        r = rockcat_profile_set_index(p, s->edit_profile);
    if (r != ROCKCAT_OK) {
        rockcat_profile_free(p);
        report_error(s, r == ROCKCAT_ERR_ARGUMENT ? "Not a profile of this model" : "Importing failed", r);
        return;
    }
    // into the editor, not onto the device yet: SAVE TO DEVICE does that, like with any other change
    rockcat_profile_free(s->profile);
    s->profile = p;
    show_profile(s);
    s->dirty = true;
    update_profile_buttons(s);
    char msg[96];
    std::snprintf(msg, sizeof msg, "Imported into profile %d, not saved to the %s yet", s->edit_profile + 1, thing(s));
    set_status(s, msg, "connected");
}

void on_backup(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    GDateTime *now = g_date_time_new_now_local();
    gchar *day = g_date_time_format(now, "%Y-%m-%d");
    g_date_time_unref(now);
    std::string name = short_name(s) + "-backup-" + day + ".rswb";
    g_free(day);
    std::string path = pick_file(s, true, "Back up all profiles", name.c_str());
    if (path.empty())
        return;
    set_status(s, "Reading every profile...", nullptr);
    while (gtk_events_pending())
        gtk_main_iteration();
    int n = rockcat_backup_save(s->dev, path.c_str());
    if (n < 0) {
        report_error(s, "Backup failed", n);
        return;
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "%d profiles backed up", n);
    set_status(s, msg, "connected");
}

void on_restore(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    std::string path = pick_file(s, false, "Restore a backup", nullptr);
    if (path.empty())
        return;
    char detail[160];
    std::snprintf(detail, sizeof detail, "Every profile on the %s gets replaced by the one in the backup. Changes that are not saved are lost too.", thing(s));
    if (!ask(s, "Restore this backup?", detail, "Restore"))
        return;
    set_status(s, "Writing every profile...", nullptr);
    while (gtk_events_pending())
        gtk_main_iteration();
    int n = rockcat_backup_restore(s->dev, path.c_str(), -1);
    if (n < 0) {
        report_error(s, n == ROCKCAT_ERR_UNSUPPORTED ? "That backup is from another model"
                        : n == ROCKCAT_ERR_PROTOCOL  ? "Not a backup, or it is cut off"
                                                     : "Restoring failed", n);
        return;
    }
    if (s->engine)
        rockcat_engine_reload(s->engine);
    load_profile(s);
    char msg[96];
    std::snprintf(msg, sizeof msg, "%d profiles restored", n);
    set_status(s, msg, "connected");
}

// the box on the settings page: one profile to and from a file, all of them as a backup
GtkWidget *files_panel(app_state *s)
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    struct { const char *text, *tip; GCallback cb; } items[] = {
        {"EXPORT PROFILE", "The profile being edited into a file, changes that are not saved included", G_CALLBACK(on_export_profile)},
        {"IMPORT PROFILE", "A profile file into the profile being edited. Goes onto the device with SAVE TO DEVICE", G_CALLBACK(on_import_profile)},
        {"BACK UP ALL", "Every profile on the device into one file", G_CALLBACK(on_backup)},
        {"RESTORE BACKUP", "Every profile on the device replaced by the ones in a backup", G_CALLBACK(on_restore)},
    };
    for (int i = 0; i < 4; ++i) {
        GtkWidget *b = gtk_button_new_with_label(items[i].text);
        gtk_widget_set_tooltip_text(b, items[i].tip);
        g_signal_connect(b, "clicked", items[i].cb, s);
        gtk_grid_attach(GTK_GRID(grid), b, i % 2, i / 2, 1, 1);
    }
    GtkWidget *hint = label("Profile files end in .rcpf. Backups end in .rswb and are the same files rockcat-cli backup and restore use", "rc-hint");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 2, 2, 1);
    return box_panel("PROFILES AND BACKUPS", grid);
}

// the window behind the gear in the header: tray, start minimized, about. only the program, never a device
void on_app_settings(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    GtkWidget *d = gtk_dialog_new_with_buttons("rockcat-swarm settings", GTK_WINDOW(s->window), GTK_DIALOG_MODAL, "Close",
                                               GTK_RESPONSE_CLOSE, nullptr);
    add_class(d, "rockcat");
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(area), 14);
    gtk_box_set_spacing(GTK_BOX(area), 14);

    GtkWidget *abox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    s->pref_tray_icon = gtk_check_button_new_with_label("Register Tray Icon");
    gtk_widget_set_tooltip_text(s->pref_tray_icon, "Show rockcat-swarm in the system tray while it runs");
    s->pref_tray_style_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(s->pref_tray_style_row, 24);
    s->pref_tray_style = gtk_combo_box_text_new();
    for (const char *name : {"Colored", "White", "Black"})
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->pref_tray_style), name);
    gtk_widget_set_tooltip_text(s->pref_tray_style, "White or black fit a panel that only has plain icons");
    gtk_box_pack_start(GTK_BOX(s->pref_tray_style_row), label("Tray icon"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s->pref_tray_style_row), s->pref_tray_style, FALSE, FALSE, 0);
    s->pref_tray = gtk_check_button_new_with_label("Keep running in the system tray");
    gtk_widget_set_margin_start(s->pref_tray, 24);
    gtk_widget_set_tooltip_text(s->pref_tray, "Closing the window hides it to the tray, the lighting engine keeps running");
    s->pref_start_min = gtk_check_button_new_with_label("Start minimized to the tray");
    gtk_widget_set_margin_start(s->pref_start_min, 48);
    gtk_widget_set_tooltip_text(s->pref_start_min, "Start without opening the window (same as --minimized)");
    gtk_box_pack_start(GTK_BOX(abox), s->pref_tray_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(abox), s->pref_tray_style_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(abox), s->pref_tray, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(abox), s->pref_start_min, FALSE, FALSE, 0);
#ifndef ROCKCAT_HAVE_TRAY
    gtk_widget_set_sensitive(abox, FALSE);
    gtk_box_pack_start(GTK_BOX(abox), label("Built without system tray support (libayatana-appindicator3)", "rc-hint"), FALSE, FALSE, 0);
#endif
    gtk_box_pack_start(GTK_BOX(area), box_panel("APPLICATION", abox), FALSE, FALSE, 0);

    GtkWidget *about = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    char version[64];
    std::snprintf(version, sizeof version, "rockcat-swarm %s", rockcat_version());
    gtk_box_pack_start(GTK_BOX(about), label(version), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(about), label("open replacement for ROCCAT Swarm, GPL-3.0. forked from rossat-swarm by TheElevatedOne", "rc-hint"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(about), label("not affiliated with ROCCAT or Turtle Beach in any way", "rc-hint"), FALSE, FALSE, 0);
#ifdef ROCKCAT_ROCCAT_ASSETS
    // goes away together with the pictures when somebody builds with -DROCKCAT_ROCCAT_ASSETS=OFF
    gtk_box_pack_start(GTK_BOX(about), label("the device pictures are ROCCATs own and still belong to ROCCAT / Turtle Beach", "rc-hint"),
                       FALSE, FALSE, 0);
#endif
    gtk_box_pack_start(GTK_BOX(area), box_panel("ABOUT", about), FALSE, FALSE, 0);

    gui_prefs prefs = load_prefs();
    bool busy = s->updating;
    s->updating = true;
    g_signal_connect(s->pref_tray_icon, "toggled", G_CALLBACK(on_pref_tray_icon), s);
    g_signal_connect(s->pref_tray, "toggled", G_CALLBACK(on_pref_tray), s);
    g_signal_connect(s->pref_tray_style, "changed", G_CALLBACK(on_pref_tray_style), s);
    g_signal_connect(s->pref_start_min, "toggled", G_CALLBACK(on_pref_start_min), s);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->pref_tray_icon), prefs.tray_icon);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->pref_tray), prefs.tray);
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->pref_tray_style), prefs.tray_style);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->pref_start_min), prefs.start_minimized);
    gtk_widget_set_sensitive(s->pref_tray, prefs.tray_icon);
    gtk_widget_set_sensitive(s->pref_tray_style_row, prefs.tray_icon);
    gtk_widget_set_sensitive(s->pref_start_min, prefs.tray_icon && prefs.tray);
    s->updating = busy;

    gtk_widget_show_all(d);
    gtk_dialog_run(GTK_DIALOG(d));
    s->pref_tray_icon = s->pref_tray = s->pref_start_min = s->pref_tray_style = s->pref_tray_style_row = nullptr;
    gtk_widget_destroy(d);
}

} // namespace gui
