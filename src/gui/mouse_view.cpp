// the Kone AIMO drawn with cairo, once with its lights and once with its buttons.
// goes away when the device pictures out of Swarm take over

#include "app.h"

namespace gui {

// Shape of a Kone-style mouse seen from the top, in a 200x300 box.
void mouse_path(cairo_t *cr)
{
    cairo_new_path(cr);
    cairo_move_to(cr, 100, 6);
    cairo_curve_to(cr, 150, 6, 176, 40, 180, 95);
    cairo_curve_to(cr, 184, 140, 196, 170, 192, 215);
    cairo_curve_to(cr, 188, 268, 150, 296, 100, 296);
    cairo_curve_to(cr, 50, 296, 12, 268, 8, 215);
    cairo_curve_to(cr, 4, 170, 16, 140, 20, 95);
    cairo_curve_to(cr, 24, 40, 50, 6, 100, 6);
    cairo_close_path(cr);
}

void set_led_source(cairo_t *cr, const rockcat_color &c, bool on, double extra_alpha)
{
    if (!on) {
        cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 1.0);
        return;
    }
    cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                          (0.25 + 0.75 * c.a / 255.0) * extra_alpha);
}

struct led_shape {
    double x, y, w, h;
};

// LED zones in the order the mouse numbers them: wheel, left/right front, two bars.
constexpr led_shape LED_SHAPES[KONE_LEDS] = {
    {92, 52, 16, 46},   // 0 wheel
    {28, 70, 10, 30},   // 1 left front
    {20, 108, 10, 26},  // 2 left bar
    {18, 140, 10, 26},  // 3
    {18, 172, 10, 26},  // 4
    {162, 70, 10, 30},  // 5 right front
    {170, 108, 10, 26}, // 6 right bar
    {172, 140, 10, 26}, // 7
    {172, 172, 10, 26}, // 8
    {20, 204, 10, 26},  // 9 left bar
    {170, 204, 10, 26}, // 10 right bar
};

void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void draw_mouse_body(cairo_t *cr)
{
    cairo_pattern_t *body = cairo_pattern_create_linear(0, 0, 200, 300);
    cairo_pattern_add_color_stop_rgb(body, 0, 0.23, 0.23, 0.23);
    cairo_pattern_add_color_stop_rgb(body, 1, 0.08, 0.08, 0.08);
    mouse_path(cr);
    cairo_set_source(cr, body);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(body);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);

    // button split and thumb area
    cairo_set_source_rgba(cr, 0, 0, 0, 0.8);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, 100, 8);
    cairo_line_to(cr, 100, 46);
    cairo_move_to(cr, 100, 104);
    cairo_line_to(cr, 100, 128);
    cairo_move_to(cr, 24, 120);
    cairo_curve_to(cr, 60, 130, 140, 130, 176, 120);
    cairo_stroke(cr);

    // DPI buttons behind the wheel
    for (int i = 0; i < 2; ++i) {
        rounded_rect(cr, 92, 106 + i * 12, 16, 10, 2);
        cairo_set_source_rgb(cr, 0.16, 0.16, 0.16);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);
    }
}

void scale_to_widget(GtkWidget *w, cairo_t *cr)
{
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double s = std::fmin(W / 210.0, H / 310.0);
    cairo_translate(cr, (W - 200 * s) / 2, (H - 300 * s) / 2);
    cairo_scale(cr, s, s);
}

gboolean on_draw_lighting(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    // this drawing is of a Kone AIMO and reads its eleven colour buttons, anything else has no business here
    if (s->led_color.size() != KONE_LEDS || !s->brightness)
        return FALSE;
    scale_to_widget(w, cr);
    draw_mouse_body(cr);
    rockcat_color frame[KONE_LEDS];
    std::fill(frame, frame + KONE_LEDS, rockcat_color{255, 0, 0, 0});
    uint8_t effect = current_effect(s);
    bool software = engine_rendered(s);
    if (software) {
        if (rockcat_profile *ps = preview_profile(s)) {
            rockcat_profile_set_int(ps, ROCKCAT_SET_BRIGHTNESS, 255); // preview shape of the effect, brightness is applied by alpha below
            rockcat_profile_render(ps, effect, s->preview_t, 0.0, frame, KONE_LEDS);
            rockcat_profile_free(ps);
        }
    }
    double level = gtk_range_get_value(GTK_RANGE(s->brightness)) / 255.0;
    for (int i = 0; i < KONE_LEDS; ++i) {
        const led_shape &l = LED_SHAPES[i];
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        rockcat_color c = software ? frame[i] : from_rgba(rgba);
        c.a = static_cast<uint8_t>(c.a * (0.15 + 0.85 * level));
        bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->led_enable[i])) && effect != ROCKCAT_EFFECT_OFF;
        // glow
        if (on) {
            cairo_pattern_t *glow = cairo_pattern_create_radial(l.x + l.w / 2, l.y + l.h / 2, 1,
                                                                l.x + l.w / 2, l.y + l.h / 2, l.h);
            cairo_pattern_add_color_stop_rgba(glow, 0, c.r / 255.0, c.g / 255.0, c.b / 255.0, 0.45 * c.a / 255.0);
            cairo_pattern_add_color_stop_rgba(glow, 1, c.r / 255.0, c.g / 255.0, c.b / 255.0, 0);
            cairo_set_source(cr, glow);
            cairo_arc(cr, l.x + l.w / 2, l.y + l.h / 2, l.h, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(glow);
        }
        rounded_rect(cr, l.x, l.y, l.w, l.h, 3);
        set_led_source(cr, c, on);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        cairo_set_font_size(cr, 8);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
        double tx = i == 0 ? l.x + 4 : (l.x < 100 ? l.x - 12 : l.x + l.w + 3);
        cairo_move_to(cr, tx, l.y + l.h / 2 + 3);
        cairo_show_text(cr, num);
    }
    return FALSE;
}

// Anchor points for the physical buttons, used to highlight a selection.
constexpr double BUTTON_ANCHORS[KONE_BUTTONS][2] = {
    {55, 45},   {145, 45},  {100, 75},  {84, 75},  {116, 75}, {100, 56},
    {100, 94},  {14, 150},  {14, 185},  {14, 118}, {100, 111}, {100, 123},
};

gboolean on_draw_buttons(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    scale_to_widget(w, cr);
    draw_mouse_body(cr);
    rounded_rect(cr, 92, 52, 16, 46, 6);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_fill(cr);
    // thumb buttons
    for (int i = 0; i < 3; ++i) {
        rounded_rect(cr, 6, 108 + i * 34, 10, 24, 3);
        cairo_set_source_rgb(cr, 0.14, 0.14, 0.14);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);
    }
    for (int i = 0; i < KONE_BUTTONS; ++i) {
        bool hl = s->highlighted_button == i;
        cairo_arc(cr, BUTTON_ANCHORS[i][0], BUTTON_ANCHORS[i][1], hl ? 8 : 6, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.40, 0.76, 0.97, hl ? 0.95 : 0.55);
        cairo_fill(cr);
        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        cairo_set_font_size(cr, 7);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, num, &ext);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_move_to(cr, BUTTON_ANCHORS[i][0] - ext.width / 2 - ext.x_bearing,
                      BUTTON_ANCHORS[i][1] + ext.height / 2);
        cairo_show_text(cr, num);
    }
    return FALSE;
}

} // namespace gui
