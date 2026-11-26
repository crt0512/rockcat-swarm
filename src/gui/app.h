// what the files of the gui share: the state of the one window there is, and every function that gets called across files
// shell.cpp builds the window out of the pages in pages/, device.cpp talks to the device, sync.cpp moves values between
// the profile and the widgets
#pragma once

#include "window.h"

#include "rockcat/rockcat.h"

#ifdef ROCKCAT_HAVE_TRAY
#include <libayatana-appindicator/app-indicator.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gui {

// every device so far has five profiles. the rest (dpi levels, leds, buttons...) the pages ask the device for
constexpr int N_PROFILES = 5;
// the button and lighting pages of a mouse: the picture takes whatever room is left next to a column of this width.
// the same width on both pages, so the mouse has the same size and place on both and still grows with the window
constexpr int MOUSE_VIEW_HEIGHT = 400, MOUSE_SIDE_WIDTH = 700;

inline const rockcat_caps *kone_caps()
{
    static const rockcat_caps *caps = [] {
        const rockcat_caps *c = nullptr;
        uint16_t pid = 0;
        for (int i = 0; rockcat_model_at(i, &pid, nullptr, &c) == ROCKCAT_OK; ++i)
            if (pid == ROCKCAT_PID_KONE_AIMO_REMASTERED)
                return c;
        return static_cast<const rockcat_caps *>(nullptr);
    }();
    return caps;
}

struct device_view;  // device_view.cpp
struct keyboard_art; // keyboard_view.cpp
struct key_page;     // pages/keys.cpp
struct side_panel;   // side_panel.cpp

struct dpi_row {
    GtkWidget *enabled;
    GtkWidget *active;
    GtkWidget *x;
    GtkWidget *y;
};

struct button_row {
    GtkWidget *combo;
    GtkWidget *p1;
    GtkWidget *p2;
    GtkWidget *text; // macro text entry
};

struct app_state {
    GtkWidget *window = nullptr;
    GtkWidget *device_label = nullptr;
    GtkWidget *device_icon = nullptr;
    GtkWidget *status_label = nullptr;
    GtkWidget *notebook = nullptr;
    GtkWidget *profile_buttons[N_PROFILES] = {};
    GtkWidget *apply_button = nullptr;
    GtkWidget *header = nullptr;
    GtkWidget *activate_button = nullptr;
    GtkWidget *reload_button = nullptr;

    // settings page
    std::vector<dpi_row> dpi;
    GtkWidget *dpi_lock = nullptr;
    std::vector<GtkWidget *> polling; // one radio button per rate in caps->polling_hz

    // buttons page
    std::vector<button_row> buttons; // layer * inputs + input
    std::vector<GtkWidget *> button_names; // the label in front of each row, by input
    std::vector<GtkWidget *> button_cells; // what each row has per layer (layer * inputs + input), shown and hidden with it
    // a mouse with side buttons you put in yourself (the Nyth): which caps are in, the drawing of its side, and the
    // TOP / SIDE switch the picture sits in. nullptr for every other mouse
    side_panel *side = nullptr;
    GtkWidget *side_view = nullptr, *button_stack = nullptr;

    // illumination page
    GtkWidget *effect = nullptr;
    GtkWidget *speed = nullptr;
    GtkWidget *brightness = nullptr;
    std::vector<GtkWidget *> led_color, led_enable; // per led. empty on a keyboard, 120 colour buttons help nobody
    GtkWidget *all_color = nullptr;  // keyboards: the colour that goes onto the picked keys
    GtkWidget *color_flow = nullptr;
    GtkWidget *idle_minutes = nullptr, *idle_style = nullptr, *idle_brightness = nullptr, *macro_feedback = nullptr;
    GtkWidget *group_color = nullptr;
    GtkWidget *theme = nullptr;
    GtkWidget *render_engine = nullptr;
    GtkWidget *shift_enable = nullptr;
    GtkWidget *shift_color = nullptr;
    GtkWidget *shift_style = nullptr;
    GtkWidget *shift_hint = nullptr;
    GtkWidget *theme_swatches[3] = {};
    GtkWidget *engine_label = nullptr;
    bool engine_busy = false;
    int engine_retry = 0;
    guint engine_source = 0;
    GtkWidget *colors_panel = nullptr;
    // the settings of the effect that is picked, for effects the pc draws. filled again whenever another effect gets picked
    GtkWidget *fx_panel = nullptr, *fx_box = nullptr;
    // idle: the box for what the keyboard does by itself, and the one for the idle animation the pc draws
    GtkWidget *idle_panel = nullptr, *idle_fx_panel = nullptr, *idle_fx_box = nullptr;
    int fx_shown = -1;
    rockcat_engine *engine = nullptr;
    // keyboards get an engine each (wave, ripple, heatmap), by device path. they run whichever tab is showing
    std::map<std::string, rockcat_engine *> kb_engines;
    guint preview_source = 0;
    double preview_t = 0;
    GtkWidget *mouse_light = nullptr;
    GtkWidget *mouse_buttons = nullptr;
    int highlighted_button = -1;

    // advanced page
    GtkWidget *angle_snapping = nullptr;
    GtkWidget *left_handed = nullptr;
    GtkWidget *debounce = nullptr;
    GtkWidget *raw_view = nullptr;
    GtkWidget *tdcu_label = nullptr;
    GtkWidget *calib_button = nullptr;
    GtkWidget *calib_off = nullptr;  // back to the factory tracking, only shown while calibrated (Nyth)
    GtkWidget *lift_off = nullptr;   // the lift-off distance chooser (Nyth)

    // application preferences (~/.config/rockcat/gui.ini)
    GtkWidget *pref_tray_icon = nullptr;
    GtkWidget *pref_tray_style = nullptr;
    GtkWidget *pref_tray_style_row = nullptr;
    GtkWidget *pref_tray = nullptr;
    GtkWidget *pref_start_min = nullptr;
    GtkWidget *dpi_panel = nullptr;
    GtkWidget *polling_panel = nullptr;
    bool tray_icon = false;        // the icon is there, effective for this run (pref or --minimized)
    int tray_style = 0;            // 0 coloured, 1 white, 2 black
    bool tray_enabled = false;     // closing the window hides it to that icon, same deal
    GtkWidget *auto_apply = nullptr;
    guint auto_apply_source = 0;
    bool quitting = false;
#ifdef ROCKCAT_HAVE_TRAY
    AppIndicator *indicator = nullptr;
    GtkWidget *tray_show_item = nullptr;
#endif

    rockcat_device *dev = nullptr;
    rockcat_device_info info{};
    std::string firmware;
    // what the connected device has. the pages are built from this, nullptr while nothing is connected
    const rockcat_caps *caps = nullptr;
    // its picture with markers and lights, nullptr for a device nobody made one for
    device_view *view = nullptr;
    // keyboards: the board with its keys, which of them are picked, the key under the pointer (by led, -1 = none)
    keyboard_art *kb = nullptr;
    key_page *keys = nullptr; // the key assignment page of a keyboard
    // the tab of a ROCCAT device the library doesnt know is showing (no s->dev then)
    int unsupported_pid = 0;
    std::string unsupported_path, unsupported_name;
    GtkWidget *kb_view = nullptr, *kb_hint = nullptr;
    int kb_hover = -1, kb_drag = -1;
    // the lighting page can show the Easy-Shift light layer instead of the profile colours (kb_layer 1). that one lives
    // on the pc and gets saved as soon as it changes
    int kb_layer = 0;
    int es_on = 0;
    std::vector<rockcat_color> es_colors;
    std::vector<GtkWidget *> kb_regions; // the layout choosers on the pages that show the board
    GtkSizeGroup *kb_rows = nullptr;      // their toolbar rows, kept the same height
    GtkWidget *es_enable = nullptr, *es_row = nullptr, *es_panel = nullptr, *es_effect = nullptr, *es_box = nullptr;
    double idle_preview_t0 = -1; // when the idle preview started on the preview clock, below 0 = not playing
    // more than one device plugged in: a row of tabs, one per device, says which one the window shows
    GtkWidget *device_bar = nullptr;
    GtkWidget *device_scroll = nullptr; // what device_bar sits in, this is the one that gets shown and hidden
    std::string want_path, listed;
    // asked for on the command line: the profile to open instead of the active one. -1 = nobody asked
    int want_profile = -1;
    int edit_profile = 0;
    int active_profile = 0;
    bool loaded = false;
    bool dirty = false; // the profile being edited has changes that are not on the device yet
    bool updating = false; // suppress signal feedback while filling widgets
    // what was last read from (or written to) the mouse for the profile being edited. nullptr until something is connected
    rockcat_profile *profile = nullptr;
    guint hotplug_source = 0;
};

// how many buttons (or keys) there are per layer, and how many of everything else. 0 while nothing is connected
inline int n_inputs(const app_state *s) { return s->caps ? s->caps->input_count : 0; }
inline int n_layers(const app_state *s) { return s->caps ? s->caps->layer_count : 0; }
inline bool is_kone(const app_state *s) { return s->caps && s->caps == kone_caps(); }
inline bool is_keyboard(const app_state *s) { return s->caps && s->caps->kind == ROCKCAT_KIND_KEYBOARD; }
// leds that are on or off and have the one colour they were born with (Ryos MK Pro, TKL Pro). no colour pickers for those
inline bool one_colour(const app_state *s) { return s->caps && s->caps->led_count > 0 && !(s->caps->features & ROCKCAT_HAS_RGB); }
// "mouse" or "keyboard", for sentences
// how many profiles the device has, the footer has buttons for up to N_PROFILES of them
inline int n_profiles(const app_state *s)
{
    return s->caps ? std::max(0, std::min(N_PROFILES, s->caps->profile_count)) : N_PROFILES;
}
inline const char *thing(const app_state *s) { return is_keyboard(s) ? "keyboard" : s->caps ? "mouse" : "device"; }
// whether the profile at hand has that setting at all. the pages only grow a control for what it has
inline bool has_setting(const app_state *s, rockcat_setting which)
{
    int v = 0;
    return s->profile && rockcat_profile_get_int(s->profile, which, &v) == ROCKCAT_OK;
}

struct gui_prefs {
    bool tray_icon = true;
    int tray_style = 0;
    bool tray = false;
    bool start_minimized = false;
    bool auto_apply = false;
};

struct calib_state;

GtkWidget *label(const char *text, const char *cls = nullptr, float xalign = 0.0f);
// a picture out of the built in assets, webp or png whichever the build has (assets.cpp). width > 0 fits it into width x height
GdkPixbuf *asset_pixbuf(const std::string &resource, int width = 0, int height = 0);
cairo_surface_t *asset_surface(const std::string &resource);
void add_class(GtkWidget *w, const char *cls);
// every child of a box keeps its natural height in the middle of the row instead of stretching to it
void center_children(GtkWidget *box);
// true for a mouse with its own list of what a button can be (the Lua) and not the shared Swarm one
bool own_button_types(const rockcat_caps *caps);
GtkWidget *box_panel(const char *title, GtkWidget *content);
void set_status(app_state *s, const char *text, const char *cls);
void report_error(app_state *s, const char *what, int err);
GdkRGBA to_rgba(const rockcat_color &c);
rockcat_color from_rgba(const GdkRGBA &c);
std::string prefs_path();
gui_prefs load_prefs();
void save_prefs(const gui_prefs &p);
void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);
int combo_type(GtkWidget *combo);
void collect(app_state *s);
void ensure_combo_id(GtkWidget *combo, int value);
void update_button_row_sensitivity(app_state *s, int i);
void update_raw_view(app_state *s);
void fill(app_state *s);
void set_controls_sensitive(app_state *s, bool on);
void update_profile_buttons(app_state *s);
bool load_profile(app_state *s);
void show_device_icon(app_state *s, int pid);
void connect_device(app_state *s);
void disconnect(app_state *s, const char *why);
gboolean hotplug_tick(gpointer data);
void write_text_macros(app_state *s);
void on_apply(GtkButton *, gpointer data);
void on_reload(GtkButton *, gpointer data);
void on_activate_profile(GtkButton *, gpointer data);
void on_profile_toggled(GtkToggleButton *b, gpointer data);
void on_dpi_x_changed(GtkSpinButton *spin, gpointer data);
void on_dpi_lock_toggled(GtkToggleButton *b, gpointer data);
GtkWidget *dpi_spin(app_state *s);
GtkWidget *build_settings_page(app_state *s);
void on_button_combo_changed(GtkComboBox *combo, gpointer data);
gboolean on_button_row_focus(GtkWidget *w, GdkEvent *, gpointer data);
GtkWidget *button_combo(app_state *s, int idx);
void on_reset_buttons(GtkButton *, gpointer data);
GtkWidget *build_buttons_page(app_state *s);
void on_light_changed(GtkWidget *, gpointer data);
void apply_group_color(app_state *s, std::initializer_list<int> leds);
uint8_t current_effect(app_state *s);
rockcat_profile *preview_profile(app_state *s);
gboolean preview_tick(gpointer data);
bool engine_rendered(app_state *s);
void update_effect_ui(app_state *s);
void on_effect_changed(GtkComboBox *, gpointer data);
void on_theme_changed(GtkComboBox *, gpointer data);
gboolean on_draw_swatch(GtkWidget *w, cairo_t *cr, gpointer data);
void set_engine_label(app_state *s);
void start_engine(app_state *s);
gboolean engine_tick(gpointer data);
void on_render_toggled(GtkToggleButton *, gpointer data);
void on_shift_toggled(GObject *, GParamSpec *, gpointer data);
GtkWidget *build_illumination_page(app_state *s);
void update_tdcu_label(app_state *s);
void calib_set_text(calib_state *c, const char *t);
gboolean calib_tick(gpointer data);
void on_calib_start(GtkButton *, gpointer data);
void calib_finish(calib_state *c, bool save);
void on_calib_destroy(GtkWidget *, gpointer data);
void on_calibrate(GtkButton *, gpointer data);
GtkWidget *build_advanced_page(app_state *s);
void show_window(app_state *s);
void hide_window(app_state *s);
void on_tray_show(GtkMenuItem *, gpointer data);
void on_tray_quit(GtkMenuItem *, gpointer data);
void tray_sync(app_state *s);
gboolean on_delete(GtkWidget *, GdkEvent *, gpointer data);
void store_prefs(app_state *s);
void on_app_settings(GtkButton *, gpointer data);
void on_export_profile(GtkButton *, gpointer data);
void on_import_profile(GtkButton *, gpointer data);
void on_backup(GtkButton *, gpointer data);
void on_restore(GtkButton *, gpointer data);
GtkWidget *files_panel(app_state *s);
bool settle_changes(app_state *s);
void on_pref_tray_icon(GtkToggleButton *b, gpointer data);
void on_pref_tray(GtkToggleButton *b, gpointer data);
void on_pref_tray_style(GtkComboBox *box, gpointer data);
void on_pref_start_min(GtkToggleButton *, gpointer data);
gboolean auto_apply_fire(gpointer data);
void schedule_auto_apply(app_state *s);
void on_any_change(GtkWidget *, gpointer data);
void on_any_notify(GObject *, GParamSpec *, gpointer data);
void hook_auto_apply(app_state *s, GtkWidget *w);
void on_auto_apply_toggled(GObject *sw, GParamSpec *, gpointer data);
void on_destroy(GtkWidget *, gpointer data);

void build_pages(app_state *s);
void show_fx_options(app_state *s);
GtkWidget *device_panel(app_state *s);
keyboard_art *keyboard_load(app_state *s);
void keyboard_free(keyboard_art *k);
GtkWidget *build_keyboard_view(app_state *s);
void keyboard_draw_board(app_state *s, GtkWidget *w, cairo_t *cr, const std::function<void(const rockcat_key &, double *)> &print);
const rockcat_key *keyboard_key_at(app_state *s, GtkWidget *w, double x, double y);
int keyboard_key_count(app_state *s, const rockcat_key **keys);
double keyboard_aspect(app_state *s);
void keyboard_keep_shape(app_state *s, GtkWidget *w);
GtkWidget *keyboard_region_box(app_state *s);
GtkWidget *panel_title(GtkWidget *panel);
// a box title (bold, spaced out, underlined)
GtkWidget *title_label(const char *text);
GtkWidget *build_keys_page(app_state *s);
void keys_refresh(app_state *s);
void keys_forget(app_state *s);
void keys_pick(app_state *s, int input);
void shift_layer_load(app_state *s);
void shift_layer_save(app_state *s);
void show_es_options(app_state *s);
gboolean keys_on_key(app_state *s, GdkEventKey *e);
void update_keyboard_preview(app_state *s);
void on_idle_preview(GtkButton *, gpointer data);
void set_highlight(app_state *s, int input);
void update_view_preview(app_state *s);
gboolean on_view_leave(GtkWidget *w, GdkEventCrossing *e, gpointer data);
device_view *view_load(int pid);
void view_free(device_view *v);
int view_button_number(const device_view *v, int input);
void view_size(const device_view *v, int height, int *w, int *h);
void view_buttons_size(const device_view *v, int height, int *w, int *h);
// the number the buttons page puts in front of a button (the marker number, or our own one)
int button_number(const app_state *s, int input);
side_panel *side_panel_load(int pid);
void side_panel_free(side_panel *p);
bool side_input_fitted(const app_state *s, int input);
bool side_input(const app_state *s, int input);
// the number a side button has on its marker, 1 for the top front slot
int side_number(const app_state *s, int input);
GtkWidget *build_side_view(app_state *s);
// shows the rows of the side buttons that have a cap on them and hides the others
void side_apply_rows(app_state *s);
gboolean on_draw_view_lights(GtkWidget *w, cairo_t *cr, gpointer data);
gboolean on_draw_view_buttons(GtkWidget *w, cairo_t *cr, gpointer data);
gboolean on_view_motion(GtkWidget *w, GdkEventMotion *e, gpointer data);
gboolean on_view_click(GtkWidget *w, GdkEventButton *e, gpointer data);
bool read_profile(app_state *s);
void show_profile(app_state *s);
void lights_to_profile(app_state *s);
rockcat_engine *keyboard_engine(app_state *s);
void close_device(app_state *s);
void on_device_tab(GtkToggleButton *tab, gpointer data);
void redraw_light(app_state *s);
bool pc_only(app_state *s, int effect);
void draw_view_lights(app_state *s, cairo_t *cr, bool animate);
void refresh_device_list(app_state *s);
void show_unsupported(app_state *s, const char *path);
} // namespace gui
