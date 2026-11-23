// the keyboard on the illumination page: ROCCATs picture of the board with every key lit in the colour it has.
// click keys to pick them, then choose a colour and it goes onto the picked ones (or onto all, if none is picked)
// where the keys sit comes out of the key tables of the library, the pictures out of assets/.../keyboards/<product id>/

#include "app.h"

namespace gui {

struct keyboard_art {
    // body, then per picture set (0 = iso boards, 1 = the US one) the keycaps and what is printed on them
    // (some boards have a US body of their own, without it the one body is used for both)
    cairo_surface_t *body = nullptr, *body_us = nullptr, *caps[2] = {}, *legends[2] = {};
    int picture_w = 0, picture_h = 0; // size of the pictures
    int space_w = 0, space_h = 0;     // the coordinates the key table uses, the pictures get stretched onto that
    int region = 0;                   // which regional layout is shown
    std::vector<int> regions;
    const rockcat_key *keys = nullptr;
    int key_count = 0;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the part of the space that gets shown
    double board[4] = {0, 0, 0, 0};    // where the board sits on its picture, in key table coordinates. all zero = unknown
    std::vector<char> picked;          // by led
    // shift + drag: a box like the one for picking files, everything it touches gets picked on top of what was picked before
    bool boxing = false;
    double box[4] = {0, 0, 0, 0};      // where it started and where the pointer is, in key table coordinates
    std::vector<char> before;          // what was picked when the box started
};

namespace {

// box = gets the part of the picture that isnt see through (left, top, right, bottom in pixels), for finding the board on it
cairo_surface_t *load(int pid, const char *name, int *box = nullptr)
{
    char path[128];
    std::snprintf(path, sizeof path, "/org/rockcat/swarm/assets/keyboards/%04x/%s.png", pid, name);
    GdkPixbuf *pix = gdk_pixbuf_new_from_resource(path, nullptr);
    if (!pix)
        return nullptr;
    if (box && gdk_pixbuf_get_has_alpha(pix) && gdk_pixbuf_get_n_channels(pix) == 4) {
        int w = gdk_pixbuf_get_width(pix), h = gdk_pixbuf_get_height(pix), stride = gdk_pixbuf_get_rowstride(pix);
        const guchar *px = gdk_pixbuf_read_pixels(pix);
        box[0] = w, box[1] = h, box[2] = 0, box[3] = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (px[y * stride + x * 4 + 3] > 24) {
                    box[0] = std::min(box[0], x);
                    box[1] = std::min(box[1], y);
                    box[2] = std::max(box[2], x + 1);
                    box[3] = std::max(box[3], y + 1);
                }
    }
    cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(pix, 1, nullptr);
    g_object_unref(pix);
    return surface;
}

void pick_region(app_state *s, int region)
{
    keyboard_art *k = s->kb;
    k->region = region;
    k->key_count = rockcat_keyboard_keys(s->caps, region, &k->keys);
    // what gets shown: the keys and a bit of board around them, the pictures are mostly empty space
    double x0 = 1e9, y0 = 1e9, x1 = 0, y1 = 0;
    for (int i = 0; i < k->key_count; ++i) {
        x0 = std::min<double>(x0, k->keys[i].x);
        y0 = std::min<double>(y0, k->keys[i].y);
        x1 = std::max<double>(x1, k->keys[i].x + k->keys[i].w);
        y1 = std::max<double>(y1, k->keys[i].y + k->keys[i].h);
    }
    // the whole board, palm rest and all, when the picture says where it is. keys can stick out over it and count too
    double pad = 26;
    if (k->board[2] > k->board[0]) {
        x0 = std::min(x0, k->board[0]);
        // (the picture has the cable sticking out at the top, that part is not worth the room it takes)
        y0 = std::max(k->board[1], y0 - 30);
        x1 = std::max(x1, k->board[2]);
        y1 = std::max(y1, k->board[3]);
        pad = 6;
    }
    k->x0 = std::max(0.0, x0 - pad);
    k->y0 = std::max(0.0, y0 - pad);
    k->x1 = std::min<double>(k->space_w, x1 + pad);
    k->y1 = std::min<double>(k->space_h, y1 + pad);
}

// leaves cairo in the coordinates of the key table. returns the scale, and where the shown part starts on the widget
double fit(const keyboard_art *k, GtkWidget *w, cairo_t *cr, double *left = nullptr, double *top = nullptr)
{
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double cw = k->x1 - k->x0, ch = k->y1 - k->y0;
    double scale = std::min(W / cw, H / ch);
    double x = (W - cw * scale) / 2, y = (H - ch * scale) / 2;
    if (cr) {
        cairo_translate(cr, x, y);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, -k->x0, -k->y0);
    }
    if (left)
        *left = x;
    if (top)
        *top = y;
    return scale;
}

void picture(const keyboard_art *k, cairo_t *cr, cairo_surface_t *surface)
{
    if (!surface)
        return;
    cairo_save(cr);
    cairo_scale(cr, static_cast<double>(k->space_w) / k->picture_w, static_cast<double>(k->space_h) / k->picture_h);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

// a spot on the widget in the coordinates of the key table
void to_keys(const keyboard_art *k, GtkWidget *w, double px, double py, double *x, double *y)
{
    double left = 0, top = 0, scale = fit(k, w, nullptr, &left, &top);
    *x = (px - left) / scale + k->x0;
    *y = (py - top) / scale + k->y0;
}

const rockcat_key *key_at(app_state *s, GtkWidget *w, double px, double py)
{
    const keyboard_art *k = s->kb;
    double x = 0, y = 0;
    to_keys(k, w, px, py, &x, &y);
    for (int i = 0; i < k->key_count; ++i)
        if (x >= k->keys[i].x && x < k->keys[i].x + k->keys[i].w && y >= k->keys[i].y && y < k->keys[i].y + k->keys[i].h)
            return &k->keys[i];
    return nullptr;
}

bool has_light(const app_state *s, const rockcat_key &key)
{
    return key.led >= 0 && key.led < s->caps->led_count;
}

int picked_count(const keyboard_art *k)
{
    return static_cast<int>(std::count(k->picked.begin(), k->picked.end(), 1));
}

void say_picked(app_state *s)
{
    if (!s->kb_hint)
        return;
    if (s->caps && s->caps->led_count == 0) {
        gtk_label_set_text(GTK_LABEL(s->kb_hint), "One light for the whole board. The picture shows what effect and brightness do to it");
        return;
    }
    int n = picked_count(s->kb);
    char text[160];
    if (n == 0)
        std::snprintf(text, sizeof text, "No key picked: a colour goes onto all keys. Click keys to pick them, drag over several, or hold shift and drag a box.");
    else
        std::snprintf(text, sizeof text, "%d key%s picked: a colour goes onto %s.", n, n == 1 ? "" : "s", n == 1 ? "it" : "them");
    gtk_label_set_text(GTK_LABEL(s->kb_hint), text);
}

} // namespace

keyboard_art *keyboard_load(app_state *s)
{
    int ids[16];
    int n = rockcat_keyboard_regions(s->caps, ids, 16);
    if (n <= 0)
        return nullptr;
    auto *k = new keyboard_art;
    k->regions.assign(ids, ids + std::min(n, 16));
    rockcat_keyboard_size(s->caps, &k->space_w, &k->space_h);
    int pid = s->info.product_id;
    int box[4] = {0, 0, 0, 0};
    k->body = load(pid, "body", box);
    k->body_us = load(pid, "body-us");
    k->caps[0] = load(pid, "keycaps-eu");
    k->legends[0] = load(pid, "legends-eu");
    k->caps[1] = load(pid, "keycaps-us");
    k->legends[1] = load(pid, "legends-us");
    cairo_surface_t *any = k->body ? k->body : k->caps[0];
    k->picture_w = any ? cairo_image_surface_get_width(any) : k->space_w;
    k->picture_h = any ? cairo_image_surface_get_height(any) : k->space_h;
    if (k->body && box[2] > box[0])
        for (int i = 0; i < 4; ++i)
            k->board[i] = box[i] * static_cast<double>(i % 2 ? k->space_h : k->space_w) / (i % 2 ? k->picture_h : k->picture_w);
    k->picked.assign(static_cast<size_t>(std::max(s->caps->led_count, 1)), 0);
    s->kb = k;
    // every keyboard on the desk this was written at is swiss, which is the german board with other prints. so DE first
    int region = k->regions[0];
    for (int id : k->regions)
        if (id == 1)
            region = id;
    pick_region(s, region);
    return k;
}

void keyboard_free(keyboard_art *k)
{
    if (!k)
        return;
    for (cairo_surface_t *surface : {k->body, k->body_us, k->caps[0], k->caps[1], k->legends[0], k->legends[1]})
        if (surface)
            cairo_surface_destroy(surface);
    delete k;
}

gboolean on_draw_keyboard(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const keyboard_art *k = s->kb;
    if (!k || !s->profile)
        return FALSE;
    fit(k, w, cr);
    const int set = k->region == 0 ? 1 : 0;
    // one light for the whole board (Sova membrane, MK Glow): every key on the picture is a "led" of its own here and they
    // all show the same, so the board still looks like what it does
    const bool whole = s->caps->led_count == 0;
    int leds = s->caps->led_count;
    if (whole)
        for (int i = 0; i < k->key_count; ++i)
            leds = std::max(leds, k->keys[i].led + 1);

    // what every key shows right now. the library knows how blinking, breathing and heartbeat go, anything else stands still
    std::vector<rockcat_color> frame(static_cast<size_t>(leds), rockcat_color{255, 0, 0, 0});
    std::vector<char> on(static_cast<size_t>(leds), 0);
    int effect = s->effect ? static_cast<int>(current_effect(s)) : static_cast<int>(ROCKCAT_EFFECT_FULLY_LIT);
    bool moving = effect == ROCKCAT_EFFECT_BLINKING || effect == ROCKCAT_EFFECT_BREATHING || effect == ROCKCAT_EFFECT_HEARTBEAT;
    if (whole) {
        // our own drawing of the firmware effects, the rhythm is a guess. a board without any light setting (Ryos MK, TKL)
        // has no light at all and stays dark
        const double t = s->preview_t;
        double f = has_setting(s, ROCKCAT_SET_BRIGHTNESS) || has_setting(s, ROCKCAT_SET_EFFECT) ? 1 : 0;
        switch (effect) {
        case ROCKCAT_EFFECT_OFF:       f = 0; break;
        case ROCKCAT_EFFECT_BLINKING:  f *= std::fmod(t, 1.0) < 0.5; break;
        case ROCKCAT_EFFECT_BREATHING: f *= 0.5 - 0.5 * std::cos(2 * M_PI * t / 3); break;
        case ROCKCAT_EFFECT_HEARTBEAT: {
            double p = std::fmod(t, 1.4);
            f *= std::max(std::exp(-std::pow((p - 0.15) / 0.07, 2)), 0.7 * std::exp(-std::pow((p - 0.42) / 0.07, 2)));
            break;
        }
        case ROCKCAT_EFFECT_IMPULSE:   f *= std::exp(-3 * std::fmod(t, 1.5)); break;
        default:                       break;
        }
        const rockcat_color glow{static_cast<uint8_t>(255 * f), 0x20, 0x60, 0xff};
        for (int i = 0; i < leds; ++i) {
            frame[static_cast<size_t>(i)] = glow;
            on[static_cast<size_t>(i)] = glow.a > 6;
        }
    } else if (rockcat_profile *p = preview_profile(s)) {
        rockcat_profile_set_int(p, ROCKCAT_SET_BRIGHTNESS, 255);
        rockcat_profile_render(p, effect == ROCKCAT_EFFECT_OFF ? effect : moving ? effect : static_cast<int>(ROCKCAT_EFFECT_FULLY_LIT),
                               s->preview_t, 0.0, frame.data(), leds);
        rockcat_profile_free(p);
    }
    for (int i = 0; i < leds && !whole; ++i) {
        int en = 0;
        rockcat_profile_get_led(s->profile, i, nullptr, &en);
        on[static_cast<size_t>(i)] = en && (frame[static_cast<size_t>(i)].r || frame[static_cast<size_t>(i)].g || frame[static_cast<size_t>(i)].b);
    }
    // an effect the engine draws: when the profile showing is the one thats active, the picture simply shows what the
    // engine is sending to the keyboard right now. for another profile (or before APPLY) it stays the plain colours
    if (!whole && pc_only(s, effect)) {
        rockcat_engine_status st{};
        rockcat_engine *engine = keyboard_engine(s);
        if (engine)
            rockcat_engine_get_status(engine, &st);
        if (engine && st.rendering && st.profile == s->edit_profile && st.effect == effect &&
            rockcat_engine_get_frame(engine, frame.data(), leds) == leds)
            for (int i = 0; i < leds; ++i)
                on[static_cast<size_t>(i)] = frame[static_cast<size_t>(i)].r || frame[static_cast<size_t>(i)].g || frame[static_cast<size_t>(i)].b;
    }
    // the Easy-Shift layer: just its colours, standing still
    if (s->kb_layer == 1 && !whole) {
        for (int i = 0; i < leds && i < static_cast<int>(s->es_colors.size()); ++i) {
            const rockcat_color &c = s->es_colors[static_cast<size_t>(i)];
            frame[static_cast<size_t>(i)] = rockcat_color{255, c.r, c.g, c.b};
            on[static_cast<size_t>(i)] = c.a != 0;
        }
    }
    // the brightness of the board dims everything alike. never all the way down, a black picture tells nobody anything
    double level = s->brightness ? 0.25 + 0.75 * gtk_range_get_value(GTK_RANGE(s->brightness)) / 255.0 : 1;

    // the idle preview: for a few seconds the picture does what the keyboard would do when nobody touches it.
    // our own drawing of it, how the firmware really times these nobody measured
    if (s->idle_preview_t0 >= 0 && s->idle_style && s->kb_layer == 0) {
        const double t = s->preview_t - s->idle_preview_t0;
        const int style = combo_type(s->idle_style);
        const double low = s->idle_brightness ? gtk_range_get_value(GTK_RANGE(s->idle_brightness)) / 255.0 : 0;
        for (int i = 0; i < leds; ++i) {
            // every key gets a number between 0 and 1 that is its own and looks random
            double own = std::fmod(std::sin(i * 12.9898) * 43758.5453, 1.0);
            own = own < 0 ? own + 1 : own;
            double f = 1;
            if (style == ROCKCAT_IDLE_STARLIT_SKY) {
                // dark, and now and then a key lights up and fades
                double phase = std::fmod(t * (0.25 + 0.2 * own) + own * 7, 1.0);
                f = phase < 0.25 ? std::sin(phase / 0.25 * M_PI) : 0;
                f *= std::min(1.0, t);
                f = std::max(f, 1 - std::min(1.0, t * 2));
            } else if (style == ROCKCAT_IDLE_FALL_ASLEEP) {
                // the keys go out one after the other
                f = 1 - std::min(1.0, std::max(0.0, (t - own * 3.5) * 2.5));
            } else {
                // everything goes down to the idle brightness together
                f = 1 - (1 - low) * std::min(1.0, t / 1.2);
            }
            rockcat_color &c = frame[static_cast<size_t>(i)];
            c.a = static_cast<uint8_t>(c.a * std::max(0.0, std::min(1.0, f)));
            if (c.a < 6)
                on[static_cast<size_t>(i)] = 0;
        }
    }

    picture(k, cr, set == 1 && k->body_us ? k->body_us : k->body);
    // light under the keys
    for (int i = 0; i < k->key_count; ++i) {
        const rockcat_key &key = k->keys[i];
        if (!(key.led >= 0 && key.led < leds) || !on[static_cast<size_t>(key.led)])
            continue;
        const rockcat_color &c = frame[static_cast<size_t>(key.led)];
        cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, 0.55 * level * c.a / 255.0);
        rounded_rect(cr, key.x - 1.5, key.y - 1.5, key.w + 3, key.h + 3, 3);
        cairo_fill(cr);
    }
    picture(k, cr, k->caps[set]);
    // what is printed on the keys, each in the colour of its key. without ROCCATs pictures there is no print,
    // then the name of the key gets written on it
    for (int i = 0; i < k->key_count; ++i) {
        const rockcat_key &key = k->keys[i];
        bool lit = (key.led >= 0 && key.led < leds) && on[static_cast<size_t>(key.led)];
        double r = 0.33, g = 0.34, b = 0.36;
        if (lit) {
            const rockcat_color &c = frame[static_cast<size_t>(key.led)];
            double a = level * c.a / 255.0;
            r = c.r / 255.0 * a;
            g = c.g / 255.0 * a;
            b = c.b / 255.0 * a;
        }
        cairo_save(cr);
        cairo_rectangle(cr, key.x, key.y, key.w, key.h);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, r, g, b);
        if (k->legends[set]) {
            cairo_scale(cr, static_cast<double>(k->space_w) / k->picture_w, static_cast<double>(k->space_h) / k->picture_h);
            cairo_mask_surface(cr, k->legends[set], 0, 0);
        }
        cairo_restore(cr);
    }
    // the keys that are picked, and the one under the pointer
    for (int i = 0; i < k->key_count; ++i) {
        const rockcat_key &key = k->keys[i];
        bool picked = (key.led >= 0 && key.led < leds) && k->picked[static_cast<size_t>(key.led)];
        bool hover = s->kb_hover == key.led;
        if (!picked && !hover)
            continue;
        rounded_rect(cr, key.x + 0.75, key.y + 0.75, key.w - 1.5, key.h - 1.5, 3);
        if (picked) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
            cairo_fill_preserve(cr);
        }
        cairo_set_line_width(cr, picked ? 1.6 : 1);
        cairo_set_source_rgba(cr, 1, 1, 1, picked ? 0.95 : 0.55);
        cairo_stroke(cr);
    }
    if (k->boxing) {
        cairo_rectangle(cr, std::min(k->box[0], k->box[2]), std::min(k->box[1], k->box[3]), std::fabs(k->box[2] - k->box[0]), std::fabs(k->box[3] - k->box[1]));
        cairo_set_source_rgba(cr, 0.32, 0.67, 0.87, 0.22);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 1);
        cairo_set_source_rgba(cr, 0.32, 0.67, 0.87, 0.95);
        cairo_stroke(cr);
    }
    return FALSE;
}

// the picture of the board is always as tall as its width says (and the page scrolls if that doesnt fit). that way the
// board has the same size on every page that shows it, whatever else sits above or next to it
static void on_board_allocate(GtkWidget *w, GdkRectangle *a, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    int want = static_cast<int>(a->width * keyboard_aspect(s) + 0.5), now = 0;
    gtk_widget_get_size_request(w, nullptr, &now);
    if (std::abs(want - now) <= 2)
        return;
    // the board getting taller can make the page grow a scrollbar, which makes the board narrower, so it gets shorter, the
    // scrollbar goes away, wider again, and round it goes (any hover or relayout kicks it off, it looked like an earthquake).
    // going straight back to the height it had a moment ago is that loop, so it stays where it is
    const int before = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "rc-board-before"));
    const gint64 when = static_cast<gint64>(reinterpret_cast<intptr_t>(g_object_get_data(G_OBJECT(w), "rc-board-when")));
    const gint64 t = g_get_monotonic_time() / 1000;
    if (before && std::abs(want - before) <= 2 && t - when < 1000)
        return;
    g_object_set_data(G_OBJECT(w), "rc-board-before", GINT_TO_POINTER(now));
    g_object_set_data(G_OBJECT(w), "rc-board-when", reinterpret_cast<gpointer>(static_cast<intptr_t>(t)));
    gtk_widget_set_size_request(w, 480, want);
}

void keyboard_keep_shape(app_state *s, GtkWidget *w)
{
    gtk_widget_set_vexpand(w, FALSE);
    g_signal_connect(w, "size-allocate", G_CALLBACK(on_board_allocate), s);
}

// the board for other pages (key assignment): body, keycaps, and the prints in whatever colour print() says.
// leaves cairo in key table coordinates, so the caller can draw onto the keys
void keyboard_draw_board(app_state *s, GtkWidget *w, cairo_t *cr, const std::function<void(const rockcat_key &, double *)> &print)
{
    const keyboard_art *k = s->kb;
    if (!k)
        return;
    fit(k, w, cr);
    const int set = k->region == 0 ? 1 : 0;
    picture(k, cr, set == 1 && k->body_us ? k->body_us : k->body);
    picture(k, cr, k->caps[set]);
    for (int i = 0; i < k->key_count; ++i) {
        const rockcat_key &key = k->keys[i];
        double rgb[3] = {0.7, 0.7, 0.72};
        print(key, rgb);
        cairo_save(cr);
        cairo_rectangle(cr, key.x, key.y, key.w, key.h);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
        if (k->legends[set]) {
            cairo_scale(cr, static_cast<double>(k->space_w) / k->picture_w, static_cast<double>(k->space_h) / k->picture_h);
            cairo_mask_surface(cr, k->legends[set], 0, 0);
        }
        cairo_restore(cr);
    }
}

const rockcat_key *keyboard_key_at(app_state *s, GtkWidget *w, double x, double y)
{
    return s->kb ? key_at(s, w, x, y) : nullptr;
}

int keyboard_key_count(app_state *s, const rockcat_key **keys)
{
    if (!s->kb)
        return 0;
    *keys = s->kb->keys;
    return s->kb->key_count;
}

double keyboard_aspect(app_state *s)
{
    return s->kb ? (s->kb->y1 - s->kb->y0) / (s->kb->x1 - s->kb->x0) : 0.4;
}

// while the effect moves the picture needs a clock
void update_keyboard_preview(app_state *s)
{
    if (!s->kb || !s->kb_view)
        return;
    int effect = s->effect ? static_cast<int>(current_effect(s)) : static_cast<int>(ROCKCAT_EFFECT_FULLY_LIT);
    bool moving = effect == ROCKCAT_EFFECT_BLINKING || effect == ROCKCAT_EFFECT_BREATHING || effect == ROCKCAT_EFFECT_HEARTBEAT ||
                  effect == ROCKCAT_EFFECT_IMPULSE;
    moving = moving || s->idle_preview_t0 >= 0 || pc_only(s, effect);
    if (moving && !s->preview_source)
        s->preview_source = g_timeout_add(40, preview_tick, s);
    else if (!moving && s->preview_source) {
        g_source_remove(s->preview_source);
        s->preview_source = 0;
    }
    gtk_widget_queue_draw(s->kb_view);
}

// what is picked while a box is being dragged: what was picked before, plus every key the box touches
static void box_pick(app_state *s)
{
    keyboard_art *k = s->kb;
    double x0 = std::min(k->box[0], k->box[2]), x1 = std::max(k->box[0], k->box[2]);
    double y0 = std::min(k->box[1], k->box[3]), y1 = std::max(k->box[1], k->box[3]);
    k->picked = k->before;
    for (int i = 0; i < k->key_count; ++i) {
        const rockcat_key &key = k->keys[i];
        if (has_light(s, key) && key.x < x1 && key.x + key.w > x0 && key.y < y1 && key.y + key.h > y0)
            k->picked[static_cast<size_t>(key.led)] = 1;
    }
}

gboolean on_keyboard_press(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->kb || e->button != 1)
        return FALSE;
    if (e->state & GDK_SHIFT_MASK) {
        // shift held: a box gets dragged, and whatever it touches joins what is picked already
        keyboard_art *k = s->kb;
        k->boxing = true;
        k->before = k->picked;
        to_keys(k, w, e->x, e->y, &k->box[0], &k->box[1]);
        k->box[2] = k->box[0];
        k->box[3] = k->box[1];
        s->kb_drag = -1;
        box_pick(s);
        say_picked(s);
        gtk_widget_queue_draw(w);
        return TRUE;
    }
    const rockcat_key *key = key_at(s, w, e->x, e->y);
    if (key && has_light(s, *key)) {
        char &p = s->kb->picked[static_cast<size_t>(key->led)];
        p = !p;
        // dragging on from here does to the other keys what this click did to the first one
        s->kb_drag = p ? 1 : 0;
    } else {
        // a click next to the keys lets go of all of them
        std::fill(s->kb->picked.begin(), s->kb->picked.end(), 0);
        s->kb_drag = -1;
    }
    say_picked(s);
    gtk_widget_queue_draw(w);
    return TRUE;
}

gboolean on_keyboard_release(GtkWidget *w, GdkEventButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->kb_drag = -1;
    if (s->kb && s->kb->boxing) {
        s->kb->boxing = false;
        gtk_widget_queue_draw(w);
    }
    return FALSE;
}

gboolean on_keyboard_motion(GtkWidget *w, GdkEventMotion *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->kb)
        return FALSE;
    if (s->kb->boxing && (e->state & GDK_BUTTON1_MASK)) {
        to_keys(s->kb, w, e->x, e->y, &s->kb->box[2], &s->kb->box[3]);
        box_pick(s);
        say_picked(s);
        gtk_widget_queue_draw(w);
        return FALSE;
    }
    const rockcat_key *key = key_at(s, w, e->x, e->y);
    int hover = key && has_light(s, *key) ? key->led : -1;
    bool changed = hover != s->kb_hover;
    s->kb_hover = hover;
    if (hover >= 0 && s->kb_drag >= 0 && (e->state & GDK_BUTTON1_MASK) && s->kb->picked[static_cast<size_t>(hover)] != s->kb_drag) {
        s->kb->picked[static_cast<size_t>(hover)] = static_cast<char>(s->kb_drag);
        changed = true;
        say_picked(s);
    }
    if (changed) {
        gtk_widget_set_tooltip_text(w, key ? key->name : nullptr);
        gtk_widget_queue_draw(w);
    }
    return FALSE;
}

gboolean on_keyboard_leave(GtkWidget *w, GdkEventCrossing *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->kb_hover != -1) {
        s->kb_hover = -1;
        gtk_widget_queue_draw(w);
    }
    return FALSE;
}

// the picked keys, or all of them when none is picked
static std::vector<int> targets(app_state *s)
{
    std::vector<int> leds;
    bool any = picked_count(s->kb) > 0;
    for (int i = 0; i < s->caps->led_count; ++i)
        if (!any || s->kb->picked[static_cast<size_t>(i)])
            leds.push_back(i);
    return leds;
}

// a colour goes into the profile right away (onto the picked keys), APPLY sends it to the keyboard
static void paint_picked(app_state *s, rockcat_color c)
{
    if (!s->kb || !s->profile || s->updating)
        return;
    if (s->kb_layer == 1) {
        for (int led : targets(s))
            if (led < static_cast<int>(s->es_colors.size()))
                s->es_colors[static_cast<size_t>(led)] = c;
        shift_layer_save(s);
        gtk_widget_queue_draw(s->kb_view);
        return;
    }
    for (int led : targets(s))
        rockcat_profile_set_led(s->profile, led, c, 1);
    gtk_widget_queue_draw(s->kb_view);
    schedule_auto_apply(s);
}

void on_keyboard_colour(GtkColorButton *button, gpointer data)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    rockcat_color c = from_rgba(rgba);
    c.a = 255;
    paint_picked(static_cast<app_state *>(data), c);
}

// the LIGHT ON button of a one colour board. its blue, the only thing it can do
static void on_keyboard_light_on(GtkButton *, gpointer data)
{
    paint_picked(static_cast<app_state *>(data), rockcat_color{255, 0x20, 0x60, 0xff});
}

void on_keyboard_dark(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->kb || !s->profile)
        return;
    if (s->kb_layer == 1) {
        for (int led : targets(s))
            if (led < static_cast<int>(s->es_colors.size()))
                s->es_colors[static_cast<size_t>(led)].a = 0;
        shift_layer_save(s);
        gtk_widget_queue_draw(s->kb_view);
        return;
    }
    for (int led : targets(s)) {
        rockcat_color c{};
        rockcat_profile_get_led(s->profile, led, &c, nullptr);
        rockcat_profile_set_led(s->profile, led, c, 0);
    }
    gtk_widget_queue_draw(s->kb_view);
    schedule_auto_apply(s);
}

void on_keyboard_pick_all(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->kb)
        return;
    // all, or none if all are picked already
    bool all = picked_count(s->kb) == static_cast<int>(s->kb->picked.size());
    std::fill(s->kb->picked.begin(), s->kb->picked.end(), all ? 0 : 1);
    // slots of the key matrix without a key cant be seen and shouldnt count
    if (!all) {
        std::fill(s->kb->picked.begin(), s->kb->picked.end(), 0);
        for (int i = 0; i < s->kb->key_count; ++i)
            if (has_light(s, s->kb->keys[i]))
                s->kb->picked[static_cast<size_t>(s->kb->keys[i].led)] = 1;
    }
    say_picked(s);
    gtk_widget_queue_draw(s->kb_view);
}

// the button in the idle box: play what the keyboard does when nobody touches it, on the picture, for a few seconds
void on_idle_preview(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->kb_view)
        return;
    s->idle_preview_t0 = s->preview_t;
    update_keyboard_preview(s);
}

void on_keyboard_region(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const char *id = gtk_combo_box_get_active_id(combo);
    if (!s->kb || !id || s->updating)
        return;
    pick_region(s, std::atoi(id));
    // both pages that show the board have one of these, they say the same thing
    s->updating = true;
    for (GtkWidget *other : s->kb_regions)
        if (other != GTK_WIDGET(combo))
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(other), id);
    s->updating = false;
    if (s->kb_view)
        gtk_widget_queue_draw(s->kb_view);
    keys_refresh(s);
}

// which shape of board: ISO with the tall enter key, ANSI with the wide one. thats the only difference that shows,
// which country is printed on the caps is the same picture for all ISO boards. the keyboard itself doesnt know or care
GtkWidget *keyboard_region_box(app_state *s)
{
    keyboard_art *k = s->kb;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *region = gtk_combo_box_text_new();
    bool iso = false;
    for (int id : k->regions) {
        if (id != 0 && iso)
            continue;
        char text[8];
        std::snprintf(text, sizeof text, "%d", id);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(region), text, id == 0 ? "ANSI" : "ISO");
        iso = iso || id != 0;
    }
    char now[8];
    std::snprintf(now, sizeof now, "%d", k->region);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(region), now);
    g_object_set_data(G_OBJECT(region), "rc-no-autoapply", GINT_TO_POINTER(1));
    g_signal_connect(region, "changed", G_CALLBACK(on_keyboard_region), s);
    s->kb_regions.push_back(region);
    gtk_box_pack_end(GTK_BOX(box), region, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), label("LAYOUT", "rc-hint"), FALSE, FALSE, 0);
    return box;
}

// the Easy-Shift layer of the profile being edited, out of the config (keys never set are dark)
void shift_layer_load(app_state *s)
{
    if (!s->caps || !is_keyboard(s))
        return;
    s->es_colors.assign(static_cast<size_t>(s->caps->led_count), rockcat_color{0, 0, 0, 0});
    s->es_on = 0;
    rockcat_shift_layer_get(s->info.product_id, static_cast<uint8_t>(s->edit_profile), &s->es_on, s->es_colors.data(), s->caps->led_count);
    if (s->es_enable) {
        bool busy = s->updating;
        s->updating = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->es_enable), s->es_on != 0);
        s->updating = busy;
    }
    if (s->kb_view)
        gtk_widget_queue_draw(s->kb_view);
}

void shift_layer_save(app_state *s)
{
    rockcat_shift_layer_set(s->info.product_id, static_cast<uint8_t>(s->edit_profile), s->es_on, s->es_colors.data(),
                            static_cast<int>(s->es_colors.size()));
}

static void on_es_enable(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    s->es_on = gtk_toggle_button_get_active(b);
    shift_layer_save(s);
}

static void on_kb_layer(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    int layer = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "layer"));
    GtkWidget *other = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(b), "other"));
    if (!gtk_toggle_button_get_active(b)) {
        // one of the two is always down
        if (layer == s->kb_layer) {
            s->updating = true;
            gtk_toggle_button_set_active(b, TRUE);
            s->updating = false;
        }
        return;
    }
    s->kb_layer = layer;
    s->updating = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
    s->updating = false;
    // a layer nobody painted yet starts as a copy of the profile colours, nicer than a black board to start from
    if (layer == 1 && std::none_of(s->es_colors.begin(), s->es_colors.end(), [](const rockcat_color &c) { return c.a != 0; }))
        for (int i = 0; i < static_cast<int>(s->es_colors.size()); ++i) {
            rockcat_color c{};
            int en = 0;
            rockcat_profile_get_led(s->profile, i, &c, &en);
            s->es_colors[static_cast<size_t>(i)] = en ? rockcat_color{255, c.r, c.g, c.b} : rockcat_color{0, 0, 0, 0};
        }
    gtk_widget_set_visible(s->es_row, layer == 1);
    show_es_options(s);
    update_keyboard_preview(s);
}

// the picture with what belongs to it: which layout, pick all, the colour, lights off
GtkWidget *build_keyboard_view(app_state *s)
{
    keyboard_art *k = s->kb;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    s->kb_view = gtk_drawing_area_new();
    double aspect = (k->y1 - k->y0) / (k->x1 - k->x0);
    gtk_widget_set_size_request(s->kb_view, 480, static_cast<int>(480 * aspect));
    gtk_widget_add_events(s->kb_view, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(s->kb_view, "draw", G_CALLBACK(on_draw_keyboard), s);
    g_signal_connect(s->kb_view, "button-press-event", G_CALLBACK(on_keyboard_press), s);
    g_signal_connect(s->kb_view, "button-release-event", G_CALLBACK(on_keyboard_release), s);
    g_signal_connect(s->kb_view, "motion-notify-event", G_CALLBACK(on_keyboard_motion), s);
    g_signal_connect(s->kb_view, "leave-notify-event", G_CALLBACK(on_keyboard_leave), s);
    // second row: the hint on the left and the layout on the right, the way the key assignment page has its row
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    // the paint buttons sit up in the first row, right aligned next to the layer switch (same place as nothing on the
    // key page, so both pages keep the board at the same spot)
    GtkWidget *paint = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    // a board with one light for everything has no keys to paint, it just gets the picture (and the layout to show it in)
    if (s->caps->led_count > 0) {
        if (one_colour(s)) {
            // blue or nothing, so a picker would only lie. one button to light keys and the one next to it to put them out
            GtkWidget *on = gtk_button_new_with_label("LIGHT ON");
            gtk_widget_set_tooltip_text(on, "Switch the light of the picked keys on (all keys if none is picked). This board only does blue");
            g_signal_connect(on, "clicked", G_CALLBACK(on_keyboard_light_on), s);
            gtk_box_pack_start(GTK_BOX(paint), on, FALSE, FALSE, 0);
        } else {
            s->all_color = gtk_color_button_new();
            add_class(s->all_color, "rc-swatch");
            gtk_widget_set_tooltip_text(s->all_color, "Colour for the picked keys (all keys if none is picked)");
            g_signal_connect(s->all_color, "color-set", G_CALLBACK(on_keyboard_colour), s);
            gtk_box_pack_start(GTK_BOX(paint), label("COLOUR"), FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(paint), s->all_color, FALSE, FALSE, 0);
        }
        GtkWidget *dark = gtk_button_new_with_label("LIGHT OFF");
        gtk_widget_set_tooltip_text(dark, "Switch the light of the picked keys off (all keys if none is picked)");
        g_signal_connect(dark, "clicked", G_CALLBACK(on_keyboard_dark), s);
        gtk_box_pack_start(GTK_BOX(paint), dark, FALSE, FALSE, 0);
        GtkWidget *all = gtk_button_new_with_label("PICK ALL / NONE");
        g_signal_connect(all, "clicked", G_CALLBACK(on_keyboard_pick_all), s);
        gtk_box_pack_start(GTK_BOX(paint), all, FALSE, FALSE, 0);
    }
    s->kb_hint = label("", "rc-hint");
    // always one line, cut short in a narrow window. a wrapping hint changed its height whenever the text did (picking keys
    // with shift held flips it between the long and the short one), the page got and lost its scrollbar, the board got
    // narrower and wider with it and the whole thing shook
    gtk_label_set_ellipsize(GTK_LABEL(s->kb_hint), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(s->kb_hint), 90);
    gtk_box_pack_start(GTK_BOX(bar), s->kb_hint, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(bar), keyboard_region_box(s), FALSE, FALSE, 0);
    // which colours are being painted: the profile ones, or the ones that show while Easy-Shift is held
    GtkWidget *layers = nullptr;
    if (s->caps->features & ROCKCAT_HAS_DIRECT_LIGHT) {
        layers = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        add_class(layers, "linked");
        GtkWidget *a = gtk_toggle_button_new_with_label("NORMAL"), *b = gtk_toggle_button_new_with_label("WITH EASY-SHIFT HELD");
        g_object_set_data(G_OBJECT(a), "layer", GINT_TO_POINTER(0));
        g_object_set_data(G_OBJECT(b), "layer", GINT_TO_POINTER(1));
        g_object_set_data(G_OBJECT(a), "other", b);
        g_object_set_data(G_OBJECT(b), "other", a);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a), TRUE);
        for (GtkWidget *w : {a, b}) {
            g_object_set_data(G_OBJECT(w), "rc-no-autoapply", GINT_TO_POINTER(1));
            g_signal_connect(w, "toggled", G_CALLBACK(on_kb_layer), s);
            gtk_box_pack_start(GTK_BOX(layers), w, FALSE, FALSE, 0);
        }
        // (in the same row as the switch, a row of its own would push the board down whenever it shows)
        s->es_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(s->es_row, 12);
        s->es_enable = gtk_check_button_new_with_label("Light the keys up like this while Easy-Shift is held");
        gtk_widget_set_tooltip_text(s->es_enable, "Drawn by rockcat while it runs. Saved at once, the keyboard itself never hears of it");
        g_object_set_data(G_OBJECT(s->es_enable), "rc-no-autoapply", GINT_TO_POINTER(1));
        g_signal_connect(s->es_enable, "toggled", G_CALLBACK(on_es_enable), s);
        gtk_box_pack_start(GTK_BOX(s->es_row), s->es_enable, FALSE, FALSE, 0);
        gtk_widget_set_no_show_all(s->es_row, TRUE);
        gtk_widget_show(s->es_enable);
    }
    // the buttons first and the picture under them, so they stay in sight in a small window
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    if (layers) {
        gtk_box_pack_start(GTK_BOX(top), layers, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(top), s->es_row, FALSE, FALSE, 0);
    }
    gtk_box_pack_end(GTK_BOX(top), paint, FALSE, FALSE, 0);
    center_children(paint);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
    // the second row has the same height as the one on the key assignment page, so the board sits at the same spot
    gtk_size_group_add_widget(s->kb_rows, bar);
    // the row can end up taller than its buttons (it shares its height with the one on the key page), they stay their size
    center_children(bar);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    keyboard_keep_shape(s, s->kb_view);
    gtk_box_pack_start(GTK_BOX(box), s->kb_view, FALSE, FALSE, 0);
    say_picked(s);
    return box;
}

} // namespace gui
