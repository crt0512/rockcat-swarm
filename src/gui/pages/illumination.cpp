// the Illumination page: effect and colours for every device, plus whatever else it has: the live preview and the lighting
// engine that draws what a Kone AIMO cant, colour flow on a Kova, the idle settings of a keyboard

#include "../app.h"

namespace gui {

void on_light_changed(GtkWidget *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    redraw_light(s);
}

// the preview only exists for the device there is a drawing of
void redraw_light(app_state *s)
{
    if (s->mouse_light)
        gtk_widget_queue_draw(s->mouse_light);
    if (s->kb_view)
        gtk_widget_queue_draw(s->kb_view);
}

void apply_group_color(app_state *s, std::initializer_list<int> leds)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->group_color), &rgba);
    for (int i : leds)
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->led_color[static_cast<size_t>(i)]), &rgba);
    redraw_light(s);
    schedule_auto_apply(s);
}

void on_group_all(GtkButton *, gpointer d)
{
    auto *s = static_cast<app_state *>(d);
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->group_color), &rgba);
    for (GtkWidget *w : s->led_color)
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(w), &rgba);
    redraw_light(s);
    schedule_auto_apply(s);
}
void on_group_wheel(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {0}); }
void on_group_left(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {1, 2, 3, 4, 9}); }
void on_group_right(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {5, 6, 7, 8, 10}); }

uint8_t current_effect(app_state *s)
{
    int e = combo_type(s->effect);
    return e < 0 ? 0 : static_cast<uint8_t>(e);
}

// a copy of the loaded profile with the lighting widgets applied, so the preview shows what APPLY would do. caller frees it
rockcat_profile *preview_profile(app_state *s)
{
    rockcat_profile *p = rockcat_profile_clone(s->profile);
    if (!p)
        return nullptr;
    if (s->speed)
        rockcat_profile_set_int(p, ROCKCAT_SET_EFFECT_SPEED, static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->speed))));
    if (s->brightness)
        rockcat_profile_set_int(p, ROCKCAT_SET_BRIGHTNESS, static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->brightness))));
    if (s->theme) {
        int th = combo_type(s->theme);
        rockcat_profile_set_int(p, ROCKCAT_SET_COLOR_THEME, th < 0 ? 0 : th);
    }
    for (size_t i = 0; i < s->led_color.size(); ++i) {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        rockcat_profile_set_led(p, static_cast<int>(i), from_rgba(rgba), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->led_enable[i])));
    }
    return p;
}

gboolean preview_tick(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->preview_t += 0.04;
    // the idle preview plays for a while and then lets the picture be again
    if (s->idle_preview_t0 >= 0 && s->preview_t - s->idle_preview_t0 > 7) {
        s->idle_preview_t0 = -1;
        guint mine = s->preview_source;
        s->preview_source = 0;
        update_keyboard_preview(s);
        update_view_preview(s);
        if (!s->preview_source) {
            // nothing else needs the clock
            redraw_light(s);
            return G_SOURCE_REMOVE;
        }
        // something does and started a clock of its own, this one can go
        (void)mine;
        redraw_light(s);
        return G_SOURCE_REMOVE;
    }
    redraw_light(s);
    return G_SOURCE_CONTINUE;
}

// which effects the pc has to draw is the devices business, heartbeat is ours on a Kone and the keyboards own on a Ryos
bool pc_only(app_state *s, int effect)
{
    return s->caps && effect >= 0 && effect < 32 && ((s->caps->software_effects >> effect) & 1);
}

bool engine_rendered(app_state *s)
{
    if (!s->render_engine)
        return false;
    return pc_only(s, current_effect(s)) || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->render_engine));
}

void update_effect_ui(app_state *s)
{
    // everything in here is about the engine and the AIMO theme, a device with neither has nothing to update
    if (!s->render_engine)
        return;
    bool only_engine = pc_only(s, current_effect(s));
    if (only_engine && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->render_engine)))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->render_engine), TRUE);
    gtk_widget_set_sensitive(s->render_engine, !only_engine);
    bool software = engine_rendered(s);
    bool aimo = current_effect(s) == ROCKCAT_EFFECT_AIMO;
    gtk_label_set_text(GTK_LABEL(s->shift_hint),
                       software ? "Shown by the rockcat engine while Easy-Shift[+] is held."
                                : "This effect is drawn by the mouse, which always shows its built-in light blue "
                                  "(#059cfa, hard-coded in the firmware). Turning this on switches the profile to "
                                  "\"Render with rockcat engine\".");
    gtk_widget_set_sensitive(s->theme, aimo);
    gtk_widget_set_sensitive(s->colors_panel, !aimo);
    if (software && !s->preview_source)
        s->preview_source = g_timeout_add(40, preview_tick, s);
    else if (!software && s->preview_source) {
        g_source_remove(s->preview_source);
        s->preview_source = 0;
    }
    redraw_light(s);
}

// one of the settings of an effect got changed. these live on the pc and count at once: written to the config file,
// where the engine notices by itself. no APPLY, the device never hears of them
void on_fx_option(GtkWidget *w, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    const char *key = static_cast<const char *>(g_object_get_data(G_OBJECT(w), "rc-key"));
    int effect = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "rc-effect")), value = 0;
    if (GTK_IS_COLOR_BUTTON(w)) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(w), &c);
        value = (static_cast<int>(c.red * 255 + 0.5) << 16) | (static_cast<int>(c.green * 255 + 0.5) << 8) | static_cast<int>(c.blue * 255 + 0.5);
    } else if (GTK_IS_RANGE(w)) {
        value = static_cast<int>(gtk_range_get_value(GTK_RANGE(w)) + 0.5);
    } else if (GTK_IS_COMBO_BOX(w)) {
        value = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    } else if (GTK_IS_TOGGLE_BUTTON(w)) {
        value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    }
    // which profile slot: the profile, or the Easy-Shift layer of it (profile | ROCKCAT_SHIFT_LAYER)
    int slot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "rc-slot"));
    rockcat_effect_option_set(s->info.product_id, static_cast<uint8_t>(s->edit_profile | slot), effect, key, value);
}

// fills a box with one control for every setting the library lists for that effect (or for the idle animation, which
// is asked for the same way). built from the list, so a new setting shows up here without anybody touching the gui.
// says how many there were
int fill_options(app_state *s, GtkWidget *box, int effect, int slot = 0)
{
    GList *old = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *c = old; c; c = c->next)
        gtk_widget_destroy(GTK_WIDGET(c->data));
    g_list_free(old);
    const rockcat_effect_option *options = nullptr;
    int n = rockcat_effect_options(effect, &options);
    if (n <= 0)
        return 0;
    bool busy = s->updating;
    s->updating = true;
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    for (int i = 0; i < n; ++i) {
        const rockcat_effect_option &o = options[i];
        int value = rockcat_effect_option_get(s->info.product_id, static_cast<uint8_t>(s->edit_profile | slot), effect, o.key);
        GtkWidget *w = nullptr;
        const char *signal = "value-changed";
        switch (o.kind) {
        case ROCKCAT_OPTION_RANGE:
            w = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, o.min, o.max, 1);
            gtk_scale_set_value_pos(GTK_SCALE(w), GTK_POS_RIGHT);
            gtk_range_set_value(GTK_RANGE(w), value);
            gtk_widget_set_hexpand(w, TRUE);
            break;
        case ROCKCAT_OPTION_COLOR: {
            GdkRGBA c{((value >> 16) & 0xff) / 255.0, ((value >> 8) & 0xff) / 255.0, (value & 0xff) / 255.0, 1};
            w = gtk_color_button_new_with_rgba(&c);
            add_class(w, "rc-swatch");
            gtk_widget_set_halign(w, GTK_ALIGN_START);
            signal = "color-set";
            break;
        }
        case ROCKCAT_OPTION_TOGGLE:
            w = gtk_check_button_new_with_label(o.label);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), value != 0);
            signal = "toggled";
            break;
        case ROCKCAT_OPTION_CHOICE: {
            w = gtk_combo_box_text_new();
            gchar **names = g_strsplit(o.choices ? o.choices : "", "|", -1);
            for (gchar **name = names; *name; ++name)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), *name);
            g_strfreev(names);
            gtk_combo_box_set_active(GTK_COMBO_BOX(w), value);
            signal = "changed";
            break;
        }
        }
        g_object_set_data(G_OBJECT(w), "rc-key", const_cast<char *>(o.key));
        g_object_set_data(G_OBJECT(w), "rc-effect", GINT_TO_POINTER(effect));
        g_object_set_data(G_OBJECT(w), "rc-slot", GINT_TO_POINTER(slot));
        g_object_set_data(G_OBJECT(w), "rc-no-autoapply", GINT_TO_POINTER(1));
        g_signal_connect(w, signal, G_CALLBACK(on_fx_option), s);
        if (o.kind == ROCKCAT_OPTION_TOGGLE) {
            gtk_grid_attach(GTK_GRID(grid), w, 0, i, 2, 1);
        } else {
            GtkWidget *name = label(o.label);
            gtk_label_set_line_wrap(GTK_LABEL(name), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(name), 16);
            gtk_grid_attach(GTK_GRID(grid), name, 0, i, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), w, 1, i, 1, 1);
        }
    }
    gtk_container_add(GTK_CONTAINER(box), grid);
    s->updating = busy;
    return n;
}

// (a panel that can stay away is kept out of show_all, which means its insides get shown by hand)
void show_panel(GtkWidget *panel, bool visible)
{
    gtk_widget_set_visible(panel, visible);
    if (!visible)
        return;
    GList *inside = gtk_container_get_children(GTK_CONTAINER(panel));
    for (GList *c = inside; c; c = c->next)
        gtk_widget_show_all(GTK_WIDGET(c->data));
    g_list_free(inside);
}

// the button in the idle animation box: the engine plays it once on the keyboard and wakes up again
void on_idle_fx_preview(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    rockcat_effect_idle_preview(s->info.product_id, static_cast<uint8_t>(s->edit_profile));
    if (s->edit_profile != s->active_profile)
        set_status(s, "The preview plays on the active profile, this one is not it", "connected");
}

// the effect of the Easy-Shift layer: its colours standing still, or one of the effects the pc draws, with its own settings
void show_es_options(app_state *s)
{
    if (!s->es_panel)
        return;
    const bool painting_it = s->kb_layer == 1;
    gtk_widget_set_visible(s->es_panel, painting_it);
    if (!painting_it)
        return;
    int effect = rockcat_shift_layer_effect(s->info.product_id, static_cast<uint8_t>(s->edit_profile));
    bool busy = s->updating;
    s->updating = true;
    char id[8];
    std::snprintf(id, sizeof id, "%d", effect);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->es_effect), id))
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->es_effect), 0);
    s->updating = busy;
    fill_options(s, s->es_box, effect, ROCKCAT_SHIFT_LAYER);
    // (out of show_all, so its insides get shown by hand)
    GList *inside = gtk_container_get_children(GTK_CONTAINER(s->es_panel));
    for (GList *c = inside; c; c = c->next)
        gtk_widget_show_all(GTK_WIDGET(c->data));
    g_list_free(inside);
}

static void on_es_effect(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const char *id = gtk_combo_box_get_active_id(combo);
    if (s->updating || !id)
        return;
    rockcat_shift_layer_set_effect(s->info.product_id, static_cast<uint8_t>(s->edit_profile), std::atoi(id));
    show_es_options(s);
}

// the boxes with the settings of the effect that is picked, and of the idle animation next to it. called whenever the
// effect or the profile changes, the controls get made again with the values of that profile
void show_fx_options(app_state *s)
{
    if (!s->fx_panel || !s->effect)
        return;
    const int effect = current_effect(s);
    const bool ours = pc_only(s, effect);
    show_panel(s->fx_panel, ours && fill_options(s, s->fx_box, effect) > 0);
    // what happens when nobody types: the keyboard does it for its own effects, we do it for ours. two boxes, the
    // one that counts for the picked effect shows
    if (s->idle_fx_panel)
        show_panel(s->idle_fx_panel, ours && fill_options(s, s->idle_fx_box, ROCKCAT_EFFECT_OPTIONS_IDLE) > 0);
    if (s->idle_panel && s->idle_fx_panel)
        gtk_widget_set_visible(s->idle_panel, !ours);
}

void on_effect_changed(GtkComboBox *, gpointer data)
{
    update_effect_ui(static_cast<app_state *>(data));
    update_view_preview(static_cast<app_state *>(data));
    update_keyboard_preview(static_cast<app_state *>(data));
    show_fx_options(static_cast<app_state *>(data));
}

void on_flow_toggled(GtkToggleButton *, gpointer data)
{
    update_view_preview(static_cast<app_state *>(data));
}

void on_theme_changed(GtkComboBox *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    for (GtkWidget *w : s->theme_swatches)
        gtk_widget_queue_draw(w);
    redraw_light(s);
}

gboolean on_draw_swatch(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    int slot = 0;
    for (int i = 0; i < 3; ++i)
        if (s->theme_swatches[i] == w)
            slot = i;
    rockcat_color c[3];
    int th = combo_type(s->theme);
    rockcat_aimo_theme(th < 0 ? 0 : th, c);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    rounded_rect(cr, 1, 1, W - 2, H - 2, 3);
    cairo_set_source_rgb(cr, c[slot].r / 255.0, c[slot].g / 255.0, c[slot].b / 255.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
    return FALSE;
}

// Status line under the effect: who is drawing the active profile's lighting.
void set_engine_label(app_state *s)
{
    if (!s->engine_label)
        return;
    if (!s->engine) {
        gtk_label_set_text(GTK_LABEL(s->engine_label),
                           s->engine_busy ? "Lighting engine: handled by `rockcat-cli engine` / the rockcat-engine service"
                                          : "Lighting engine: not running");
        return;
    }
    rockcat_engine_status st;
    rockcat_engine_get_status(s->engine, &st);
    char msg[160];
    if (st.last_error != ROCKCAT_OK)
        std::snprintf(msg, sizeof msg, "Lighting engine: %s", rockcat_strerror(st.last_error));
    else if (st.profile == 0xff)
        std::snprintf(msg, sizeof msg, "Lighting engine: starting\u2026");
    else {
        const char *n = rockcat_effect_name(st.effect);
        std::snprintf(msg, sizeof msg, "Active profile %d: %s, %s", st.profile + 1, n ? n : "?",
                      st.rendering ? "drawn by rockcat" : "drawn by the mouse");
    }
    gtk_label_set_text(GTK_LABEL(s->engine_label), msg);
}

// The GUI runs the engine for as long as it is open (window or tray); it idles
// while the active profile is drawn by the mouse. If another engine (CLI or
// the user service) holds the lock, retry every few seconds.
void start_engine(app_state *s)
{
    if (s->engine)
        return;
    int r = rockcat_engine_start(nullptr, ROCKCAT_ENGINE_AUTO, &s->engine);
    if (r != ROCKCAT_OK)
        s->engine = nullptr;
    s->engine_busy = r == ROCKCAT_ERR_BUSY;
    set_engine_label(s);
}

gboolean engine_tick(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->engine && ++s->engine_retry % 10 == 0)
        start_engine(s);
    set_engine_label(s);
    return G_SOURCE_CONTINUE;
}

void on_render_toggled(GtkToggleButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    update_effect_ui(s);
}

void on_shift_toggled(GObject *, GParamSpec *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    // a custom Easy-Shift colour needs the effect to be engine-rendered
    if (gtk_switch_get_active(GTK_SWITCH(s->shift_enable)))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->render_engine), TRUE);
    update_effect_ui(s);
}

GtkWidget *build_illumination_page(app_state *s)
{
    const rockcat_caps *caps = s->caps;
    // the engine, the AIMO theme and the Easy-Shift colour all hang on the pc drawing effects, which only some devices need
    // (other devices have effects the pc draws too, but their engine needs none of these controls: it just follows the profile)
    const bool engine = is_kone(s);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);

    // the preview is a drawing of a Kone AIMO, so only a Kone AIMO gets it
    if (is_kone(s)) {
        s->mouse_light = gtk_drawing_area_new();
        gtk_widget_set_size_request(s->mouse_light, 230, 340);
        g_signal_connect(s->mouse_light, "draw", G_CALLBACK(on_draw_lighting), s);
        gtk_box_pack_start(GTK_BOX(page), s->mouse_light, FALSE, FALSE, 0);
    } else if (s->view && !is_keyboard(s)) {
        // everybody else: ROCCATs picture of the device with its lights in the colours picked on the right
        s->mouse_light = gtk_drawing_area_new();
        int w = 0, h = 0;
        // same size and place as on the button page
        view_size(s->view, MOUSE_VIEW_HEIGHT, &w, &h);
        gtk_widget_set_size_request(s->mouse_light, w, h);
        gtk_widget_set_hexpand(s->mouse_light, TRUE);
        gtk_widget_set_vexpand(s->mouse_light, TRUE);
        g_signal_connect(s->mouse_light, "draw", G_CALLBACK(on_draw_view_lights), s);
        gtk_box_pack_start(GTK_BOX(page), s->mouse_light, TRUE, TRUE, 0);
    }

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);

    // effect
    GtkWidget *eg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(eg), 12);
    gtk_grid_set_row_spacing(GTK_GRID(eg), 8);
    int row = 0;
    s->effect = gtk_combo_box_text_new();
    for (int e = 0; e < 32; ++e) {
        const char *name = rockcat_effect_name(static_cast<uint8_t>(e));
        if (!name || !((caps->effects >> e) & 1))
            continue;
        char id[8], text[64];
        std::snprintf(id, sizeof id, "%d", e);
        std::snprintf(text, sizeof text, pc_only(s, e) ? "%s  (engine)" : "%s", name);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->effect), id, text);
    }
    g_signal_connect(s->effect, "changed", G_CALLBACK(on_effect_changed), s);
    GtkWidget *effbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(effbox), s->effect, TRUE, TRUE, 0);
    if (engine) {
        s->render_engine = gtk_check_button_new_with_label("Render with rockcat engine");
        gtk_widget_set_tooltip_text(s->render_engine,
                                    "The PC draws the effect (the mouse stores AIMO). Needed for a custom "
                                    "Easy-Shift[+] colour; requires the engine to run.");
        g_signal_connect(s->render_engine, "toggled", G_CALLBACK(on_render_toggled), s);
        gtk_box_pack_start(GTK_BOX(effbox), s->render_engine, FALSE, FALSE, 0);
    }
    gtk_grid_attach(GTK_GRID(eg), label("EFFECT"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), effbox, 1, row++, 1, 1);
    if (has_setting(s, ROCKCAT_SET_EFFECT_SPEED)) {
        s->speed = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, caps->effect_speed_min, caps->effect_speed_max, 1);
        gtk_widget_set_hexpand(s->speed, TRUE);
        g_signal_connect(s->speed, "value-changed", G_CALLBACK(on_light_changed), s);
        gtk_grid_attach(GTK_GRID(eg), label("SPEED"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(eg), s->speed, 1, row++, 1, 1);
    }
    if (has_setting(s, ROCKCAT_SET_BRIGHTNESS)) {
        s->brightness = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 255, 1);
        gtk_widget_set_hexpand(s->brightness, TRUE);
        g_signal_connect(s->brightness, "value-changed", G_CALLBACK(on_light_changed), s);
        gtk_grid_attach(GTK_GRID(eg), label("BRIGHTNESS"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(eg), s->brightness, 1, row++, 1, 1);
    }
    if (has_setting(s, ROCKCAT_SET_COLOR_FLOW)) {
        s->color_flow = gtk_check_button_new_with_label("The mouse steps through the colours by itself");
        gtk_widget_set_tooltip_text(s->color_flow, "Done by the mouse, in visible steps. The effect \"Colour flow, smooth\" does the same without the steps, drawn by rockcat");
        g_signal_connect(s->color_flow, "toggled", G_CALLBACK(on_flow_toggled), s);
        gtk_grid_attach(GTK_GRID(eg), label("COLOUR FLOW"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(eg), s->color_flow, 1, row++, 1, 1);
    }
    if (has_setting(s, ROCKCAT_SET_COLOR_THEME)) {
        s->theme = gtk_combo_box_text_new();
        for (int i = 0; i < ROCKCAT_AIMO_THEMES; ++i) {
            char id[8], text[32];
            std::snprintf(id, sizeof id, "%d", i);
            std::snprintf(text, sizeof text, "Theme %d", i + 1);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->theme), id, text);
        }
        g_signal_connect(s->theme, "changed", G_CALLBACK(on_theme_changed), s);
        GtkWidget *tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(tb), s->theme, FALSE, FALSE, 0);
        for (auto &sw : s->theme_swatches) {
            sw = gtk_drawing_area_new();
            gtk_widget_set_size_request(sw, 22, 22);
            g_signal_connect(sw, "draw", G_CALLBACK(on_draw_swatch), s);
            gtk_box_pack_start(GTK_BOX(tb), sw, FALSE, FALSE, 0);
        }
        gtk_grid_attach(GTK_GRID(eg), label("AIMO THEME"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(eg), tb, 1, row++, 1, 1);
    }
    if (engine) {
        s->engine_label = label("", "rc-hint");
        gtk_label_set_line_wrap(GTK_LABEL(s->engine_label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(s->engine_label), 60);
        gtk_grid_attach(GTK_GRID(eg), s->engine_label, 1, row++, 1, 1);
        GtkWidget *ehint = label("\"Render with rockcat engine\" lets the PC draw the effect (always on for effects "
                                 "marked engine). rockcat-swarm does this while it is open or in the tray; without "
                                 "the GUI use `rockcat-cli engine` or the rockcat-engine user service. "
                                 "AIMO uses the colour theme instead of the per-LED colours.", "rc-hint");
        gtk_label_set_line_wrap(GTK_LABEL(ehint), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(ehint), 60);
        gtk_grid_attach(GTK_GRID(eg), ehint, 0, row++, 2, 1);
    }
    if (!is_kone(s) && caps->software_effects) {
        GtkWidget *hint = label("The effects marked (engine) are drawn by rockcat-swarm, so they only run while it is open "
                                "or in the tray. Pick one, press APPLY, then play with its settings below.", "rc-hint");
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(hint), 40);
        gtk_grid_attach(GTK_GRID(eg), hint, 0, row++, 2, 1);
    }
    gtk_box_pack_start(GTK_BOX(right), box_panel("LIGHTING EFFECT", eg), FALSE, FALSE, 0);
    if (caps->software_effects) {
        // filled by show_fx_options() with whatever the picked effect can be told. stays away for effects without settings
        s->fx_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        s->fx_panel = box_panel("EFFECT SETTINGS (take effect at once)", s->fx_box);
        gtk_widget_set_no_show_all(s->fx_panel, TRUE);
        gtk_box_pack_start(GTK_BOX(right), s->fx_panel, FALSE, FALSE, 0);
    }
    if (caps->software_effects && is_keyboard(s)) {
        // the idle animation of the effects the pc draws. a thing of its own, the same whichever of them is on.
        // (only where keys can wake it up again, a mouse says nothing about being moved on the interface we listen to)
        s->idle_fx_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *ib = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_box_pack_start(GTK_BOX(ib), s->idle_fx_box, FALSE, FALSE, 0);
        GtkWidget *play = gtk_button_new_with_label("PREVIEW");
        gtk_widget_set_tooltip_text(play, "Plays it once on the keyboard and on the picture, then wakes up again. Needs this profile to be the active one");
        gtk_widget_set_halign(play, GTK_ALIGN_START);
        g_signal_connect(play, "clicked", G_CALLBACK(on_idle_fx_preview), s);
        gtk_box_pack_start(GTK_BOX(ib), play, FALSE, FALSE, 0);
        s->idle_fx_panel = box_panel("IDLE ANIMATION (takes effect at once)", ib);
        // what the keys do while Easy-Shift is held, shown when that layer is the one being painted on the left
        GtkWidget *eb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        s->es_effect = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->es_effect), "1", "Its colours, standing still");
        for (int e = 0; e < 32; ++e)
            if ((caps->software_effects >> e) & 1) {
                char eid[8];
                std::snprintf(eid, sizeof eid, "%d", e);
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->es_effect), eid, rockcat_effect_name(e));
            }
        g_object_set_data(G_OBJECT(s->es_effect), "rc-no-autoapply", GINT_TO_POINTER(1));
        g_signal_connect(s->es_effect, "changed", G_CALLBACK(on_es_effect), s);
        s->es_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(eb), s->es_effect, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(eb), s->es_box, FALSE, FALSE, 0);
        GtkWidget *eh = label("Uses the colours painted on the Easy-Shift layer. Takes effect at once, while rockcat runs.", "rc-hint");
        gtk_label_set_line_wrap(GTK_LABEL(eh), TRUE);
        gtk_label_set_xalign(GTK_LABEL(eh), 0);
        gtk_box_pack_start(GTK_BOX(eb), eh, FALSE, FALSE, 0);
        s->es_panel = box_panel("EASY-SHIFT EFFECT", eb);
        gtk_widget_set_no_show_all(s->es_panel, TRUE);
        gtk_box_pack_start(GTK_BOX(right), s->es_panel, FALSE, FALSE, 0);
        gtk_widget_set_no_show_all(s->idle_fx_panel, TRUE);
        gtk_box_pack_start(GTK_BOX(right), s->idle_fx_panel, FALSE, FALSE, 0);
    }

    // colours
    GtkWidget *cg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(cg), 10);
    gtk_grid_set_row_spacing(GTK_GRID(cg), 4);
    const bool alpha = caps->features & ROCKCAT_HAS_LED_BRIGHTNESS;
    if (is_keyboard(s) && s->kb) {
        // the keyboard with every key in its colour, click keys and choose a colour. sits on top of everything else on this page
        s->colors_panel = box_panel("KEY COLOURS", build_keyboard_view(s));
        gtk_size_group_add_widget(s->kb_rows, panel_title(s->colors_panel));
    } else {
        const int leds = caps->led_count;
        s->led_color.assign(static_cast<size_t>(leds), nullptr);
        s->led_enable.assign(static_cast<size_t>(leds), nullptr);
        for (int i = 0; i < leds; ++i) {
            int col = (i % 2) * 3, r = i / 2;
            char t[40];
            std::snprintf(t, sizeof t, "%d  %s", i + 1, rockcat_led_label(caps, i));
            GtkWidget *en = s->led_enable[static_cast<size_t>(i)] = gtk_check_button_new();
            GtkWidget *cb = s->led_color[static_cast<size_t>(i)] = gtk_color_button_new();
            gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(cb), alpha);
            add_class(cb, "rc-swatch");
            g_signal_connect(cb, "color-set", G_CALLBACK(on_light_changed), s);
            g_signal_connect(en, "toggled", G_CALLBACK(on_light_changed), s);
            gtk_grid_attach(GTK_GRID(cg), en, col, r, 1, 1);
            gtk_grid_attach(GTK_GRID(cg), cb, col + 1, r, 1, 1);
            gtk_grid_attach(GTK_GRID(cg), label(t), col + 2, r, 1, 1);
        }
        GtkWidget *gb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        s->group_color = gtk_color_button_new();
        gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->group_color), alpha);
        GdkRGBA def{0.40, 0.76, 0.97, 1.0};
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->group_color), &def);
        gtk_box_pack_start(GTK_BOX(gb), label("Set colour for:"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(gb), s->group_color, FALSE, FALSE, 0);
        // wheel and the two sides are how the leds of a Kone AIMO are laid out, any other mouse just gets "all"
        struct { const char *t; GCallback cb; bool kone_only; } groups[] = {
            {"ALL", G_CALLBACK(on_group_all), false}, {"WHEEL", G_CALLBACK(on_group_wheel), true},
            {"LEFT SIDE", G_CALLBACK(on_group_left), true}, {"RIGHT SIDE", G_CALLBACK(on_group_right), true}};
        for (const auto &g : groups) {
            if (g.kone_only && !is_kone(s))
                continue;
            GtkWidget *b = gtk_button_new_with_label(g.t);
            g_signal_connect(b, "clicked", g.cb, s);
            gtk_box_pack_start(GTK_BOX(gb), b, FALSE, FALSE, 0);
        }
        gtk_grid_attach(GTK_GRID(cg), gb, 0, (leds + 1) / 2, 6, 1);
        s->colors_panel = box_panel(alpha ? "COLOURS (checkbox = LED enabled, alpha = LED brightness)" : "COLOURS (checkbox = light on)", cg);
    }
    if (!s->kb_view)
        gtk_box_pack_start(GTK_BOX(right), s->colors_panel, FALSE, FALSE, 0);

    if (has_setting(s, ROCKCAT_SET_IDLE_MINUTES)) {
        // what the lights do when nobody has touched a key for a while
        GtkWidget *ig = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(ig), 12);
        gtk_grid_set_row_spacing(GTK_GRID(ig), 8);
        s->idle_minutes = gtk_spin_button_new_with_range(0, 255, 1);
        GtkWidget *mb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(mb), s->idle_minutes, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mb), label("minutes, 0 = never", "rc-hint"), FALSE, FALSE, 0);
        s->idle_style = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->idle_style), "0", "Dim");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->idle_style), "1", "Starlit sky");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->idle_style), "3", "Fall asleep");
        s->idle_brightness = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 255, 1);
        gtk_widget_set_hexpand(s->idle_brightness, TRUE);
        gtk_grid_attach(GTK_GRID(ig), label("AFTER"), 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(ig), mb, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(ig), label("THEN"), 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(ig), s->idle_style, 1, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(ig), label("DIM TO"), 0, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(ig), s->idle_brightness, 1, 2, 1, 1);
        if (has_setting(s, ROCKCAT_SET_MACRO_FEEDBACK)) {
            s->macro_feedback = gtk_check_button_new_with_label("Light show while a macro runs");
            gtk_grid_attach(GTK_GRID(ig), s->macro_feedback, 1, 3, 1, 1);
        }
        if (s->kb_view) {
            GtkWidget *play = gtk_button_new_with_label("PREVIEW");
            gtk_widget_set_tooltip_text(play, "Play it on the picture above for a few seconds. Nothing is sent to the keyboard");
            gtk_widget_set_halign(play, GTK_ALIGN_START);
            g_signal_connect(play, "clicked", G_CALLBACK(on_idle_preview), s);
            gtk_grid_attach(GTK_GRID(ig), play, 1, 4, 1, 1);
        }
        // (for the effects the keyboard draws itself. the ones we draw have their own box, see show_fx_options)
        s->idle_panel = box_panel("WHEN IDLE", ig);
        gtk_box_pack_start(GTK_BOX(right), s->idle_panel, FALSE, FALSE, 0);
    }

    if (engine) {
        GtkWidget *sg = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(sg), 12);
        gtk_grid_set_row_spacing(GTK_GRID(sg), 8);
        s->shift_enable = gtk_switch_new();
        gtk_widget_set_valign(s->shift_enable, GTK_ALIGN_CENTER);
        g_signal_connect(s->shift_enable, "notify::active", G_CALLBACK(on_shift_toggled), s);
        s->shift_color = gtk_color_button_new();
        gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->shift_color), TRUE);
        add_class(s->shift_color, "rc-swatch");
        s->shift_style = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->shift_style), "Solid");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->shift_style), "Pulse");
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->shift_style), 0);
        GtkWidget *srow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(srow), s->shift_enable, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(srow), s->shift_color, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(srow), s->shift_style, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(sg), srow, 0, 0, 1, 1);
        s->shift_hint = label("", "rc-hint");
        gtk_label_set_line_wrap(GTK_LABEL(s->shift_hint), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(s->shift_hint), 60);
        gtk_grid_attach(GTK_GRID(sg), s->shift_hint, 0, 1, 1, 1);
        gtk_box_pack_start(GTK_BOX(right), box_panel("EASY-SHIFT[+] LIGHTING", sg), FALSE, FALSE, 0);
    }

    // next to a picture the settings keep one width (the picture gets the rest), alone they spread out
    const bool picture = s->mouse_light && !is_kone(s);
    if (picture) {
        gtk_widget_set_size_request(right, MOUSE_SIDE_WIDTH, -1);
        gtk_widget_set_hexpand(right, FALSE);
    }
    gtk_box_pack_start(GTK_BOX(page), right, !picture, TRUE, 0);
    if (s->kb_view) {
        // keyboards: the board on the left, the settings in a column next to it
        GtkWidget *all = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        gtk_container_set_border_width(GTK_CONTAINER(all), 16);
        // the board takes all the room there is (it grows with the window, keeps its shape and stays centred), the
        // settings next to it keep one width
        gtk_widget_set_hexpand(s->kb_view, TRUE);
        gtk_container_set_border_width(GTK_CONTAINER(page), 0);
        gtk_box_pack_start(GTK_BOX(all), s->colors_panel, TRUE, TRUE, 0);
        // the board stays where it is and the settings scroll next to it: whoever changes one wants to see what it
        // does to the keys
        GtkWidget *below = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(below), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        // (without propagating its insides width, a scrolled window asks for exactly its min content width)
        gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(below), 400);
        gtk_widget_set_size_request(below, 400, -1);
        // the sliders inside want to grow, which would make the whole column grow with the window. it doesnt
        gtk_widget_set_hexpand(below, FALSE);
        gtk_container_add(GTK_CONTAINER(below), page);
        gtk_box_pack_start(GTK_BOX(all), below, FALSE, TRUE, 0);
        return all;
    }
    return page;
}

} // namespace gui
