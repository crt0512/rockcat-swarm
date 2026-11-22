// rockcat-swarm GTK3 main window.
#pragma once

#include <gtk/gtk.h>

/* Builds the main window and shows it - unless the tray is enabled and the
 * app should start minimized (preference or force_minimized/--minimized). */
GtkWidget *rockcat_window_new(GtkApplication *app, bool force_minimized);

/* Switches to "settings", "buttons", "illumination" or "advanced". */
void rockcat_window_show_page(GtkWidget *window, const char *page);
