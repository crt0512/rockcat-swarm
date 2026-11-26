// the picture of the device on the buttons and the illumination page: ROCCATs own top view, with a marker on every button
// and the lights tinted in the colours picked. everything it needs comes out of assets/.../views/<product id>/ and is made
// by tools/assets/mouse-view.py. a device nobody made a view for simply has none, the pages cope

#include "app.h"

namespace gui {

struct device_view {
    cairo_surface_t *picture = nullptr;
    int width = 0, height = 0;
    double marker = 27;
    // the buttons page shows only this much of the picture from the top (the Nyth has its back below, that one is
    // there for its light and has no buttons). height when the view.ini doesnt say
    int buttons_height = 0;
    struct mark {
        double x = 0, y = 0;
        int number = 0; // what Swarm prints on it. 0 = this button has no marker
    };
    std::vector<mark> marks;             // by input
    std::vector<cairo_surface_t *> lights; // by led, nullptr where there is no mask
    std::vector<char> painted;           // by led: the light is part of the picture (see view.ini)
    std::vector<char> shine;             // by led: shines through the shell, only drawn on the illumination page
};

namespace {

// fits the picture (its top vh rows) into the widget and leaves cairo in picture coordinates. returns the scale
double fit(const device_view *v, int vh, GtkWidget *w, cairo_t *cr, double *left = nullptr, double *top = nullptr)
{
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double k = std::min(W / v->width, H / vh);
    double x = (W - v->width * k) / 2, y = (H - vh * k) / 2;
    if (cr) {
        cairo_translate(cr, x, y);
        cairo_scale(cr, k, k);
    }
    if (left)
        *left = x;
    if (top)
        *top = y;
    return k;
}

// a light: the mask says where (its alpha) and how bright the light is painted there (its grey), the colour is ours
void paint_light(cairo_t *cr, cairo_surface_t *mask, double r, double g, double b, double alpha = 1)
{
    cairo_push_group(cr);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
    cairo_set_source_surface(cr, mask, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
    cairo_set_source_surface(cr, mask, 0, 0);
    cairo_paint(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
}

int marker_at(app_state *s, GtkWidget *w, double px, double py)
{
    const device_view *v = s->view;
    double left = 0, top = 0, k = fit(v, v->buttons_height, w, nullptr, &left, &top);
    double x = (px - left) / k, y = (py - top) / k;
    for (size_t i = 0; i < v->marks.size(); ++i)
        if (v->marks[i].number && std::hypot(v->marks[i].x - x, v->marks[i].y - y) <= v->marker / 2 + 3)
            return static_cast<int>(i);
    return -1;
}

} // namespace

device_view *view_load(int pid)
{
    char dir[96];
    std::snprintf(dir, sizeof dir, "/org/rockcat/swarm/assets/views/%04x/", pid);
    GBytes *bytes = g_resources_lookup_data((std::string(dir) + "view.ini").c_str(), G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (!bytes)
        return nullptr;
    GKeyFile *kf = g_key_file_new();
    gsize len = 0;
    const char *text = static_cast<const char *>(g_bytes_get_data(bytes, &len));
    bool ok = g_key_file_load_from_data(kf, text, len, G_KEY_FILE_NONE, nullptr);
    g_bytes_unref(bytes);
    auto *v = new device_view;
    if (ok) {
        char *name = g_key_file_get_string(kf, "view", "picture", nullptr);
        v->picture = name ? asset_surface(std::string(dir) + name) : nullptr;
        g_free(name);
        v->width = g_key_file_get_integer(kf, "view", "width", nullptr);
        v->height = g_key_file_get_integer(kf, "view", "height", nullptr);
        v->marker = g_key_file_get_integer(kf, "view", "marker", nullptr);
        v->buttons_height = g_key_file_get_integer(kf, "view", "buttons_height", nullptr);
        if (v->buttons_height <= 0 || v->buttons_height > v->height)
            v->buttons_height = v->height;
    }
    if (!ok || !v->picture || v->width <= 0 || v->height <= 0) {
        g_key_file_unref(kf);
        view_free(v);
        return nullptr;
    }
    gsize n = 0;
    if (char **keys = g_key_file_get_keys(kf, "buttons", &n, nullptr)) {
        for (gsize i = 0; i < n; ++i) {
            int input = std::atoi(keys[i]), number = 0;
            double x = 0, y = 0;
            char *value = g_key_file_get_string(kf, "buttons", keys[i], nullptr);
            // (g_ascii_strtod and not sscanf: a german locale would read 116.5 as 116)
            char *end = nullptr;
            bool parsed = false;
            if (value) {
                x = g_ascii_strtod(value, &end);
                if (end && *end == ';') {
                    y = g_ascii_strtod(end + 1, &end);
                    if (end && *end == ';') {
                        number = std::atoi(end + 1);
                        parsed = true;
                    }
                }
            }
            if (parsed && input >= 0 && input < 256) {
                if (v->marks.size() <= static_cast<size_t>(input))
                    v->marks.resize(static_cast<size_t>(input) + 1);
                device_view::mark &m = v->marks[static_cast<size_t>(input)];
                m.x = x;
                m.y = y;
                m.number = number;
            }
            g_free(value);
        }
        g_strfreev(keys);
    }
    if (char **keys = g_key_file_get_keys(kf, "lights", &n, nullptr)) {
        for (gsize i = 0; i < n; ++i) {
            int led = std::atoi(keys[i]);
            char *value = g_key_file_get_string(kf, "lights", keys[i], nullptr);
            if (value && led >= 0 && led < 256) {
                if (v->lights.size() <= static_cast<size_t>(led))
                    v->lights.resize(static_cast<size_t>(led) + 1, nullptr);
                v->lights[static_cast<size_t>(led)] = asset_surface(std::string(dir) + value);
                v->painted.resize(v->lights.size(), 0);
                v->painted[static_cast<size_t>(led)] = static_cast<char>(g_key_file_get_integer(kf, "painted", keys[i], nullptr) != 0);
                v->shine.resize(v->lights.size(), 0);
                v->shine[static_cast<size_t>(led)] = static_cast<char>(g_key_file_get_integer(kf, "shine", keys[i], nullptr) != 0);
            }
            g_free(value);
        }
        g_strfreev(keys);
    }
    g_key_file_unref(kf);
    return v;
}

void view_free(device_view *v)
{
    if (!v)
        return;
    if (v->picture)
        cairo_surface_destroy(v->picture);
    for (cairo_surface_t *l : v->lights)
        if (l)
            cairo_surface_destroy(l);
    delete v;
}

// the number Swarm prints on the marker of that button, 0 if the view has none for it (or there is no view)
int view_button_number(const device_view *v, int input)
{
    return v && input >= 0 && static_cast<size_t>(input) < v->marks.size() ? v->marks[static_cast<size_t>(input)].number : 0;
}

// how big the picture wants to be shown: as high as asked, as wide as that makes it
void view_size(const device_view *v, int height, int *w, int *h)
{
    *h = height;
    *w = v->width * height / v->height;
}

// the same for the buttons page, which leaves out what is below buttons_height
void view_buttons_size(const device_view *v, int height, int *w, int *h)
{
    *h = height;
    *w = v->width * height / v->buttons_height;
}

// the lights as the page has them set right now. a light that is off gets painted dark, the picture itself has
// some of them painted on in ROCCATs red and that would shine through otherwise
// animate = show what the effect does over time (the illumination page), otherwise the colours stand still
void draw_view_lights(app_state *s, cairo_t *cr, bool animate)
{
    const device_view *v = s->view;
    const int effect = s->effect ? static_cast<int>(current_effect(s)) : static_cast<int>(ROCKCAT_EFFECT_FULLY_LIT);
    // what the leds show right now according to the library, which knows how blinking and breathing go.
    // its a preview: the rhythm is ours, the device has its own clock
    std::vector<rockcat_color> frame(v->lights.size(), rockcat_color{255, 0, 0, 0});
    bool have = false;
    if (rockcat_profile *p = preview_profile(s)) {
        have = rockcat_profile_render(p, animate ? effect : (effect == ROCKCAT_EFFECT_OFF ? effect : ROCKCAT_EFFECT_FULLY_LIT),
                                      s->preview_t, 0.0, frame.data(), static_cast<int>(frame.size())) > 0;
        rockcat_profile_free(p);
    }
    // an effect the engine draws: when the profile showing is the active one, the picture shows what goes to the device
    if (animate && pc_only(s, effect)) {
        rockcat_engine_status st{};
        rockcat_engine *engine = keyboard_engine(s);
        if (engine)
            rockcat_engine_get_status(engine, &st);
        std::vector<rockcat_color> live(static_cast<size_t>(std::max(1, s->caps->led_count)));
        if (engine && st.rendering && st.profile == s->edit_profile && st.effect == effect &&
            rockcat_engine_get_frame(engine, live.data(), static_cast<int>(live.size())) > 0) {
            for (size_t i = 0; i < frame.size() && i < live.size(); ++i)
                frame[i] = live[i];
            have = true;
        }
    }
    const bool flow = animate && s->color_flow && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->color_flow));
    for (size_t led = 0; led < v->lights.size(); ++led) {
        // light through the shell (free set) is a whole lot of glow, only worth it where the lights are the point
        if (!v->lights[led] || (!animate && v->shine[led]))
            continue;
        double r = 0, g = 0, b = 0;
        if (have) {
            r = frame[led].r / 255.0 * frame[led].a / 255.0;
            g = frame[led].g / 255.0 * frame[led].a / 255.0;
            b = frame[led].b / 255.0 * frame[led].a / 255.0;
        }
        if (flow) {
            // colour flow: the light wanders through the colours, how bright it is still follows the effect
            double level = std::max(r, std::max(g, b)), h = std::fmod(s->preview_t * 0.12 + led * 0.08, 1.0) * 6, x = 1 - std::fabs(std::fmod(h, 2.0) - 1);
            const double wheel[6][3] = {{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}};
            const double *c = wheel[static_cast<int>(h) % 6];
            r = c[0] * level;
            g = c[1] * level;
            b = c[2] * level;
        }
        double level = std::max(r, std::max(g, b));
        if (v->painted[led]) {
            // part of the picture (a wheel light painted on in ROCCATs red): always painted over, dark when its off
            const double dark = 0.07;
            paint_light(cr, v->lights[led], std::max(r, dark), std::max(g, dark), std::max(b, dark + 0.01));
        } else if (level > 0.01) {
            // a glow next to the device: light that isnt there isnt drawn, dimmer light is thinner and not darker
            paint_light(cr, v->lights[led], r / level, g / level, b / level, level);
        }
    }
}

// the illumination page needs a clock while the effect moves, and none while it stands still
void update_view_preview(app_state *s)
{
    if (!s->view || !s->mouse_light)
        return;
    int effect = s->effect ? static_cast<int>(current_effect(s)) : static_cast<int>(ROCKCAT_EFFECT_FULLY_LIT);
    bool moving = (effect != ROCKCAT_EFFECT_FULLY_LIT && effect != ROCKCAT_EFFECT_OFF) ||
                  (effect != ROCKCAT_EFFECT_OFF && s->color_flow && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->color_flow)));
    if (moving && !s->preview_source)
        s->preview_source = g_timeout_add(40, preview_tick, s);
    else if (!moving && s->preview_source) {
        g_source_remove(s->preview_source);
        s->preview_source = 0;
    }
    redraw_light(s);
}

gboolean on_draw_view_lights(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->view)
        return FALSE;
    fit(s->view, s->view->height, w, cr);
    cairo_set_source_surface(cr, s->view->picture, 0, 0);
    cairo_paint(cr);
    draw_view_lights(s, cr, true);
    return FALSE;
}

gboolean on_draw_view_buttons(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const device_view *v = s->view;
    if (!v)
        return FALSE;
    fit(v, v->buttons_height, w, cr);
    cairo_rectangle(cr, 0, 0, v->width, v->buttons_height);
    cairo_clip(cr);
    cairo_set_source_surface(cr, v->picture, 0, 0);
    cairo_paint(cr);
    // the lights in their colours here too, a mouse with a red wheel on one page and a blue one on the next looks broken
    if (!s->led_color.empty())
        draw_view_lights(s, cr, false);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, v->marker * 0.44);
    for (size_t i = 0; i < v->marks.size(); ++i) {
        const device_view::mark &m = v->marks[i];
        if (!m.number)
            continue;
        bool on = static_cast<int>(i) == s->highlighted_button;
        // (the text of the marker before left its pen somewhere, without this cairo draws a line from there to the circle)
        cairo_new_path(cr);
        cairo_arc(cr, m.x, m.y, v->marker / 2 - 1, 0, 2 * M_PI);
        if (on)
            cairo_set_source_rgba(cr, 0.32, 0.67, 0.87, 0.85);
        else
            cairo_set_source_rgba(cr, 0, 0, 0, 0.62);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, on ? 3 : 1.5);
        if (on)
            cairo_set_source_rgb(cr, 1, 1, 1);
        else
            cairo_set_source_rgb(cr, 0.32, 0.67, 0.87);
        cairo_stroke(cr);
        char text[8];
        std::snprintf(text, sizeof text, "%d", m.number % 1000);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, text, &ext);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, m.x - ext.width / 2 - ext.x_bearing, m.y - ext.height / 2 - ext.y_bearing);
        cairo_show_text(cr, text);
    }
    return FALSE;
}

// pointing at a marker lights it up, and with it the row of that button in the list
gboolean on_view_motion(GtkWidget *w, GdkEventMotion *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->view)
        return FALSE;
    int hit = marker_at(s, w, e->x, e->y);
    set_highlight(s, hit);
    GdkWindow *win = gtk_widget_get_window(w);
    if (win) {
        GdkCursor *hand = hit >= 0 ? gdk_cursor_new_from_name(gdk_window_get_display(win), "pointer") : nullptr;
        gdk_window_set_cursor(win, hand);
        if (hand)
            g_object_unref(hand);
    }
    return FALSE;
}

gboolean on_view_leave(GtkWidget *, GdkEventCrossing *, gpointer data)
{
    set_highlight(static_cast<app_state *>(data), -1);
    return FALSE;
}

// a click on a marker puts the keyboard focus on what that button does, so the next thing typed or scrolled changes it
gboolean on_view_click(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->view)
        return FALSE;
    int hit = marker_at(s, w, e->x, e->y);
    if (hit >= 0 && static_cast<size_t>(hit) < s->buttons.size() && s->buttons[static_cast<size_t>(hit)].combo)
        gtk_widget_grab_focus(s->buttons[static_cast<size_t>(hit)].combo);
    return FALSE;
}

} // namespace gui
