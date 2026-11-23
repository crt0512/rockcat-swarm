// the two things the gui remembers about itself (tray, start minimized), in the config dir

#include "app.h"

namespace gui {

std::string prefs_path()
{
    return std::string(rockcat_config_dir()) + "/gui.ini";
}

gui_prefs load_prefs()
{
    gui_prefs p;
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, prefs_path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        // on unless somebody said no, a file from before this option existed has no say in it
        if (g_key_file_has_key(kf, "gui", "tray_icon", nullptr))
            p.tray_icon = g_key_file_get_boolean(kf, "gui", "tray_icon", nullptr);
        p.tray_style = std::clamp(g_key_file_get_integer(kf, "gui", "tray_style", nullptr), 0, 2);
        p.tray = g_key_file_get_boolean(kf, "gui", "tray", nullptr);
        p.start_minimized = g_key_file_get_boolean(kf, "gui", "start_minimized", nullptr);
        p.auto_apply = g_key_file_get_boolean(kf, "gui", "auto_apply", nullptr);
    }
    g_key_file_unref(kf);
    return p;
}

void save_prefs(const gui_prefs &p)
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, prefs_path().c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_boolean(kf, "gui", "tray_icon", p.tray_icon);
    g_key_file_set_integer(kf, "gui", "tray_style", p.tray_style);
    g_key_file_set_boolean(kf, "gui", "tray", p.tray);
    g_key_file_set_boolean(kf, "gui", "start_minimized", p.start_minimized);
    g_key_file_remove_key(kf, "gui", "engine", nullptr); // pre-1.0.1 setting
    g_key_file_set_boolean(kf, "gui", "auto_apply", p.auto_apply);
    g_mkdir_with_parents(rockcat_config_dir(), 0755);
    g_key_file_save_to_file(kf, prefs_path().c_str(), nullptr);
    g_key_file_unref(kf);
}

} // namespace gui
