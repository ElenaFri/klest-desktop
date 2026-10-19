/*
 * Klest Settings — a small GTK 4 tool to edit Klest's configuration (ADR 0005).
 *
 * It reads and writes the same TOML file the compositor loads (ADR 0007), using
 * the shared `klest_config` module. GTK lives only here: it is not a dependency
 * of the compositor, which keeps the core lean.
 */

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>

#include "config/config.h"

struct klest_settings {
	char path[4096];
	GtkColorDialogButton *color;
	GtkSpinButton *rate;
	GtkSpinButton *delay;
	GtkEntry *terminal;
	GtkLabel *status;
};

static void on_save(GtkButton *button, gpointer data)
{
	(void)button;
	struct klest_settings *s = data;

	struct klest_config cfg;
	/* Load the existing file so fields the UI does not edit (e.g. the
	 * autostart list) survive the save; falls back to defaults if absent. */
	klest_config_load(&cfg, s->path);

	const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(s->color);
	cfg.background_color[0] = rgba->red;
	cfg.background_color[1] = rgba->green;
	cfg.background_color[2] = rgba->blue;
	cfg.background_color[3] = rgba->alpha;

	cfg.keyboard_repeat_rate = gtk_spin_button_get_value_as_int(s->rate);
	cfg.keyboard_repeat_delay = gtk_spin_button_get_value_as_int(s->delay);

	const char *term = gtk_editable_get_text(GTK_EDITABLE(s->terminal));
	snprintf(cfg.terminal, sizeof(cfg.terminal), "%s", term);

	if (klest_config_save(&cfg, s->path)) {
		char msg[4200];
		snprintf(msg, sizeof(msg), "Saved to %s", s->path);
		gtk_label_set_text(s->status, msg);
	} else {
		gtk_label_set_text(s->status, "Save failed — see the terminal for details.");
	}
}

/* Adds a "label: widget" row to `grid` at `row`. */
static void attach_row(GtkGrid *grid, int row, const char *label, GtkWidget *widget)
{
	GtkWidget *lbl = gtk_label_new(label);
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	gtk_widget_set_hexpand(widget, TRUE);
	gtk_grid_attach(grid, lbl, 0, row, 1, 1);
	gtk_grid_attach(grid, widget, 1, row, 1, 1);
}

static void activate(GtkApplication *app, gpointer data)
{
	struct klest_settings *s = data;

	struct klest_config cfg;
	klest_config_load(&cfg, NULL);
	if (!klest_config_default_path(s->path, sizeof(s->path))) {
		snprintf(s->path, sizeof(s->path), "config.toml");
	}

	GtkWidget *window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Klest Settings");
	gtk_window_set_default_size(GTK_WINDOW(window), 460, -1);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_window_set_child(GTK_WINDOW(window), box);

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_append(GTK_BOX(box), grid);

	/* Background colour. */
	GdkRGBA initial = {
		cfg.background_color[0], cfg.background_color[1],
		cfg.background_color[2], cfg.background_color[3],
	};
	GtkColorDialog *dialog = gtk_color_dialog_new();
	GtkWidget *color = gtk_color_dialog_button_new(dialog);
	gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(color), &initial);
	s->color = GTK_COLOR_DIALOG_BUTTON(color);
	attach_row(GTK_GRID(grid), 0, "Background colour", color);

	/* Keyboard repeat rate. */
	GtkWidget *rate = gtk_spin_button_new_with_range(1, 1000, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(rate), cfg.keyboard_repeat_rate);
	s->rate = GTK_SPIN_BUTTON(rate);
	attach_row(GTK_GRID(grid), 1, "Key repeat rate (/s)", rate);

	/* Keyboard repeat delay. */
	GtkWidget *delay = gtk_spin_button_new_with_range(1, 60000, 10);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(delay), cfg.keyboard_repeat_delay);
	s->delay = GTK_SPIN_BUTTON(delay);
	attach_row(GTK_GRID(grid), 2, "Key repeat delay (ms)", delay);

	/* Terminal command (Alt+Return). */
	GtkWidget *terminal = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(terminal), cfg.terminal);
	gtk_entry_set_placeholder_text(GTK_ENTRY(terminal), "e.g. foot");
	s->terminal = GTK_ENTRY(terminal);
	attach_row(GTK_GRID(grid), 3, "Terminal (Alt+Return)", terminal);

	/* Save button + status line. */
	GtkWidget *save = gtk_button_new_with_label("Save");
	gtk_widget_add_css_class(save, "suggested-action");
	gtk_widget_set_halign(save, GTK_ALIGN_END);
	g_signal_connect(save, "clicked", G_CALLBACK(on_save), s);
	gtk_box_append(GTK_BOX(box), save);

	GtkWidget *status = gtk_label_new("");
	gtk_widget_set_halign(status, GTK_ALIGN_START);
	gtk_widget_add_css_class(status, "dim-label");
	s->status = GTK_LABEL(status);
	gtk_box_append(GTK_BOX(box), status);

	gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[])
{
	struct klest_settings s = {0};

	GtkApplication *app =
		gtk_application_new("org.klest.Settings", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), &s);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
