// the Button assignment page: every button of a mouse, once per layer. keyboards get their keys with the keyboard view

#include "../app.h"

namespace gui {

void on_button_combo_changed(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "button"));
    update_button_row_sensitivity(s, idx);
}

// the button somebody is looking at, whether they point at its marker on the picture or at its row in the list:
// both light up together. -1 = none
void set_highlight(app_state *s, int input)
{
    if (input == s->highlighted_button)
        return;
    const int inputs = n_inputs(s);
    for (int which : {s->highlighted_button, input}) {
        if (which < 0 || which >= inputs)
            continue;
        std::vector<GtkWidget *> row;
        if (static_cast<size_t>(which) < s->button_names.size())
            row.push_back(s->button_names[static_cast<size_t>(which)]);
        for (int layer = 0; layer < n_layers(s); ++layer)
            if (static_cast<size_t>(which + layer * inputs) < s->buttons.size())
                row.push_back(s->buttons[static_cast<size_t>(which + layer * inputs)].combo);
        for (GtkWidget *w : row) {
            if (!w)
                continue;
            if (which == input)
                gtk_style_context_add_class(gtk_widget_get_style_context(w), "rc-row-on");
            else
                gtk_style_context_remove_class(gtk_widget_get_style_context(w), "rc-row-on");
        }
    }
    s->highlighted_button = input;
    if (s->mouse_buttons)
        gtk_widget_queue_draw(s->mouse_buttons);
}

gboolean on_button_row_focus(GtkWidget *w, GdkEvent *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (n_inputs(s) > 0)
        set_highlight(s, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "button")) % n_inputs(s));
    return FALSE;
}

GtkWidget *button_combo(app_state *s, int idx)
{
    GtkWidget *combo = gtk_combo_box_text_new();
    if (own_button_types(s->caps)) {
        // a mouse with a list of its own (the Lua): only what it knows, under its names
        for (int t = 0; t < 256; ++t) {
            const char *name = rockcat_assignment_name(s->caps, static_cast<uint8_t>(t));
            if (std::strcmp(name, "?") == 0)
                continue;
            char id[8];
            std::snprintf(id, sizeof id, "%d", t);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, name);
        }
    } else {
        for (int i = 0; i < rockcat_button_type_count(); ++i) {
            int t = rockcat_button_type_at(i);
            char id[8];
            std::snprintf(id, sizeof id, "%d", t);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, rockcat_button_type_name(static_cast<uint8_t>(t)));
        }
    }
    // long names get cut short in a narrow window instead of making the window wide
    GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(combo));
    for (GList *c = cells; c; c = c->next)
        g_object_set(c->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 12, nullptr);
    g_list_free(cells);
    g_object_set_data(G_OBJECT(combo), "button", GINT_TO_POINTER(idx));
    g_signal_connect(combo, "changed", G_CALLBACK(on_button_combo_changed), s);
    g_signal_connect(combo, "focus-in-event", G_CALLBACK(on_button_row_focus), s);
    g_signal_connect(combo, "enter-notify-event", G_CALLBACK(on_button_row_focus), s);
    gtk_widget_add_events(combo, GDK_ENTER_NOTIFY_MASK);
    return combo;
}

void on_reset_buttons(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->profile || rockcat_profile_reset_inputs(s->profile) != ROCKCAT_OK)
        return;
    s->updating = true;
    for (int i = 0; i < static_cast<int>(s->buttons.size()); ++i) {
        rockcat_assignment b{};
        rockcat_profile_get_input(s->profile, i / n_inputs(s), i % n_inputs(s), &b);
        ensure_combo_id(s->buttons[i].combo, b.type);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p1), 0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p2), 0);
        update_button_row_sensitivity(s, i);
    }
    s->updating = false;
    set_status(s, "Factory layout loaded, press APPLY to write it", nullptr);
    schedule_auto_apply(s);
}

GtkWidget *build_buttons_page(app_state *s)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);

    const int inputs = n_inputs(s), layers = n_layers(s);
    s->buttons.assign(static_cast<size_t>(inputs * layers), button_row{});
    s->button_names.assign(static_cast<size_t>(inputs), nullptr);
    // every other mouse gets ROCCATs picture of it with a marker on every button, if the build has one
    if (s->view) {
        s->mouse_buttons = gtk_drawing_area_new();
        int w = 0, h = 0;
        // one size, the same on the button and the lighting page, so the mouse stays put when switching between them
        view_size(s->view, MOUSE_VIEW_HEIGHT, &w, &h);
        gtk_widget_set_size_request(s->mouse_buttons, w, h);
        gtk_widget_set_hexpand(s->mouse_buttons, TRUE);
        gtk_widget_set_vexpand(s->mouse_buttons, TRUE);
        gtk_widget_add_events(s->mouse_buttons, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK);
        g_signal_connect(s->mouse_buttons, "draw", G_CALLBACK(on_draw_view_buttons), s);
        g_signal_connect(s->mouse_buttons, "motion-notify-event", G_CALLBACK(on_view_motion), s);
        g_signal_connect(s->mouse_buttons, "button-press-event", G_CALLBACK(on_view_click), s);
        gtk_widget_add_events(s->mouse_buttons, GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(s->mouse_buttons, "leave-notify-event", G_CALLBACK(on_view_leave), s);
        gtk_box_pack_start(GTK_BOX(page), s->mouse_buttons, TRUE, TRUE, 0);
    }
    // the list goes in the order of the numbers on the markers, which is the order Swarm had. without a view its our own order
    std::vector<int> order(static_cast<size_t>(inputs));
    for (int i = 0; i < inputs; ++i)
        order[static_cast<size_t>(i)] = i;
    auto number = [s](int input) { int n = view_button_number(s->view, input); return n ? n : input + 1; };
    if (s->view)
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return number(a) < number(b); });

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_attach(GTK_GRID(grid), label("BUTTON", "rc-hint"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label("STANDARD FUNCTION", "rc-hint"), 1, 0, 1, 1);
    if (layers > 1)
        gtk_grid_attach(GTK_GRID(grid), label("EASY-SHIFT[+] FUNCTION", "rc-hint"), 2, 0, 1, 1);

    for (int line = 0; line < inputs; ++line) {
        const int i = order[static_cast<size_t>(line)];
        char t[64];
        std::snprintf(t, sizeof t, "%d  %s", number(i), rockcat_input_label(s->caps, i));
        GtkWidget *name = label(t);
        add_class(name, "rc-row-name");
        s->button_names[static_cast<size_t>(i)] = name;
        gtk_grid_attach(GTK_GRID(grid), name, 0, line + 1, 1, 1);
        for (int layer = 0; layer < layers; ++layer) {
            int idx = i + layer * inputs;
            button_row &r = s->buttons[static_cast<size_t>(idx)];
            r.combo = button_combo(s, idx);
            r.p1 = gtk_spin_button_new_with_range(0, 255, 1);
            r.p2 = gtk_spin_button_new_with_range(0, 255, 1);
            // raw parameter holders; shortcuts are edited as text
            gtk_widget_set_no_show_all(r.p1, TRUE);
            gtk_widget_set_no_show_all(r.p2, TRUE);
            r.text = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(r.text), "macro text (optional)");
            gtk_entry_set_width_chars(GTK_ENTRY(r.text), 14);
            GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_box_pack_start(GTK_BOX(cell), r.combo, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(cell), r.p1, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(cell), r.p2, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(cell), r.text, FALSE, FALSE, 0);
            gtk_widget_set_no_show_all(r.p1, TRUE);
            gtk_widget_set_no_show_all(r.p2, TRUE);
            gtk_widget_set_no_show_all(r.text, TRUE);
            gtk_grid_attach(GTK_GRID(grid), cell, 1 + layer, line + 1, 1, 1);
        }
    }
    GtkWidget *reset = gtk_button_new_with_label("RESTORE FACTORY LAYOUT");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_buttons), s);
    GtkWidget *hint = label("Shortcut: type a key combination such as ctrl+c or alt+f4. "
                            "Macro: type text to store it as a keystroke macro on APPLY. "
                            "The Easy-Shift[+] column is active while the thumb Easy-Shift button is held.", "rc-hint");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 70);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, inputs + 1, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), reset, 0, inputs + 2, 1, 1);

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), grid);
    // a row that gets the focus (a click on its marker does that) scrolls itself into sight
    gtk_container_set_focus_vadjustment(GTK_CONTAINER(grid), gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll)));
    // the table keeps its own width, a bigger window gives the room to the picture. without a picture it may spread
    const bool picture = s->mouse_buttons != nullptr;
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), picture);
    GtkWidget *panel = box_panel("BUTTON ASSIGNMENT", scroll);
    // next to the picture the table has the width every mouse page has on that side
    if (picture) {
        gtk_widget_set_size_request(panel, MOUSE_SIDE_WIDTH, -1);
        gtk_widget_set_hexpand(panel, FALSE);
        gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
    }
    gtk_box_pack_start(GTK_BOX(page), panel, !picture, TRUE, 0);
    return page;
}

} // namespace gui
