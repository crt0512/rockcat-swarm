// the side of a mouse whose side buttons are caps you put in yourself (the Nyth). the buttons page gets a SIDE tab with
// Swarms picture of the side and a box of caps under it: drag a cap onto a slot, drag it off the mouse to take it out.
// what sits where is remembered per device in gui.ini, the mouse itself is never told. the list on the page only shows
// the side buttons that have a cap you can press on them.
// the pictures come out of views/<pid>/ next to the top view (side.png, side-button-NN.png ..., made by
// tools/assets/side-view.py, the [side] part of view.ini says where they go). the free set has see-through stand ins
// of them, then only the outlines of the slots and the markers get drawn

#include "app.h"

namespace gui {

namespace {

// what can sit on a slot, the caps Swarm has pictures of
enum cap : char {
    CAP_EMPTY = '.',
    CAP_BUTTON = 's',
    CAP_BLANK = 'b',    // a blank cap on one slot
    CAP_DOUBLE = '<',   // the front half of a double button. only on slots 1+2 or 3+4 of a row, Swarm has no other
    CAP_DOUBLE_2 = '>', // its back half
    CAP_QUAD = '#',     // a blank over the whole row
};

struct side_layout {
    uint16_t pid;
    int rows, cols;  // seen from the left: rows top to bottom, columns front to back
    int first_input; // the input of slot 1 (top front), the others follow row by row the way Swarm numbers them
};

const side_layout LAYOUTS[] = {
    {0x2e7c, 3, 4, 8},
    {0x2e7d, 3, 4, 8},
};

const cap PALETTE[] = {CAP_BUTTON, CAP_BLANK, CAP_DOUBLE, CAP_QUAD};

const char *cap_name(cap c)
{
    switch (c) {
    case CAP_BUTTON: return "BUTTON";
    case CAP_BLANK: return "BLANK";
    case CAP_DOUBLE: return "DOUBLE BUTTON";
    case CAP_QUAD: return "BLANK ROW";
    default: return "";
    }
}

GtkTargetEntry TARGET = {const_cast<char *>("application/x-rockcat-side-cap"), GTK_TARGET_SAME_APP, 0};

struct box {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// the side and its caps are never drawn bigger than the pictures are, blown up they go blurry. the box of caps the same
constexpr double MAX_SCALE = 1.0, BOX_SCALE = 1.0;

} // namespace

struct side_panel {
    const side_layout *L = nullptr;
    std::string slots; // one cap per slot, row by row

    // the pictures. all pieces are the same size and go to (px, py) on the side picture
    cairo_surface_t *picture = nullptr;
    double width = 0, height = 0, px = 0, py = 0;
    std::vector<box> slot_box; // per slot, in the coordinates of a piece picture
    std::map<std::string, cairo_surface_t *> pieces;
    bool art = false; // false = only see-through stand ins (the free set)
    std::vector<GtkWidget *> box_pics; // the caps in the box of caps, they change with what is free

    // what is being dragged: the cap and the slot it came from (-1 = out of the box of caps)
    cap drag = CAP_EMPTY;
    int drag_from = -1;
    bool dropped = false, cancelled = false;
    int hover_slot = -1; // where a dragged cap would land, for the preview
    // a press on a cap that may turn into a drag
    bool pressing = false;
    int press_slot = -1;
    double press_x = 0, press_y = 0;
};

namespace {

std::string key_of(const side_panel *p)
{
    char k[16];
    std::snprintf(k, sizeof k, "side-%04x", p->L->pid);
    return k;
}

void save(const side_panel *p)
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, prefs_path().c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_string(kf, key_of(p).c_str(), "slots", p->slots.c_str());
    g_mkdir_with_parents(rockcat_config_dir(), 0755);
    g_key_file_save_to_file(kf, prefs_path().c_str(), nullptr);
    g_key_file_unref(kf);
}

// a saved layout that doesnt fit the grid (hand edited, an older version) is thrown away for the factory one
bool valid(const side_layout *L, const std::string &s)
{
    if (static_cast<int>(s.size()) != L->rows * L->cols)
        return false;
    for (int r = 0; r < L->rows; ++r) {
        const char *row = s.c_str() + r * L->cols;
        const bool quad = row[0] == CAP_QUAD;
        for (int c = 0; c < L->cols; ++c) {
            const char x = row[c];
            if (!std::strchr(".sb<>#", x) || quad != (x == CAP_QUAD))
                return false;
            if (x == CAP_DOUBLE && (c % 2 || row[c + 1] != CAP_DOUBLE_2))
                return false;
            if (x == CAP_DOUBLE_2 && (c % 2 == 0 || row[c - 1] != CAP_DOUBLE))
                return false;
        }
    }
    return true;
}

// the slots a cap covers when it is put onto slot i
std::vector<int> footprint(const side_layout *L, cap c, int i)
{
    const int r = i / L->cols, col = i % L->cols;
    if (c == CAP_DOUBLE) {
        const int at = r * L->cols + col / 2 * 2;
        return {at, at + 1};
    }
    if (c == CAP_QUAD) {
        std::vector<int> row;
        for (int k = 0; k < L->cols; ++k)
            row.push_back(r * L->cols + k);
        return row;
    }
    return {i};
}

// the slots of the cap that sits on slot i (both halves of a double, the whole row under a blank row)
std::vector<int> piece_at(const side_panel *p, int i)
{
    const char c = p->slots[static_cast<size_t>(i)];
    if (c == CAP_EMPTY)
        return {};
    return footprint(p->L, c == CAP_DOUBLE_2 ? CAP_DOUBLE : static_cast<cap>(c), i);
}

void take_out(side_panel *p, int i)
{
    for (int k : piece_at(p, i))
        p->slots[static_cast<size_t>(k)] = CAP_EMPTY;
}

// a cap onto slot i, whatever was in its way comes out
void put(side_panel *p, cap c, int i)
{
    std::vector<int> fp = footprint(p->L, c, i);
    for (int k : fp)
        take_out(p, k);
    for (size_t k = 0; k < fp.size(); ++k)
        p->slots[static_cast<size_t>(fp[k])] = c == CAP_DOUBLE ? (k == 0 ? CAP_DOUBLE : CAP_DOUBLE_2) : c;
}

// the slot whose switch the cap on slot i presses, -1 for a blank or nothing. a double has a switch under each half
// and presses the one on the outside of the row: on slots 1+2 it is 1, on 3+4 it is 4 (tried on a real Nyth: 1+2 types
// 1, 3+4 types 4, 5+6 types 5, 7+8 types 8, 9+10 types 9, 11+12 types =)
int pressed_slot(const side_panel *p, int i)
{
    const char c = p->slots[static_cast<size_t>(i)];
    if (c == CAP_BUTTON)
        return i;
    if (c != CAP_DOUBLE && c != CAP_DOUBLE_2)
        return -1;
    const int front = c == CAP_DOUBLE ? i : i - 1;
    return front % p->L->cols < p->L->cols / 2 ? front : front + 1;
}

// where a cap could go: the slot of the front of every footprint it can have
bool anchor(const side_layout *L, cap c, int i)
{
    const int col = i % L->cols;
    return c == CAP_DOUBLE ? col % 2 == 0 : c == CAP_QUAD ? col == 0 : true;
}

// the first place with room for the whole cap, the box of caps shows it with the picture (and number) it would have
// there. 0 when the side is full
int free_slot(const side_panel *p, cap c)
{
    const int n = p->L->rows * p->L->cols;
    for (int i = 0; i < n; ++i) {
        if (!anchor(p->L, c, i))
            continue;
        bool room = true;
        for (int k : footprint(p->L, c, i))
            room = room && p->slots[static_cast<size_t>(k)] == CAP_EMPTY;
        if (room)
            return i;
    }
    return 0;
}

// the picture of cap c lying on slot i (the first slot of its footprint), nullptr when the build has none
cairo_surface_t *piece_picture(const side_panel *p, cap c, int i)
{
    const int r = i / p->L->cols, col = i % p->L->cols;
    char name[32];
    switch (c) {
    case CAP_BUTTON: std::snprintf(name, sizeof name, "side-button-%02d", i + 1); break;
    case CAP_BLANK: std::snprintf(name, sizeof name, "side-blank-%02d", i + 1); break;
    case CAP_DOUBLE: std::snprintf(name, sizeof name, "side-double-%d", r * (p->L->cols / 2) + col / 2 + 1); break;
    case CAP_QUAD: std::snprintf(name, sizeof name, "side-quad-%d", r + 1); break;
    default: return nullptr;
    }
    auto it = p->pieces.find(name);
    return it == p->pieces.end() ? nullptr : it->second;
}

// where a set of slots is on the side picture, all of them in one box
box area(const side_panel *p, const std::vector<int> &slots)
{
    box b{1e9, 1e9, -1e9, -1e9};
    for (int i : slots) {
        const box &s = p->slot_box[static_cast<size_t>(i)];
        b.x0 = std::min(b.x0, s.x0 + p->px);
        b.y0 = std::min(b.y0, s.y0 + p->py);
        b.x1 = std::max(b.x1, s.x1 + p->px);
        b.y1 = std::max(b.y1, s.y1 + p->py);
    }
    return b;
}

// a picture with nothing on it (a stand in of the free set)
bool see_through(cairo_surface_t *s)
{
    if (!s || cairo_surface_get_type(s) != CAIRO_SURFACE_TYPE_IMAGE)
        return true;
    cairo_surface_flush(s);
    const unsigned char *d = cairo_image_surface_get_data(s);
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s),
              stride = cairo_image_surface_get_stride(s);
    for (int y = 0; d && y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (reinterpret_cast<const uint32_t *>(d + y * stride)[x] >> 24)
                return false;
    return true;
}

double fit(const side_panel *p, GtkWidget *w, cairo_t *cr, double *left, double *top)
{
    double aw = gtk_widget_get_allocated_width(w), ah = gtk_widget_get_allocated_height(w);
    double k = std::min(MAX_SCALE, std::min(aw / p->width, ah / p->height));
    *left = (aw - p->width * k) / 2;
    *top = (ah - p->height * k) / 2;
    if (cr) {
        cairo_translate(cr, *left, *top);
        cairo_scale(cr, k, k);
    }
    return k;
}

int slot_at(const side_panel *p, GtkWidget *w, double px, double py)
{
    double left = 0, top = 0, k = fit(p, w, nullptr, &left, &top);
    double x = (px - left) / k, y = (py - top) / k;
    for (int i = 0; i < p->L->rows * p->L->cols; ++i) {
        box b = area(p, {i});
        if (x >= b.x0 - 1 && x < b.x1 + 1 && y >= b.y0 - 1 && y < b.y1 + 1)
            return i;
    }
    return -1;
}

void outline(cairo_t *cr, const box &b, double width, double r, double g, double bl, double a)
{
    cairo_new_path(cr);
    rounded_rect(cr, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, 4);
    cairo_set_line_width(cr, width);
    cairo_set_source_rgba(cr, r, g, bl, a);
    cairo_stroke(cr);
}

// a cap lying on slot i with the picture it has. without one (free set) its outline, blanks a little filled in
void draw_piece(cairo_t *cr, const side_panel *p, cap c, int i, double alpha)
{
    if (cairo_surface_t *pic = p->art ? piece_picture(p, c, i) : nullptr) {
        cairo_set_source_surface(cr, pic, p->px, p->py);
        cairo_paint_with_alpha(cr, alpha);
        return;
    }
    box b = area(p, footprint(p->L, c, i));
    b = {b.x0 + 1, b.y0 + 1, b.x1 - 1, b.y1 - 1};
    if (c == CAP_BLANK || c == CAP_QUAD) {
        cairo_new_path(cr);
        rounded_rect(cr, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, 4);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.25 * alpha);
        cairo_fill(cr);
    }
    outline(cr, b, 1.2, 0.6, 0.62, 0.66, 0.9 * alpha);
}

// the same marker the top view has on its buttons, with the number Swarm gives the slot
void draw_marker(cairo_t *cr, double cx, double cy, int number, bool on)
{
    const double r = 10.5; // the side is shown at its own size (MAX_SCALE), so these are about the size of the top view ones
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    if (on)
        cairo_set_source_rgba(cr, 0.32, 0.67, 0.87, 0.85);
    else
        cairo_set_source_rgba(cr, 0, 0, 0, 0.62);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, on ? 2 : 1);
    if (on)
        cairo_set_source_rgb(cr, 1, 1, 1);
    else
        cairo_set_source_rgb(cr, 0.32, 0.67, 0.87);
    cairo_stroke(cr);
    char t[12];
    std::snprintf(t, sizeof t, "%d", number);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, r * 0.95);
    cairo_text_extents_t e;
    cairo_text_extents(cr, t, &e);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, cx - e.width / 2 - e.x_bearing, cy - e.height / 2 - e.y_bearing);
    cairo_show_text(cr, t);
}

gboolean on_draw_side(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    if (!p)
        return FALSE;
    const int n = p->L->rows * p->L->cols;
    double left = 0, top = 0;
    fit(p, w, cr, &left, &top);
    cairo_set_source_surface(cr, p->picture, 0, 0);
    cairo_paint(cr);
    // without pictures there is nothing that shows where the slots are
    if (!p->art)
        for (int i = 0; i < n; ++i) {
            box b = area(p, {i});
            cairo_save(cr);
            const double dash[] = {3, 2};
            cairo_set_dash(cr, dash, 2, 0);
            outline(cr, b, 0.8, 0.6, 0.62, 0.66, 0.5);
            cairo_restore(cr);
        }

    // the caps that are in. the one being dragged stays where it was (faint) until it lands somewhere
    std::vector<int> moving = p->drag_from >= 0 ? piece_at(p, p->drag_from) : std::vector<int>{};
    for (int i = 0; i < n; ++i) {
        const char c = p->slots[static_cast<size_t>(i)];
        if (c == CAP_EMPTY || c == CAP_DOUBLE_2 || (c == CAP_QUAD && i % p->L->cols))
            continue;
        const bool faint = std::find(moving.begin(), moving.end(), i) != moving.end();
        draw_piece(cr, p, static_cast<cap>(c), i, faint ? 0.35 : 1);
    }
    // the buttons you can press get a marker, the one that is looked at its outline too
    // (a double gets its marker over the switch it presses)
    for (int i = 0; i < n; ++i) {
        if (pressed_slot(p, i) != i)
            continue;
        const bool on = p->L->first_input + i == s->highlighted_button;
        box b = area(p, piece_at(p, i));
        if (on)
            outline(cr, b, 2, 0.32, 0.67, 0.87, 1);
        box at = area(p, {i});
        draw_marker(cr, (at.x0 + at.x1) / 2, (at.y0 + at.y1) / 2, i + 1, on);
    }
    // where the dragged cap would land
    if (p->drag != CAP_EMPTY && p->hover_slot >= 0) {
        std::vector<int> fp = footprint(p->L, p->drag, p->hover_slot);
        draw_piece(cr, p, p->drag, fp.front(), 0.7);
        outline(cr, area(p, fp), 2, 0.32, 0.67, 0.87, 1);
    }
    return FALSE;
}

// a cap the way the box of caps and the drag icon show it: the picture of it on slot i, cut out of the piece
// picture, at scale k
void paint_loose(cairo_t *cr, const side_panel *p, cap c, int i, double k)
{
    box b = area(p, footprint(p->L, c, i));
    cairo_save(cr);
    cairo_scale(cr, k, k);
    cairo_translate(cr, -b.x0, -b.y0);
    cairo_rectangle(cr, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
    cairo_clip(cr);
    draw_piece(cr, p, c, i, 1);
    cairo_restore(cr);
}

void loose_size(const side_panel *p, cap c, int i, double k, int *w, int *h)
{
    box b = area(p, footprint(p->L, c, i));
    *w = static_cast<int>(std::ceil((b.x1 - b.x0) * k));
    *h = static_cast<int>(std::ceil((b.y1 - b.y0) * k));
}

void changed(app_state *s)
{
    save(s->side);
    side_apply_rows(s);
    if (s->side_view)
        gtk_widget_queue_draw(s->side_view);
    // the box of caps shows the next free place
    for (GtkWidget *w : s->side->box_pics)
        gtk_widget_queue_draw(w);
}

// the icon under the pointer while dragging: the cap at the size it has on the screen right now
void set_drag_icon(app_state *s, GdkDragContext *ctx, cap c)
{
    side_panel *p = s->side;
    double k = BOX_SCALE, left = 0, top = 0;
    if (s->side_view && gtk_widget_get_allocated_width(s->side_view) > 1)
        k = fit(p, s->side_view, nullptr, &left, &top);
    // a cap off the mouse looks like it did there, one out of the box like it does in the box
    const int at = p->drag_from >= 0 ? piece_at(p, p->drag_from).front() : free_slot(p, c);
    int w = 0, h = 0;
    loose_size(p, c, at, k, &w, &h);
    cairo_surface_t *icon = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w + 2, h + 2);
    cairo_t *cr = cairo_create(icon);
    cairo_translate(cr, 1, 1);
    paint_loose(cr, p, c, at, k);
    cairo_destroy(cr);
    // held where a single cap would be, the drop goes by the slot under the pointer
    box first = area(p, {0});
    cairo_surface_set_device_offset(icon, -(first.x1 - first.x0) * k / 2, -(h + 2) / 2.0);
    gtk_drag_set_icon_surface(ctx, icon);
    cairo_surface_destroy(icon);
}

void on_drag_begin(GtkWidget *, GdkDragContext *ctx, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->side->dropped = s->side->cancelled = false;
    set_drag_icon(s, ctx, s->side->drag);
    if (s->side_view)
        gtk_widget_queue_draw(s->side_view);
}

gboolean on_drag_failed(GtkWidget *, GdkDragContext *, GtkDragResult result, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->side->cancelled = result != GTK_DRAG_RESULT_NO_TARGET;
    // dropped next to the mouse: no sliding back, it simply comes out
    return result == GTK_DRAG_RESULT_NO_TARGET;
}

void on_drag_end(GtkWidget *, GdkDragContext *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    const bool out = p->drag_from >= 0 && !p->dropped && !p->cancelled;
    if (out)
        take_out(p, p->drag_from);
    p->drag = CAP_EMPTY;
    p->drag_from = -1;
    p->hover_slot = -1;
    if (out)
        changed(s);
    else if (s->side_view)
        gtk_widget_queue_draw(s->side_view);
}

// the data itself never travels (same window), what is dragged is in s->side
void on_drag_data_get(GtkWidget *, GdkDragContext *, GtkSelectionData *sel, guint, guint, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const char c = static_cast<char>(s->side->drag);
    gtk_selection_data_set(sel, gtk_selection_data_get_target(sel), 8, reinterpret_cast<const guchar *>(&c), 1);
}

gboolean on_box_press(GtkWidget *w, GdkEventButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->side->drag = static_cast<cap>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cap")));
    s->side->drag_from = -1;
    return FALSE;
}

gboolean on_draw_box_cap(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    const cap c = static_cast<cap>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cap")));
    const int at = free_slot(s->side, c);
    int cw = 0, ch = 0;
    loose_size(s->side, c, at, BOX_SCALE, &cw, &ch);
    cairo_translate(cr, (gtk_widget_get_allocated_width(w) - cw) / 2.0, (gtk_widget_get_allocated_height(w) - ch) / 2.0);
    paint_loose(cr, s->side, c, at, BOX_SCALE);
    return FALSE;
}

gboolean on_side_motion(GtkWidget *w, GdkEventMotion *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    const int slot = slot_at(p, w, e->x, e->y);
    const char c = slot >= 0 ? p->slots[static_cast<size_t>(slot)] : static_cast<char>(CAP_EMPTY);
    if (!p->pressing) {
        const int at = slot >= 0 ? pressed_slot(p, slot) : -1;
        set_highlight(s, at >= 0 ? p->L->first_input + at : -1);
    }
    if (GdkWindow *win = gtk_widget_get_window(w)) {
        GdkCursor *hand = c != CAP_EMPTY ? gdk_cursor_new_from_name(gdk_window_get_display(win), "grab") : nullptr;
        gdk_window_set_cursor(win, hand);
        if (hand)
            g_object_unref(hand);
    }
    // a press on a cap that moved far enough is a drag of that cap
    if (p->pressing && gtk_drag_check_threshold(w, static_cast<int>(p->press_x), static_cast<int>(p->press_y),
                                                static_cast<int>(e->x), static_cast<int>(e->y))) {
        p->pressing = false;
        const char pc = p->slots[static_cast<size_t>(p->press_slot)];
        p->drag = pc == CAP_DOUBLE_2 ? CAP_DOUBLE : static_cast<cap>(pc);
        p->drag_from = p->press_slot;
        GtkTargetList *targets = gtk_target_list_new(&TARGET, 1);
        gtk_drag_begin_with_coordinates(w, targets, GDK_ACTION_MOVE, 1, reinterpret_cast<GdkEvent *>(e),
                                        static_cast<int>(p->press_x), static_cast<int>(p->press_y));
        gtk_target_list_unref(targets);
    }
    return FALSE;
}

gboolean on_side_press(GtkWidget *w, GdkEventButton *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    if (e->type != GDK_BUTTON_PRESS)
        return FALSE;
    const int slot = slot_at(p, w, e->x, e->y);
    if (slot < 0 || p->slots[static_cast<size_t>(slot)] == CAP_EMPTY)
        return FALSE;
    if (e->button == 3) {
        // right click takes the cap out
        take_out(p, slot);
        set_highlight(s, -1);
        changed(s);
        return TRUE;
    }
    if (e->button == 1) {
        p->pressing = true;
        p->press_slot = slot;
        p->press_x = e->x;
        p->press_y = e->y;
    }
    return FALSE;
}

// a click without a drag on a button: its row in the list gets the focus, like a marker on the top view
gboolean on_side_release(GtkWidget *, GdkEventButton *e, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    if (e->button != 1 || !p->pressing)
        return FALSE;
    p->pressing = false;
    const int slot = pressed_slot(p, p->press_slot);
    const size_t input = static_cast<size_t>(p->L->first_input + slot);
    if (slot >= 0 && input < s->buttons.size() && s->buttons[input].combo)
        gtk_widget_grab_focus(s->buttons[input].combo);
    return FALSE;
}

gboolean on_side_leave(GtkWidget *, GdkEventCrossing *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->side->pressing)
        set_highlight(s, -1);
    return FALSE;
}

gboolean on_side_drag_motion(GtkWidget *w, GdkDragContext *ctx, int x, int y, guint time, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    const int slot = p->drag != CAP_EMPTY ? slot_at(p, w, x, y) : -1;
    if (slot != p->hover_slot) {
        p->hover_slot = slot;
        gtk_widget_queue_draw(w);
    }
    gdk_drag_status(ctx, slot >= 0 ? GDK_ACTION_MOVE : static_cast<GdkDragAction>(0), time);
    return TRUE;
}

void on_side_drag_leave(GtkWidget *w, GdkDragContext *, guint, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->side->hover_slot = -1;
    gtk_widget_queue_draw(w);
}

gboolean on_side_drop(GtkWidget *w, GdkDragContext *ctx, int x, int y, guint time, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    const int slot = p->drag != CAP_EMPTY ? slot_at(p, w, x, y) : -1;
    if (slot < 0)
        return FALSE;
    if (p->drag_from >= 0)
        take_out(p, p->drag_from);
    put(p, p->drag, slot);
    p->dropped = true;
    p->hover_slot = -1;
    gtk_drag_finish(ctx, TRUE, FALSE, time);
    changed(s);
    return TRUE;
}

// a cap dragged back into the box comes out of the mouse
gboolean on_box_drag_motion(GtkWidget *, GdkDragContext *ctx, int, int, guint time, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    gdk_drag_status(ctx, s->side->drag_from >= 0 ? GDK_ACTION_MOVE : static_cast<GdkDragAction>(0), time);
    return TRUE;
}

gboolean on_box_drop(GtkWidget *, GdkDragContext *ctx, int, int, guint time, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    side_panel *p = s->side;
    if (p->drag_from < 0)
        return FALSE;
    take_out(p, p->drag_from);
    p->dropped = true;
    gtk_drag_finish(ctx, TRUE, FALSE, time);
    changed(s);
    return TRUE;
}

// takes every cap out, to start putting them in from nothing
// a button on every slot, the way the mouse comes out of the box
void on_side_reset(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->side->slots.assign(s->side->slots.size(), CAP_BUTTON);
    changed(s);
}

void on_side_clear(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->side->slots.assign(s->side->slots.size(), CAP_EMPTY);
    set_highlight(s, -1);
    changed(s);
}

void hook_drag_source(app_state *s, GtkWidget *w)
{
    g_signal_connect(w, "drag-begin", G_CALLBACK(on_drag_begin), s);
    g_signal_connect(w, "drag-end", G_CALLBACK(on_drag_end), s);
    g_signal_connect(w, "drag-failed", G_CALLBACK(on_drag_failed), s);
    g_signal_connect(w, "drag-data-get", G_CALLBACK(on_drag_data_get), s);
}

bool four_numbers(const char *text, double out[4])
{
    const char *at = text;
    for (int k = 0; k < 4; ++k) {
        char *end = nullptr;
        // (g_ascii_strtod and not sscanf: a german locale would read 116.5 as 116)
        out[k] = g_ascii_strtod(at, &end);
        if (end == at || (k < 3 && *end != ';'))
            return false;
        at = end + 1;
    }
    return true;
}

} // namespace

side_panel *side_panel_load(int pid)
{
    const side_layout *L = nullptr;
    for (const side_layout &l : LAYOUTS)
        if (l.pid == pid)
            L = &l;
    if (!L)
        return nullptr;
    char dir[96];
    std::snprintf(dir, sizeof dir, "/org/rockcat/swarm/assets/views/%04x/", pid);
    GBytes *bytes = g_resources_lookup_data((std::string(dir) + "view.ini").c_str(), G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (!bytes)
        return nullptr;
    GKeyFile *kf = g_key_file_new();
    gsize len = 0;
    const char *text = static_cast<const char *>(g_bytes_get_data(bytes, &len));
    const bool ok = g_key_file_load_from_data(kf, text, len, G_KEY_FILE_NONE, nullptr);
    g_bytes_unref(bytes);

    auto *p = new side_panel;
    p->L = L;
    const int n = L->rows * L->cols;
    p->slot_box.resize(static_cast<size_t>(n));
    bool good = ok && g_key_file_has_group(kf, "side");
    if (good) {
        char *name = g_key_file_get_string(kf, "side", "picture", nullptr);
        p->picture = name ? asset_surface(std::string(dir) + name) : nullptr;
        g_free(name);
        p->width = g_key_file_get_integer(kf, "side", "width", nullptr);
        p->height = g_key_file_get_integer(kf, "side", "height", nullptr);
        char *at = g_key_file_get_string(kf, "side", "pieces", nullptr);
        if (at) {
            char *end = nullptr;
            p->px = g_ascii_strtod(at, &end);
            p->py = end && *end == ';' ? g_ascii_strtod(end + 1, nullptr) : 0;
        }
        g_free(at);
        good = p->picture && p->width > 0 && p->height > 0;
        for (int i = 0; good && i < n; ++i) {
            char key[12];
            std::snprintf(key, sizeof key, "%d", i + 1);
            char *v = g_key_file_get_string(kf, "side-slots", key, nullptr);
            double b[4] = {};
            good = v && four_numbers(v, b);
            g_free(v);
            p->slot_box[static_cast<size_t>(i)] = {b[0], b[1], b[2], b[3]};
        }
    }
    g_key_file_unref(kf);
    if (!good) {
        side_panel_free(p);
        return nullptr;
    }
    // every piece there is a picture of. art = at least one of them (and the side) has something on it
    p->art = !see_through(p->picture);
    auto load = [&](const char *fmt, int count) {
        for (int k = 1; k <= count; ++k) {
            char name[32];
            std::snprintf(name, sizeof name, fmt, k);
            if (cairo_surface_t *sf = asset_surface(std::string(dir) + name + ".png"))
                p->pieces[name] = sf;
        }
    };
    load("side-button-%02d", n);
    load("side-blank-%02d", n);
    load("side-double-%d", n / 2);
    load("side-quad-%d", L->rows);

    // out of the box every slot has a button
    p->slots.assign(static_cast<size_t>(n), CAP_BUTTON);
    GKeyFile *prefs = g_key_file_new();
    if (g_key_file_load_from_file(prefs, prefs_path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        if (char *v = g_key_file_get_string(prefs, key_of(p).c_str(), "slots", nullptr)) {
            if (valid(L, v))
                p->slots = v;
            g_free(v);
        }
    }
    g_key_file_unref(prefs);
    return p;
}

void side_panel_free(side_panel *p)
{
    if (!p)
        return;
    if (p->picture)
        cairo_surface_destroy(p->picture);
    for (auto &piece : p->pieces)
        cairo_surface_destroy(piece.second);
    delete p;
}

bool side_input(const app_state *s, int input)
{
    if (!s->side)
        return false;
    const int slot = input - s->side->L->first_input;
    return slot >= 0 && slot < s->side->L->rows * s->side->L->cols;
}

int side_number(const app_state *s, int input)
{
    return side_input(s, input) ? input - s->side->L->first_input + 1 : 0;
}

// false when the button is one of the side ones and nothing on the side presses its switch (a blank, no cap, the
// half of a double that isnt the one it presses). true for everything else
bool side_input_fitted(const app_state *s, int input)
{
    if (!side_input(s, input))
        return true;
    const int slot = input - s->side->L->first_input;
    return pressed_slot(s->side, slot) == slot;
}

GtkWidget *build_side_view(app_state *s)
{
    side_panel *p = s->side;
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    s->side_view = gtk_drawing_area_new();
    // room for the picture at its own size, it doesnt get any bigger than that (MAX_SCALE)
    gtk_widget_set_size_request(s->side_view, static_cast<int>(p->width), static_cast<int>(p->height));
    gtk_widget_set_hexpand(s->side_view, TRUE);
    gtk_widget_set_vexpand(s->side_view, TRUE);
    gtk_widget_add_events(s->side_view, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                            GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(s->side_view, "draw", G_CALLBACK(on_draw_side), s);
    g_signal_connect(s->side_view, "motion-notify-event", G_CALLBACK(on_side_motion), s);
    g_signal_connect(s->side_view, "button-press-event", G_CALLBACK(on_side_press), s);
    g_signal_connect(s->side_view, "button-release-event", G_CALLBACK(on_side_release), s);
    g_signal_connect(s->side_view, "leave-notify-event", G_CALLBACK(on_side_leave), s);
    gtk_drag_dest_set(s->side_view, static_cast<GtkDestDefaults>(0), &TARGET, 1, GDK_ACTION_MOVE);
    g_signal_connect(s->side_view, "drag-motion", G_CALLBACK(on_side_drag_motion), s);
    g_signal_connect(s->side_view, "drag-leave", G_CALLBACK(on_side_drag_leave), s);
    g_signal_connect(s->side_view, "drag-drop", G_CALLBACK(on_side_drop), s);
    hook_drag_source(s, s->side_view);
    gtk_box_pack_start(GTK_BOX(outer), s->side_view, TRUE, TRUE, 0);

    // the box of caps: drag one onto the side, drag one back in here to take it out
    GtkWidget *caps = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 22);
    gtk_widget_set_halign(caps, GTK_ALIGN_CENTER);
    for (cap c : PALETTE) {
        GtkWidget *item = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *pic = gtk_drawing_area_new();
        // big enough for the cap on any place it can show, the pictures differ by a few pixels
        int w = 0, h = 0;
        for (int i = 0; i < p->L->rows * p->L->cols; ++i) {
            int iw = 0, ih = 0;
            if (anchor(p->L, c, i))
                loose_size(p, c, i, BOX_SCALE, &iw, &ih);
            w = std::max(w, iw);
            h = std::max(h, ih);
        }
        gtk_widget_set_size_request(pic, w + 4, h + 4);
        p->box_pics.push_back(pic);
        g_object_set_data(G_OBJECT(pic), "cap", GINT_TO_POINTER(c));
        g_signal_connect(pic, "draw", G_CALLBACK(on_draw_box_cap), s);
        // a drawing area has no window of its own to take presses, the event box around it does
        GtkWidget *grab = gtk_event_box_new();
        g_object_set_data(G_OBJECT(grab), "cap", GINT_TO_POINTER(c));
        gtk_container_add(GTK_CONTAINER(grab), pic);
        gtk_widget_set_tooltip_text(grab, "Drag onto the side of the mouse");
        gtk_drag_source_set(grab, GDK_BUTTON1_MASK, &TARGET, 1, GDK_ACTION_MOVE);
        g_signal_connect(grab, "button-press-event", G_CALLBACK(on_box_press), s);
        hook_drag_source(s, grab);
        gtk_widget_set_valign(grab, GTK_ALIGN_END);
        gtk_box_pack_start(GTK_BOX(item), grab, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(item), label(cap_name(c), "rc-hint", 0.5f), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(caps), item, FALSE, FALSE, 0);
    }
    GtkWidget *box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(box), caps);
    gtk_drag_dest_set(box, static_cast<GtkDestDefaults>(0), &TARGET, 1, GDK_ACTION_MOVE);
    g_signal_connect(box, "drag-motion", G_CALLBACK(on_box_drag_motion), s);
    g_signal_connect(box, "drag-drop", G_CALLBACK(on_box_drop), s);
    gtk_box_pack_start(GTK_BOX(outer), box, FALSE, FALSE, 0);

    GtkWidget *hint = label("Drag the buttons to where they are on your mouse. To remove them, drag them out of the mouse (or right click them). "
                            "Empty/Covered buttons are not shown in the list to the right.",
                            "rc-hint", 0.5f);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 70);
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(outer), hint, FALSE, FALSE, 0);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_CENTER);
    GtkWidget *reset = gtk_button_new_with_label("RESET");
    gtk_widget_set_tooltip_text(reset, "A button on every slot, like out of the box");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_side_reset), s);
    GtkWidget *clear = gtk_button_new_with_label("CLEAR");
    gtk_widget_set_tooltip_text(clear, "Take every cap out");
    g_signal_connect(clear, "clicked", G_CALLBACK(on_side_clear), s);
    gtk_box_pack_start(GTK_BOX(buttons), reset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), buttons, FALSE, FALSE, 0);
    return outer;
}

} // namespace gui
