// the KEY ASSIGNMENT page of a keyboard: the board, one layer at a time (normal, with Easy-Shift held, with Fn held).
// click a key, then say what it should do on that layer: be another key, a key combination, a function of the keyboard,
// a macro, or nothing. what a key can hold depends on the key and the layer, the library says (rockcat_profile_input_room)
// changes go into the profile being edited, SAVE TO DEVICE sends them like any other change

#include "../app.h"

#include <functional>

namespace gui {

struct key_page {
    GtkWidget *view = nullptr, *hint = nullptr, *picked = nullptr;
    GtkWidget *layer_buttons[3] = {};
    GtkWidget *panel = nullptr, *title = nullptr, *where = nullptr, *kind = nullptr, *stack = nullptr, *room_hint = nullptr;
    GtkWidget *key_combo = nullptr, *key_capture = nullptr;
    GtkWidget *combo_entry = nullptr, *combo_capture = nullptr;
    GtkWidget *func_combo = nullptr;
    GtkWidget *macro_view = nullptr, *macro_record = nullptr, *macro_loop = nullptr, *macro_name = nullptr, *macro_delays = nullptr,
              *macro_fixed = nullptr, *macro_count = nullptr;
    GtkListStore *macro_store = nullptr;
    GtkWidget *reset = nullptr;
    int layer = 0, selected = -1, hover = -1;
    int capture = 0; // 1 = one key, 2 = a combination, 3 = recording a macro
    // macros as edited here, by layer * 256 + input. the device only hears of them on SAVE TO DEVICE
    std::map<int, rockcat_macro> macros;
    std::vector<int> held; // keys down while recording, a held key repeats and that is not part of a macro
    gint64 last_event = 0;
    bool filling = false;
};

namespace {

// HID usage of a key -> linux input code (what the kernel makes of it, hid-input.c). the other way round is what a
// key press in the window tells us: GDK gives the linux code plus 8
constexpr uint8_t HID_TO_EVDEV[0x74] = {
    0,   0,   0,   0,   30,  48,  46,  32,  18,  33,  34,  35,  23,  36,  37,  38,  // 0x00
    50,  49,  24,  25,  16,  19,  31,  20,  22,  47,  17,  45,  21,  44,  2,   3,   // 0x10
    4,   5,   6,   7,   8,   9,   10,  11,  28,  1,   14,  15,  57,  12,  13,  26,  // 0x20
    27,  43,  43,  39,  40,  41,  51,  52,  53,  58,  59,  60,  61,  62,  63,  64,  // 0x30
    65,  66,  67,  68,  87,  88,  99,  70,  119, 110, 102, 104, 111, 107, 109, 106, // 0x40
    105, 108, 103, 69,  98,  55,  74,  78,  96,  79,  80,  81,  75,  76,  77,  71,  // 0x50
    72,  73,  82,  83,  86,  127, 116, 117, 183, 184, 185, 186, 187, 188, 189, 190, // 0x60
    191, 192, 193, 194,                                                             // 0x70
};
constexpr struct { uint8_t usage, evdev; } MORE_KEYS[] = {
    {0x87, 89}, {0x88, 93}, {0x89, 124}, {0x8a, 92}, {0x8b, 94},
    {0xe0, 29}, {0xe1, 42}, {0xe2, 56}, {0xe3, 125}, {0xe4, 97}, {0xe5, 54}, {0xe6, 100}, {0xe7, 126},
};

int usage_of(const GdkEventKey *e)
{
    int code = static_cast<int>(e->hardware_keycode) - 8;
    if (code <= 0)
        return 0;
    for (int u = 4; u < 0x74; ++u)
        if (HID_TO_EVDEV[u] == code)
            return u;
    for (const auto &m : MORE_KEYS)
        if (m.evdev == code)
            return m.usage;
    return 0;
}

bool is_modifier(int usage) { return usage >= 0xe0 && usage <= 0xe7; }

// the bits a Ryos wants in front of a combination: 1 ctrl, 2 shift, 4 alt, 8 win. rockcat_format_shortcut speaks the
// mouse ones
uint8_t to_lib_mods(uint8_t k)
{
    return static_cast<uint8_t>((k & 1 ? ROCKCAT_MOD_CTRL : 0) | (k & 2 ? ROCKCAT_MOD_SHIFT : 0) | (k & 4 ? ROCKCAT_MOD_ALT : 0) |
                                (k & 8 ? ROCKCAT_MOD_WIN : 0));
}
uint8_t to_kb_mods(uint8_t m)
{
    return static_cast<uint8_t>((m & ROCKCAT_MOD_CTRL ? 1 : 0) | (m & ROCKCAT_MOD_SHIFT ? 2 : 0) | (m & ROCKCAT_MOD_ALT ? 4 : 0) |
                                (m & ROCKCAT_MOD_WIN ? 8 : 0));
}

std::string key_text(int usage)
{
    char buf[32];
    rockcat_format_shortcut(0, static_cast<uint8_t>(usage), buf, sizeof buf);
    std::string t = buf;
    if (t.size() == 1)
        t[0] = static_cast<char>(g_ascii_toupper(t[0]));
    return t;
}

// "volume-up" -> "Volume up"
std::string pretty(const char *name)
{
    std::string t = name ? name : "?";
    for (char &c : t)
        if (c == '-')
            c = ' ';
    if (!t.empty())
        t[0] = static_cast<char>(g_ascii_toupper(t[0]));
    return t;
}

enum kind { K_KEY, K_COMBO, K_FUNCTION, K_MACRO, K_OFF };
const char *KIND_NAMES[] = {"Another key", "Key combination", "Function of the keyboard", "Macro", "Nothing (switched off)"};

kind kind_of(const rockcat_caps *caps, const rockcat_assignment &a)
{
    if (a.type == ROCKCAT_KF_DISABLED)
        return K_OFF;
    if (a.type == ROCKCAT_KF_SHORTCUT)
        return K_COMBO;
    if (a.type == ROCKCAT_KF_MACRO)
        return K_MACRO;
    return rockcat_assignment_is_function(caps, a.type) ? K_FUNCTION : K_KEY;
}

// short text for what a key does, for the picture and the title
std::string does(const rockcat_caps *caps, const rockcat_assignment &a)
{
    switch (kind_of(caps, a)) {
    case K_OFF: return "off";
    case K_MACRO: return "macro";
    case K_KEY: return key_text(a.type);
    case K_COMBO: {
        char buf[48];
        rockcat_format_shortcut(to_lib_mods(a.param1), a.param2, buf, sizeof buf);
        return buf;
    }
    default: return pretty(rockcat_assignment_name(caps, a.type));
    }
}

// functions that make sense in the list. the ones with parameters nobody can set here yet (quick launch, timers) stay out
bool listed_function(const rockcat_caps *caps, int t)
{
    if (t == 0 || t == ROCKCAT_KF_SHORTCUT || t == ROCKCAT_KF_MACRO || t == ROCKCAT_KF_QUICKLAUNCH || t == ROCKCAT_KF_TIMER || t == 0xbf)
        return false;
    return rockcat_assignment_is_function(caps, static_cast<uint8_t>(t)) && std::strcmp(rockcat_assignment_name(caps, static_cast<uint8_t>(t)), "?") != 0;
}

key_page *page_of(app_state *s) { return s->keys; }

int room(app_state *s, int layer, int input)
{
    if (!s->profile || input < 0)
        return 0;
    int r = rockcat_profile_input_room(s->profile, layer, input);
    return r < 0 ? 0 : r;
}

bool get(app_state *s, int layer, int input, rockcat_assignment *a)
{
    return s->profile && rockcat_profile_get_input(s->profile, layer, input, a) == ROCKCAT_OK;
}

bool changed(app_state *s, int layer, int input)
{
    rockcat_assignment now{}, factory{};
    if (!get(s, layer, input, &now) || rockcat_profile_factory_input(s->profile, layer, input, &factory) != ROCKCAT_OK)
        return false;
    return now.type != factory.type || now.param1 != factory.param1 || now.param2 != factory.param2;
}

void show_key(app_state *s);

void set(app_state *s, const rockcat_assignment &a)
{
    key_page *k = page_of(s);
    if (!s->profile || k->selected < 0)
        return;
    int r = rockcat_profile_set_input(s->profile, k->layer, k->selected, &a);
    if (r != ROCKCAT_OK) {
        report_error(s, "This key cant take that", r);
        return;
    }
    gtk_widget_queue_draw(k->view);
    schedule_auto_apply(s);
}

/* ---- the macro ------------------------------------------------------------ */

int macro_id(const key_page *k) { return k->layer * 256 + k->selected; }

// the macro of the selected key: as edited here if it was, from the device otherwise (read once, when first needed)
rockcat_macro &macro_of(app_state *s)
{
    key_page *k = page_of(s);
    auto it = k->macros.find(macro_id(k));
    if (it != k->macros.end())
        return it->second;
    rockcat_macro m{};
    rockcat_assignment a{};
    if (s->dev && get(s, k->layer, k->selected, &a) && a.type == ROCKCAT_KF_MACRO)
        rockcat_profile_get_macro(s->dev, s->profile, k->layer, k->selected, &m);
    if (!m.name[0])
        std::snprintf(m.name, sizeof m.name, "%s", rockcat_input_label(s->caps, k->selected));
    if (!m.set_name[0])
        std::snprintf(m.set_name, sizeof m.set_name, "rockcat");
    if (m.loop == 0)
        m.loop = 1;
    return k->macros[macro_id(k)] = m;
}

void fill_macro(app_state *s)
{
    key_page *k = page_of(s);
    rockcat_macro &m = macro_of(s);
    k->filling = true;
    gtk_list_store_clear(k->macro_store);
    for (int i = 0; i < m.count; ++i) {
        GtkTreeIter it;
        gtk_list_store_append(k->macro_store, &it);
        gtk_list_store_set(k->macro_store, &it, 0, i + 1, 1, key_text(m.keys[i].key).c_str(), 2,
                           m.keys[i].action == ROCKCAT_KEY_PRESS ? "down" : "up", 3, static_cast<int>(m.keys[i].delay_ms), -1);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(k->macro_loop), m.loop);
    gtk_entry_set_text(GTK_ENTRY(k->macro_name), m.name);
    char n[64];
    std::snprintf(n, sizeof n, "%d of %d steps", m.count, ROCKCAT_MACRO_KEYSTROKES);
    gtk_label_set_text(GTK_LABEL(k->macro_count), n);
    k->filling = false;
}

// the macro changed: into the profile it goes, and the key gets set to play it
void store_macro(app_state *s)
{
    key_page *k = page_of(s);
    rockcat_macro &m = macro_of(s);
    int r = rockcat_profile_set_macro(s->profile, k->layer, k->selected, &m);
    if (r != ROCKCAT_OK) {
        report_error(s, "This key cant hold a macro", r);
        return;
    }
    rockcat_assignment a{ROCKCAT_KF_MACRO, 0, 0};
    set(s, a);
}

void on_macro_delay_edited(GtkCellRendererText *, gchar *path, gchar *text, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    rockcat_macro &m = macro_of(s);
    int row = std::atoi(path);
    if (row < 0 || row >= m.count)
        return;
    long v = std::strtol(text, nullptr, 10);
    m.keys[row].delay_ms = static_cast<uint16_t>(std::max(0L, std::min(65535L, v)));
    fill_macro(s);
    store_macro(s);
}

void on_macro_loop(GtkSpinButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (page_of(s)->filling)
        return;
    macro_of(s).loop = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(b));
    store_macro(s);
}

void on_macro_name(GtkEntry *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (page_of(s)->filling)
        return;
    rockcat_macro &m = macro_of(s);
    std::snprintf(m.name, sizeof m.name, "%s", gtk_entry_get_text(e));
    store_macro(s);
}

void on_macro_clear(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    macro_of(s).count = 0;
    fill_macro(s);
    store_macro(s);
}

// every delay the same, for macros that should not depend on how fast somebody typed them
void on_macro_even(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    rockcat_macro &m = macro_of(s);
    int ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(page_of(s)->macro_fixed));
    for (int i = 0; i < m.count; ++i)
        m.keys[i].delay_ms = static_cast<uint16_t>(ms);
    fill_macro(s);
    store_macro(s);
}

void stop_capture(app_state *s)
{
    key_page *k = page_of(s);
    k->capture = 0;
    k->held.clear();
    gtk_button_set_label(GTK_BUTTON(k->key_capture), "PRESS A KEY...");
    gtk_button_set_label(GTK_BUTTON(k->combo_capture), "PRESS THE COMBINATION...");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(k->macro_record), FALSE);
}

void on_macro_record(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    if (gtk_toggle_button_get_active(b)) {
        // a new recording replaces the old one
        macro_of(s).count = 0;
        fill_macro(s);
        k->capture = 3;
        k->held.clear();
        k->last_event = 0;
        gtk_button_set_label(GTK_BUTTON(b), "STOP");
        set_status(s, "Recording: type the macro, then press STOP", "connected");
    } else {
        if (k->capture == 3) {
            k->capture = 0;
            store_macro(s);
            set_status(s, "Macro recorded, not saved to the keyboard yet", "connected");
        }
        gtk_button_set_label(GTK_BUTTON(b), "RECORD");
    }
}

/* ---- the panel on the right --------------------------------------------- */

void on_kind(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    if (k->filling || k->selected < 0)
        return;
    const char *id = gtk_combo_box_get_active_id(combo);
    if (!id)
        return;
    int want = std::atoi(id);
    gtk_stack_set_visible_child_name(GTK_STACK(k->stack), id);
    rockcat_assignment now{};
    get(s, k->layer, k->selected, &now);
    if (kind_of(s->caps, now) == want)
        return;
    // switching the kind gives the key something of that kind right away, so it never sits in between
    switch (want) {
    case K_OFF:
        set(s, rockcat_assignment{ROCKCAT_KF_DISABLED, 0, 0});
        break;
    case K_KEY: {
        rockcat_assignment f{};
        int usage = rockcat_profile_factory_input(s->profile, k->layer, k->selected, &f) == ROCKCAT_OK &&
                            kind_of(s->caps, f) == K_KEY ? f.type : 0x04;
        set(s, rockcat_assignment{static_cast<uint8_t>(usage), 0, 0});
        break;
    }
    case K_FUNCTION:
        set(s, rockcat_assignment{ROCKCAT_KF_MUTE, 0, 0});
        break;
    case K_COMBO:
        set(s, rockcat_assignment{ROCKCAT_KF_SHORTCUT, 1, 0x06}); // ctrl+c, to have something
        break;
    case K_MACRO:
        store_macro(s);
        break;
    }
    show_key(s);
}

void on_key_combo(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (page_of(s)->filling)
        return;
    const char *id = gtk_combo_box_get_active_id(combo);
    if (id)
        set(s, rockcat_assignment{static_cast<uint8_t>(std::atoi(id)), 0, 0});
}

void on_func_combo(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (page_of(s)->filling)
        return;
    const char *id = gtk_combo_box_get_active_id(combo);
    if (id)
        set(s, rockcat_assignment{static_cast<uint8_t>(std::atoi(id)), 0, 0});
}

void on_combo_entry(GtkEntry *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (page_of(s)->filling)
        return;
    uint8_t mods = 0, key = 0;
    if (rockcat_parse_shortcut(gtk_entry_get_text(e), &mods, &key) != ROCKCAT_OK) {
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(e)), "error");
        return;
    }
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(e)), "error");
    set(s, rockcat_assignment{ROCKCAT_KF_SHORTCUT, to_kb_mods(mods), key});
}

void on_capture(GtkButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    int want = b == GTK_BUTTON(k->key_capture) ? 1 : 2;
    if (k->capture == want) {
        stop_capture(s);
        return;
    }
    stop_capture(s);
    k->capture = want;
    gtk_button_set_label(b, want == 1 ? "WAITING FOR A KEY (click to stop)" : "WAITING FOR KEYS (click to stop)");
}

void on_reset_key(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    rockcat_assignment f{};
    if (k->selected < 0 || rockcat_profile_factory_input(s->profile, k->layer, k->selected, &f) != ROCKCAT_OK)
        return;
    set(s, f);
    show_key(s);
}

// the right side, for the key that is picked
void show_key(app_state *s)
{
    key_page *k = page_of(s);
    if (!k || !k->panel)
        return;
    stop_capture(s);
    const int input = k->selected;
    const int can = room(s, k->layer, input);
    if (k->picked) {
        rockcat_assignment now{};
        std::string text = input < 0 ? "no key picked" : std::string(rockcat_input_label(s->caps, input)) + ": " +
                                                           (get(s, k->layer, input, &now) ? does(s->caps, now) : "nothing on this layer");
        gtk_label_set_text(GTK_LABEL(k->picked), text.c_str());
    }
    if (input < 0 || can == 0) {
        gtk_widget_set_sensitive(k->panel, FALSE);
        gtk_label_set_text(GTK_LABEL(k->title), input < 0 ? "No key picked" : rockcat_input_label(s->caps, input));
        gtk_label_set_text(GTK_LABEL(k->where), input < 0 ? "Click a key on the board" : "This key has nothing of its own on this layer");
        gtk_widget_hide(k->stack);
        gtk_widget_hide(k->room_hint);
        return;
    }
    gtk_widget_set_sensitive(k->panel, TRUE);
    gtk_widget_show(k->stack);
    k->filling = true;
    rockcat_assignment a{}, f{};
    get(s, k->layer, input, &a);
    bool known = rockcat_profile_factory_input(s->profile, k->layer, input, &f) == ROCKCAT_OK;
    char title[96];
    std::snprintf(title, sizeof title, "%s", rockcat_input_label(s->caps, input));
    gtk_label_set_text(GTK_LABEL(k->title), title);
    char where[160];
    std::snprintf(where, sizeof where, "%s layer. Out of the box: %s", rockcat_layer_label(s->caps, k->layer),
                  known ? does(s->caps, f).c_str() : "not known");
    gtk_label_set_text(GTK_LABEL(k->where), where);
    gtk_widget_set_sensitive(k->reset, known);

    // what this key can be on this layer
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(k->kind));
    const int needs[] = {ROCKCAT_ROOM_KEY, ROCKCAT_ROOM_PARAMS, ROCKCAT_ROOM_KEY, ROCKCAT_ROOM_MACRO, ROCKCAT_ROOM_KEY};
    for (int i = 0; i < 5; ++i)
        if (can & needs[i]) {
            char id[4];
            std::snprintf(id, sizeof id, "%d", i);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(k->kind), id, KIND_NAMES[i]);
        }
    char now[4];
    const kind kd = kind_of(s->caps, a);
    std::snprintf(now, sizeof now, "%d", kd);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(k->kind), now);
    gtk_stack_set_visible_child_name(GTK_STACK(k->stack), now);
    gtk_widget_set_visible(k->room_hint, !(can & ROCKCAT_ROOM_PARAMS));

    char id[8];
    std::snprintf(id, sizeof id, "%d", kd == K_KEY ? a.type : -1);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(k->key_combo), id);
    std::snprintf(id, sizeof id, "%d", kd == K_FUNCTION ? a.type : -1);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(k->func_combo), id);
    gtk_entry_set_text(GTK_ENTRY(k->combo_entry), kd == K_COMBO ? does(s->caps, a).c_str() : "");
    k->filling = false;
    if (kd == K_MACRO)
        fill_macro(s);
}

/* ---- the board ------------------------------------------------------------ */

gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    if (!k || !s->profile)
        return FALSE;
    keyboard_draw_board(s, w, cr, [&](const rockcat_key &key, double *rgb) {
        // keys with nothing on this layer stay dark
        double v = room(s, k->layer, key.led) ? 0.82 : 0.22;
        rgb[0] = rgb[1] = rgb[2] = v;
    });
    const rockcat_key *keys = nullptr;
    int n = keyboard_key_count(s, &keys);
    for (int i = 0; i < n; ++i) {
        const rockcat_key &key = keys[i];
        const bool mine = changed(s, k->layer, key.led);
        const bool picked = key.led == k->selected, hover = key.led == k->hover;
        if (mine) {
            // a key that does something else than it did out of the box: tinted, with what it does now written on it
            rounded_rect(cr, key.x + 1, key.y + 1, key.w - 2, key.h - 2, 3);
            cairo_set_source_rgba(cr, 0.20, 0.55, 0.95, 0.55);
            cairo_fill(cr);
            rockcat_assignment a{};
            get(s, k->layer, key.led, &a);
            std::string text = does(s->caps, a);
            cairo_save(cr);
            cairo_rectangle(cr, key.x + 2, key.y, key.w - 4, key.h);
            cairo_clip(cr);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, std::min(9.0, key.h * 0.26));
            cairo_text_extents_t ext;
            cairo_text_extents(cr, text.c_str(), &ext);
            double x = key.x + std::max(2.0, (key.w - ext.width) / 2 - ext.x_bearing);
            cairo_move_to(cr, x, key.y + key.h - 4);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_show_text(cr, text.c_str());
            cairo_restore(cr);
        }
        if (picked || hover) {
            rounded_rect(cr, key.x + 0.75, key.y + 0.75, key.w - 1.5, key.h - 1.5, 3);
            cairo_set_line_width(cr, picked ? 2 : 1);
            cairo_set_source_rgba(cr, 1, 1, 1, picked ? 0.95 : 0.55);
            cairo_stroke(cr);
        }
    }
    return FALSE;
}

gboolean on_press(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    if (e->button != 1)
        return FALSE;
    const rockcat_key *key = keyboard_key_at(s, w, e->x, e->y);
    k->selected = key ? key->led : -1;
    show_key(s);
    gtk_widget_queue_draw(w);
    return TRUE;
}

gboolean on_motion(GtkWidget *w, GdkEventMotion *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    const rockcat_key *key = keyboard_key_at(s, w, e->x, e->y);
    int hover = key ? key->led : -1;
    if (hover != k->hover) {
        k->hover = hover;
        std::string tip;
        if (key) {
            rockcat_assignment a{};
            tip = key->name;
            if (get(s, k->layer, key->led, &a))
                tip += ": " + does(s->caps, a);
            else
                tip += ": nothing on this layer";
        }
        gtk_widget_set_tooltip_text(w, tip.empty() ? nullptr : tip.c_str());
        gtk_widget_queue_draw(w);
    }
    return FALSE;
}

gboolean on_leave(GtkWidget *w, GdkEventCrossing *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    page_of(s)->hover = -1;
    gtk_widget_queue_draw(w);
    return FALSE;
}

void on_layer(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    key_page *k = page_of(s);
    if (k->filling)
        return;
    int layer = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "layer"));
    if (!gtk_toggle_button_get_active(b)) {
        // one is always down
        if (layer == k->layer) {
            k->filling = true;
            gtk_toggle_button_set_active(b, TRUE);
            k->filling = false;
        }
        return;
    }
    k->layer = layer;
    k->filling = true;
    for (int i = 0; i < 3; ++i)
        if (k->layer_buttons[i] && i != layer)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(k->layer_buttons[i]), FALSE);
    k->filling = false;
    show_key(s);
    gtk_widget_queue_draw(k->view);
}

GtkWidget *combo_row(GtkWidget *grid, int row, const char *name, GtkWidget *w)
{
    gtk_grid_attach(GTK_GRID(grid), label(name), 0, row, 1, 1);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
    return w;
}

} // namespace

// a key went down or up somewhere in the window. while the page waits for one it is ours, otherwise GTK gets it
gboolean keys_on_key(app_state *s, GdkEventKey *e)
{
    key_page *k = s->keys;
    if (!k || !k->capture)
        return FALSE;
    const bool down = e->type == GDK_KEY_PRESS;
    int usage = usage_of(e);
    if (!usage)
        return TRUE;
    if (k->capture == 1) {
        if (!down)
            return TRUE;
        set(s, rockcat_assignment{static_cast<uint8_t>(usage), 0, 0});
        show_key(s);
        return TRUE;
    }
    if (k->capture == 2) {
        // modifiers alone dont make a combination, the key that comes with them does
        if (!down || is_modifier(usage))
            return TRUE;
        uint8_t mods = static_cast<uint8_t>(((e->state & GDK_CONTROL_MASK) ? 1 : 0) | ((e->state & GDK_SHIFT_MASK) ? 2 : 0) |
                                            ((e->state & GDK_MOD1_MASK) ? 4 : 0) | ((e->state & (GDK_SUPER_MASK | GDK_MOD4_MASK)) ? 8 : 0));
        set(s, rockcat_assignment{ROCKCAT_KF_SHORTCUT, mods, static_cast<uint8_t>(usage)});
        show_key(s);
        return TRUE;
    }
    // recording: every press and release, with the time that went by before the next one
    rockcat_macro &m = macro_of(s);
    auto at = std::find(k->held.begin(), k->held.end(), usage);
    if (down && at != k->held.end())
        return TRUE; // the key repeating by itself
    if (!down && at == k->held.end())
        return TRUE; // let go of a key that went down before the recording
    if (down)
        k->held.push_back(usage);
    else
        k->held.erase(at);
    if (m.count >= ROCKCAT_MACRO_KEYSTROKES) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(k->macro_record), FALSE);
        return TRUE;
    }
    gint64 now = g_get_monotonic_time();
    if (m.count > 0 && k->last_event)
        m.keys[m.count - 1].delay_ms = static_cast<uint16_t>(std::min<gint64>(65535, (now - k->last_event) / 1000));
    k->last_event = now;
    m.keys[m.count++] = rockcat_keystroke{static_cast<uint8_t>(usage), static_cast<uint8_t>(down ? ROCKCAT_KEY_PRESS : ROCKCAT_KEY_RELEASE), 20};
    fill_macro(s);
    return TRUE;
}

// a profile got loaded (or written and read back): whatever was edited here is in it now or gone
void keys_refresh(app_state *s)
{
    key_page *k = s->keys;
    if (!k)
        return;
    k->macros.clear();
    show_key(s);
    if (k->view)
        gtk_widget_queue_draw(k->view);
}

void keys_pick(app_state *s, int input)
{
    key_page *k = s->keys;
    if (!k)
        return;
    k->selected = input;
    show_key(s);
    gtk_widget_queue_draw(k->view);
}

// the page is about to go (another device, or the window closes): nothing may point at its widgets any more
void keys_forget(app_state *s)
{
    delete s->keys;
    s->keys = nullptr;
}

GtkWidget *build_keys_page(app_state *s)
{
    delete s->keys;
    s->keys = new key_page;
    key_page *k = s->keys;

    GtkWidget *all = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(all), 16);

    // left: which layer, and the board
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(bar, "linked");
    const char *names[3] = {"NORMAL", "WITH EASY-SHIFT HELD", "WITH FN HELD"};
    for (int i = 0; i < std::min(3, n_layers(s)); ++i) {
        GtkWidget *b = gtk_toggle_button_new_with_label(names[i]);
        g_object_set_data(G_OBJECT(b), "layer", GINT_TO_POINTER(i));
        g_object_set_data(G_OBJECT(b), "rc-no-autoapply", GINT_TO_POINTER(1));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), i == 0);
        g_signal_connect(b, "toggled", G_CALLBACK(on_layer), s);
        gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
        k->layer_buttons[i] = b;
    }
    gtk_box_pack_start(GTK_BOX(left), bar, FALSE, FALSE, 0);
    // second row, as tall as the colour row on the lighting page so the board sits at the same spot on both
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    k->picked = label("", "rc-hint");
    gtk_box_pack_start(GTK_BOX(row2), k->picked, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row2), keyboard_region_box(s), FALSE, FALSE, 0);
    gtk_size_group_add_widget(s->kb_rows, row2);
    gtk_box_pack_start(GTK_BOX(left), row2, FALSE, FALSE, 0);
    k->view = gtk_drawing_area_new();
    gtk_widget_set_size_request(k->view, 480, static_cast<int>(480 * keyboard_aspect(s)));
    gtk_widget_set_hexpand(k->view, TRUE);
    gtk_widget_add_events(k->view, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(k->view, "draw", G_CALLBACK(on_draw), s);
    g_signal_connect(k->view, "button-press-event", G_CALLBACK(on_press), s);
    g_signal_connect(k->view, "motion-notify-event", G_CALLBACK(on_motion), s);
    g_signal_connect(k->view, "leave-notify-event", G_CALLBACK(on_leave), s);
    keyboard_keep_shape(s, k->view);
    gtk_box_pack_start(GTK_BOX(left), k->view, FALSE, FALSE, 0);
    k->hint = label("Click a key to change what it does on the layer above. Blue keys do something else than out of the box, "
                    "dark keys have nothing of their own on that layer.", "rc-hint");
    gtk_label_set_line_wrap(GTK_LABEL(k->hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(k->hint), 0);
    gtk_box_pack_start(GTK_BOX(left), k->hint, FALSE, FALSE, 0);
    GtkWidget *panel = box_panel("KEYS", left);
    gtk_size_group_add_widget(s->kb_rows, panel_title(panel));
    gtk_box_pack_start(GTK_BOX(all), panel, TRUE, TRUE, 0);

    // right: the key
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    k->title = label("", "rc-device-name");
    gtk_label_set_xalign(GTK_LABEL(k->title), 0);
    k->where = label("", "rc-hint");
    gtk_label_set_xalign(GTK_LABEL(k->where), 0);
    gtk_label_set_line_wrap(GTK_LABEL(k->where), TRUE);
    gtk_box_pack_start(GTK_BOX(col), k->title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), k->where, FALSE, FALSE, 0);

    GtkWidget *kg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(kg), 10);
    k->kind = combo_row(kg, 0, "DOES", gtk_combo_box_text_new());
    g_signal_connect(k->kind, "changed", G_CALLBACK(on_kind), s);
    gtk_box_pack_start(GTK_BOX(col), kg, FALSE, FALSE, 0);
    k->room_hint = label("On this layer the key can only be another key or a function. Combinations and macros go on its "
                         "Easy-Shift side.", "rc-hint");
    gtk_label_set_line_wrap(GTK_LABEL(k->room_hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(k->room_hint), 0);
    gtk_widget_set_no_show_all(k->room_hint, TRUE);
    gtk_box_pack_start(GTK_BOX(col), k->room_hint, FALSE, FALSE, 0);

    k->stack = gtk_stack_new();
    gtk_stack_set_vhomogeneous(GTK_STACK(k->stack), FALSE);
    gtk_stack_set_homogeneous(GTK_STACK(k->stack), FALSE);
    // another key: from the list, or by pressing it
    {
        GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        k->key_combo = gtk_combo_box_text_new();
        for (int u = 4; u <= 0xe7; ++u) {
            if (rockcat_assignment_is_function(s->caps, static_cast<uint8_t>(u)))
                continue;
            std::string t = key_text(u);
            if (t.rfind("0x", 0) == 0)
                continue;
            char id[8];
            std::snprintf(id, sizeof id, "%d", u);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(k->key_combo), id, t.c_str());
        }
        g_signal_connect(k->key_combo, "changed", G_CALLBACK(on_key_combo), s);
        k->key_capture = gtk_button_new_with_label("PRESS A KEY...");
        g_signal_connect(k->key_capture, "clicked", G_CALLBACK(on_capture), s);
        gtk_box_pack_start(GTK_BOX(b), k->key_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(b), k->key_capture, FALSE, FALSE, 0);
        gtk_stack_add_named(GTK_STACK(k->stack), b, "0");
    }
    // a combination: typed or pressed
    {
        GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        k->combo_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(k->combo_entry), "ctrl+shift+p");
        g_signal_connect(k->combo_entry, "changed", G_CALLBACK(on_combo_entry), s);
        k->combo_capture = gtk_button_new_with_label("PRESS THE COMBINATION...");
        g_signal_connect(k->combo_capture, "clicked", G_CALLBACK(on_capture), s);
        gtk_box_pack_start(GTK_BOX(b), k->combo_entry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(b), k->combo_capture, FALSE, FALSE, 0);
        gtk_stack_add_named(GTK_STACK(k->stack), b, "1");
    }
    // a function of the keyboard
    {
        k->func_combo = gtk_combo_box_text_new();
        for (int t = 1; t < 256; ++t)
            if (listed_function(s->caps, t)) {
                char id[8];
                std::snprintf(id, sizeof id, "%d", t);
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(k->func_combo), id, pretty(rockcat_assignment_name(s->caps, static_cast<uint8_t>(t))).c_str());
            }
        g_signal_connect(k->func_combo, "changed", G_CALLBACK(on_func_combo), s);
        gtk_widget_set_valign(k->func_combo, GTK_ALIGN_START);
        gtk_stack_add_named(GTK_STACK(k->stack), k->func_combo, "2");
    }
    // a macro: recorded, then the delays can be changed
    {
        GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        k->macro_record = gtk_toggle_button_new_with_label("RECORD");
        gtk_widget_set_tooltip_text(k->macro_record, "Type the macro on the keyboard while this is down. Delays are recorded as typed");
        g_signal_connect(k->macro_record, "toggled", G_CALLBACK(on_macro_record), s);
        GtkWidget *clear = gtk_button_new_with_label("CLEAR");
        g_signal_connect(clear, "clicked", G_CALLBACK(on_macro_clear), s);
        k->macro_count = label("", "rc-hint");
        gtk_box_pack_start(GTK_BOX(top), k->macro_record, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(top), clear, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(top), k->macro_count, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(b), top, FALSE, FALSE, 0);

        k->macro_store = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
        k->macro_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(k->macro_store));
        g_object_unref(k->macro_store);
        const char *heads[] = {"#", "KEY", "", "THEN WAIT (MS)"};
        for (int c = 0; c < 4; ++c) {
            GtkCellRenderer *r = gtk_cell_renderer_text_new();
            if (c == 3) {
                g_object_set(r, "editable", TRUE, nullptr);
                g_signal_connect(r, "edited", G_CALLBACK(on_macro_delay_edited), s);
            }
            gtk_tree_view_append_column(GTK_TREE_VIEW(k->macro_view), gtk_tree_view_column_new_with_attributes(heads[c], r, "text", c, nullptr));
        }
        GtkWidget *sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 220);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(sw), k->macro_view);
        gtk_box_pack_start(GTK_BOX(b), sw, TRUE, TRUE, 0);

        GtkWidget *g = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(g), 8);
        gtk_grid_set_row_spacing(GTK_GRID(g), 6);
        k->macro_fixed = gtk_spin_button_new_with_range(0, 5000, 5);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(k->macro_fixed), 20);
        GtkWidget *even = gtk_button_new_with_label("SET ALL");
        gtk_widget_set_tooltip_text(even, "Every delay gets this many milliseconds");
        g_signal_connect(even, "clicked", G_CALLBACK(on_macro_even), s);
        GtkWidget *eb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(eb), k->macro_fixed, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(eb), even, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(g), label("DELAYS"), 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), eb, 1, 0, 1, 1);
        k->macro_loop = gtk_spin_button_new_with_range(1, 255, 1);
        g_signal_connect(k->macro_loop, "value-changed", G_CALLBACK(on_macro_loop), s);
        gtk_grid_attach(GTK_GRID(g), label("PLAYS"), 0, 1, 1, 1);
        GtkWidget *lb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(lb), k->macro_loop, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(lb), label("times per press", "rc-hint"), FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(g), lb, 1, 1, 1, 1);
        k->macro_name = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(k->macro_name), 32);
        g_signal_connect(k->macro_name, "changed", G_CALLBACK(on_macro_name), s);
        gtk_grid_attach(GTK_GRID(g), label("NAME"), 0, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(g), k->macro_name, 1, 2, 1, 1);
        gtk_box_pack_start(GTK_BOX(b), g, FALSE, FALSE, 0);
        gtk_stack_add_named(GTK_STACK(k->stack), b, "3");
    }
    gtk_stack_add_named(GTK_STACK(k->stack), label("The key does nothing at all.", "rc-hint"), "4");
    gtk_box_pack_start(GTK_BOX(col), k->stack, FALSE, FALSE, 0);

    k->reset = gtk_button_new_with_label("RESET THIS KEY");
    gtk_widget_set_tooltip_text(k->reset, "What this key did on this layer when the keyboard was new");
    g_signal_connect(k->reset, "clicked", G_CALLBACK(on_reset_key), s);
    gtk_widget_set_halign(k->reset, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(col), k->reset, FALSE, FALSE, 0);

    GtkWidget *resetall = gtk_button_new_with_label("FACTORY RESET KEYBINDS");
    g_signal_connect(resetall, "clicked", G_CALLBACK(on_reset_buttons), s);
    gtk_widget_set_halign(resetall, GTK_ALIGN_START);

    k->panel = col;
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_pack_start(GTK_BOX(right), box_panel("KEY", col), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), resetall, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 400);
    gtk_widget_set_size_request(scroll, 400, -1);
    gtk_widget_set_hexpand(scroll, FALSE);
    gtk_container_add(GTK_CONTAINER(scroll), right);
    gtk_box_pack_start(GTK_BOX(all), scroll, FALSE, TRUE, 0);

    // none of these are profile settings the auto apply should look at, they all go through set() and store_macro()
    for (GtkWidget *w : {k->kind, k->key_combo, k->func_combo, k->combo_entry, k->macro_loop, k->macro_name, k->macro_fixed, k->macro_record})
        g_object_set_data(G_OBJECT(w), "rc-no-autoapply", GINT_TO_POINTER(1));
    return all;
}

} // namespace gui
