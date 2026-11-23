// rockcat-swarm GTK3 main window.
#pragma once

#include <gtk/gtk.h>

/* Builds the main window and shows it - unless the tray is enabled and the
 * app should start minimized (preference or force_minimized/--minimized). */
GtkWidget *rockcat_window_new(GtkApplication *app, bool force_minimized);

/* Switches to "settings", "buttons", "illumination" or "advanced". */
void rockcat_window_show_page(GtkWidget *window, const char *page);

/* Shows that device (a product id as hex, or a hidraw path) if it is plugged in. */
void rockcat_window_show_device(GtkWidget *window, const char *which);

/* Picks that effect (a rockcat_effect number) on the illumination page, as if somebody had chosen it. Nothing gets
 * applied. For looking at the gui from a script. */
void rockcat_window_pick_effect(GtkWidget *window, int effect);

/* Picks that key (its input number) on the key assignment page of a keyboard. For scripts too. */
void rockcat_window_pick_key(GtkWidget *window, int input);

/* Adds the debug menu (pretend devices to plug in) to the header. */
void rockcat_window_set_debug(GtkWidget *window, bool on);

/* Opens that profile (from 0) for editing once a device is connected, instead of the active one. */
void rockcat_window_edit_profile(GtkWidget *window, int profile);
