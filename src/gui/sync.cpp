// profile <-> widgets: collect() reads the widgets into the profile, fill() does the opposite

#include "app.h"

namespace gui {

int combo_type(GtkWidget *combo)
{
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
    return id ? static_cast<int>(std::strtol(id, nullptr, 0)) : -1;
}

// every block in here asks "is that control there" first: the pages only grow what the device has
void collect(app_state *s)
{
    rockcat_profile *p = s->profile;
    if (!p)
        return;
    if (!s->dpi.empty()) {
        int active = 0;
        for (size_t i = 0; i < s->dpi.size(); ++i)
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->dpi[i].active)))
                active = static_cast<int>(i);
        rockcat_profile_set_int(p, ROCKCAT_SET_DPI_ACTIVE, active);
        for (size_t i = 0; i < s->dpi.size(); ++i) {
            int x = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->dpi[i].x));
            // a mouse with one value per level has no y box, x counts for both
            int y = s->dpi[i].y ? gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->dpi[i].y)) : x;
            // the active level counts as enabled no matter what its checkbox says
            rockcat_profile_set_dpi(p, static_cast<int>(i), x, y,
                                    static_cast<int>(i) == active || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->dpi[i].enabled)));
        }
    }
    for (GtkWidget *b : s->polling)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b)))
            rockcat_profile_set_int(p, ROCKCAT_SET_POLLING_HZ, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "hz")));

    if (s->effect) {
        int eff = combo_type(s->effect);
        // an effect the list doesnt offer (a keyboard profile set to one of the pc drawn ones) stays what it is
        if (eff >= 0 && s->caps && ((s->caps->effects >> eff) & 1))
            rockcat_profile_select_effect(p, eff, s->render_engine && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->render_engine)));
    }
    if (s->shift_enable) {
        rockcat_shift_lighting sl;
        sl.enabled = gtk_switch_get_active(GTK_SWITCH(s->shift_enable));
        sl.style = static_cast<uint8_t>(gtk_combo_box_get_active(GTK_COMBO_BOX(s->shift_style)) == 1 ? ROCKCAT_SHIFT_PULSE
                                                                                                     : ROCKCAT_SHIFT_SOLID);
        GdkRGBA srgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->shift_color), &srgba);
        sl.color = from_rgba(srgba);
        rockcat_shift_lighting_save(s->info.product_id, static_cast<uint8_t>(s->edit_profile), &sl);
    }
    if (s->speed)
        rockcat_profile_set_int(p, ROCKCAT_SET_EFFECT_SPEED, static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->speed))));
    if (s->theme) {
        int th = combo_type(s->theme);
        if (th >= 0)
            rockcat_profile_set_int(p, ROCKCAT_SET_COLOR_THEME, th);
    }
    if (s->brightness)
        rockcat_profile_set_int(p, ROCKCAT_SET_BRIGHTNESS, static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->brightness))));
    if (s->color_flow)
        rockcat_profile_set_int(p, ROCKCAT_SET_COLOR_FLOW, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->color_flow)));
    for (size_t i = 0; i < s->led_color.size(); ++i) {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        rockcat_profile_set_led(p, static_cast<int>(i), from_rgba(rgba), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->led_enable[i])));
    }
    // (key colours of a keyboard are not collected: picking a colour on the keyboard view writes them into the profile right there)
    if (s->idle_minutes) {
        rockcat_profile_set_int(p, ROCKCAT_SET_IDLE_MINUTES, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->idle_minutes)));
        int style = combo_type(s->idle_style);
        if (style >= 0)
            rockcat_profile_set_int(p, ROCKCAT_SET_IDLE_STYLE, style);
        rockcat_profile_set_int(p, ROCKCAT_SET_IDLE_BRIGHTNESS, static_cast<int>(gtk_range_get_value(GTK_RANGE(s->idle_brightness))));
    }
    if (s->macro_feedback)
        rockcat_profile_set_int(p, ROCKCAT_SET_MACRO_FEEDBACK, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->macro_feedback)));

    if (s->angle_snapping)
        rockcat_profile_set_int(p, ROCKCAT_SET_ANGLE_SNAPPING, gtk_switch_get_active(GTK_SWITCH(s->angle_snapping)) ? 1 : 0);
    if (s->debounce)
        rockcat_profile_set_int(p, ROCKCAT_SET_DEBOUNCE_MS, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->debounce)));
    if (s->left_handed)
        rockcat_profile_set_int(p, ROCKCAT_SET_LEFT_HANDED, gtk_switch_get_active(GTK_SWITCH(s->left_handed)) ? 1 : 0);

    const int inputs = n_inputs(s);
    for (int i = 0; i < static_cast<int>(s->buttons.size()); ++i) {
        rockcat_assignment b{};
        rockcat_profile_get_input(p, i / inputs, i % inputs, &b);
        int t = combo_type(s->buttons[i].combo);
        if (t >= 0)
            b.type = static_cast<uint8_t>(t);
        b.param1 = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->buttons[i].p1)));
        b.param2 = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->buttons[i].p2)));
        if (t == ROCKCAT_BT_SHORTCUT) {
            uint8_t mods = 0, key = 0;
            if (rockcat_parse_shortcut(gtk_entry_get_text(GTK_ENTRY(s->buttons[i].text)), &mods, &key) == ROCKCAT_OK) {
                b.param1 = mods;
                b.param2 = key;
            }
        } else if (t != ROCKCAT_BT_MACRO) {
            b.param1 = b.param2 = 0;
        }
        rockcat_profile_set_input(p, i / inputs, i % inputs, &b);
    }
}

void ensure_combo_id(GtkWidget *combo, int value)
{
    char id[8];
    std::snprintf(id, sizeof id, "%d", value);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id)) {
        char text[32];
        std::snprintf(text, sizeof text, "Unknown (0x%02x)", value);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, text);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id);
    }
}

void update_button_row_sensitivity(app_state *s, int i)
{
    int t = combo_type(s->buttons[i].combo);
    bool shortcut = t == ROCKCAT_BT_SHORTCUT;
    GtkEntry *e = GTK_ENTRY(s->buttons[i].text);
    gtk_entry_set_placeholder_text(e, shortcut ? "e.g. ctrl+c" : "macro text (optional)");
    gtk_widget_set_tooltip_text(s->buttons[i].text,
                                shortcut ? "Key combination: ctrl, shift, alt, win + a key (a-z, 0-9, f1-f24, enter, esc, tab, space, up, ...)"
                                         : "Typed as a keystroke macro when you press APPLY");
    gtk_widget_set_visible(s->buttons[i].text, shortcut || t == ROCKCAT_BT_MACRO);
}

void update_raw_view(app_state *s)
{
    // the profile the way it goes into a backup: 8 bytes of our own header, then the reports as the mouse has them
    std::string text;
    char line[96];
    std::vector<uint8_t> raw(static_cast<size_t>(std::max(0, rockcat_profile_export(s->profile, nullptr, 0))));
    rockcat_profile_export(s->profile, raw.data(), raw.size());
    for (size_t i = 0; i < raw.size(); i += 16) {
        int n = std::snprintf(line, sizeof line, "%04zx:", i);
        for (size_t j = i; j < i + 16 && j < raw.size(); ++j)
            n += std::snprintf(line + n, sizeof line - n, " %02x", raw[j]);
        text += line;
        text += '\n';
    }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->raw_view)), text.c_str(), -1);
}

void fill(app_state *s)
{
    const rockcat_profile *p = s->profile;
    if (!p)
        return;
    s->updating = true;
    auto get = [p](rockcat_setting which) {
        int v = 0;
        rockcat_profile_get_int(p, which, &v);
        return v;
    };
    bool same = true;
    for (size_t i = 0; i < s->dpi.size(); ++i) {
        int x = 0, y = 0, en = 0;
        rockcat_profile_get_dpi(p, static_cast<int>(i), &x, &y, &en);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->dpi[i].enabled), en);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->dpi[i].active), get(ROCKCAT_SET_DPI_ACTIVE) == static_cast<int>(i));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->dpi[i].x), x);
        if (s->dpi[i].y)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->dpi[i].y), y);
        same = same && x == y;
    }
    if (s->dpi_lock)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->dpi_lock), same);
    for (GtkWidget *b : s->polling)
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "hz")) == get(ROCKCAT_SET_POLLING_HZ))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);

    if (s->effect) {
        int eff = 0, by_engine = 0;
        rockcat_profile_get_effect(p, &eff, &by_engine);
        char id[8];
        std::snprintf(id, sizeof id, "%d", eff);
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->effect), id)) {
            // set to something the list doesnt offer. show it by name so nobody thinks its broken, picking it again isnt possible
            const char *name = rockcat_effect_name(static_cast<uint8_t>(eff));
            char text[64];
            if (name)
                std::snprintf(text, sizeof text, "%s  (not drawn by rockcat yet)", name);
            else
                std::snprintf(text, sizeof text, "Unknown (0x%02x)", eff);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->effect), id, text);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->effect), id);
        }
        if (s->render_engine)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->render_engine), by_engine);
    }
    if (s->shift_enable) {
        rockcat_shift_lighting sl;
        rockcat_shift_lighting_load(s->info.product_id, static_cast<uint8_t>(rockcat_profile_index(p)), &sl);
        gtk_switch_set_active(GTK_SWITCH(s->shift_enable), sl.enabled);
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->shift_style), sl.style == ROCKCAT_SHIFT_PULSE ? 1 : 0);
        GdkRGBA srgba = to_rgba(sl.color);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->shift_color), &srgba);
    }
    if (s->speed)
        gtk_range_set_value(GTK_RANGE(s->speed), get(ROCKCAT_SET_EFFECT_SPEED));
    if (s->theme) {
        char thid[8];
        std::snprintf(thid, sizeof thid, "%d", get(ROCKCAT_SET_COLOR_THEME) % ROCKCAT_AIMO_THEMES);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->theme), thid);
    }
    if (s->brightness)
        gtk_range_set_value(GTK_RANGE(s->brightness), get(ROCKCAT_SET_BRIGHTNESS));
    if (s->color_flow)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->color_flow), get(ROCKCAT_SET_COLOR_FLOW) != 0);
    for (size_t i = 0; i < s->led_color.size(); ++i) {
        rockcat_color c{};
        int en = 0;
        rockcat_profile_get_led(p, static_cast<int>(i), &c, &en);
        GdkRGBA rgba = to_rgba(c);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->led_enable[i]), en);
    }
    if (s->all_color) {
        // the colour button starts on the colour of the first key, so opening it doesnt start from black
        rockcat_color c{};
        rockcat_profile_get_led(p, 0, &c, nullptr);
        GdkRGBA rgba = to_rgba(c);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->all_color), &rgba);
    }
    if (s->idle_minutes) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->idle_minutes), get(ROCKCAT_SET_IDLE_MINUTES));
        char id[8];
        std::snprintf(id, sizeof id, "%d", get(ROCKCAT_SET_IDLE_STYLE));
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->idle_style), id);
        gtk_range_set_value(GTK_RANGE(s->idle_brightness), get(ROCKCAT_SET_IDLE_BRIGHTNESS));
    }
    if (s->macro_feedback)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->macro_feedback), get(ROCKCAT_SET_MACRO_FEEDBACK) != 0);

    if (s->angle_snapping)
        gtk_switch_set_active(GTK_SWITCH(s->angle_snapping), get(ROCKCAT_SET_ANGLE_SNAPPING) != 0);
    if (s->debounce)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->debounce), get(ROCKCAT_SET_DEBOUNCE_MS));
    if (s->left_handed)
        gtk_switch_set_active(GTK_SWITCH(s->left_handed), get(ROCKCAT_SET_LEFT_HANDED) != 0);

    const int inputs = n_inputs(s);
    for (int i = 0; i < static_cast<int>(s->buttons.size()); ++i) {
        rockcat_assignment b{};
        rockcat_profile_get_input(p, i / inputs, i % inputs, &b);
        ensure_combo_id(s->buttons[i].combo, b.type);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p1), b.param1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p2), b.param2);
        char sc[64] = "";
        if (b.type == ROCKCAT_BT_SHORTCUT)
            rockcat_format_shortcut(b.param1, b.param2, sc, sizeof sc);
        gtk_entry_set_text(GTK_ENTRY(s->buttons[i].text), sc);
        update_button_row_sensitivity(s, i);
    }
    if (s->raw_view)
        update_raw_view(s);
    s->updating = false;
    update_effect_ui(s);
    update_view_preview(s);
    update_keyboard_preview(s);
    show_fx_options(s);
    keys_refresh(s);
    shift_layer_load(s);
    redraw_light(s);
    if (s->mouse_buttons)
        gtk_widget_queue_draw(s->mouse_buttons);
}

void set_controls_sensitive(app_state *s, bool on)
{
    // every page needs the mouse except the Application panel on page 1
    for (int i = 1; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(s->notebook)); ++i)
        gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(s->notebook), i), on);
    if (s->dpi_panel)
        gtk_widget_set_sensitive(s->dpi_panel, on);
    if (s->polling_panel)
        gtk_widget_set_sensitive(s->polling_panel, on);
    gtk_widget_set_sensitive(s->apply_button,
                             on && !(s->auto_apply && gtk_switch_get_active(GTK_SWITCH(s->auto_apply))));
    gtk_widget_set_sensitive(s->activate_button, on);
    for (GtkWidget *b : s->profile_buttons)
        gtk_widget_set_sensitive(b, on);
}

void update_profile_buttons(app_state *s)
{
    bool busy = s->updating;
    s->updating = true;
    for (int i = 0; i < N_PROFILES; ++i) {
        // a star for changes that are not on the device yet, the active one is marked by its colour
        char text[32];
        std::snprintf(text, sizeof text, i == s->edit_profile && s->dirty ? "%d*" : "%d", i + 1);
        GtkWidget *b = s->profile_buttons[i];
        // a device with fewer profiles (or one that only has the one setup) doesnt get buttons for ones it hasnt
        gtk_widget_set_visible(b, i < n_profiles(s));
        gtk_button_set_label(GTK_BUTTON(b), text);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), i == s->edit_profile);
        GtkStyleContext *sc = gtk_widget_get_style_context(b);
        if (i == s->active_profile)
            gtk_style_context_add_class(sc, "rc-active");
        else
            gtk_style_context_remove_class(sc, "rc-active");
        std::string tip = i == s->active_profile ? "Active on the device" : "";
        if (i == s->edit_profile && s->dirty)
            tip += std::string(tip.empty() ? "" : ". ") + "Has changes that are not saved to the device yet";
        gtk_widget_set_tooltip_text(b, tip.empty() ? nullptr : tip.c_str());
    }
    s->updating = busy;
}

} // namespace gui
