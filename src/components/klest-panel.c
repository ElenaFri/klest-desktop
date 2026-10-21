/*
 * Klest — minimal Wayland panel (ADR 0009).
 *
 * A top layer-shell bar with a small bitmap label, workspace indicators and a
 * local clock. Workspaces are read from the compositor's
 * klest_workspace_manager_v1 global; clicking an indicator switches workspace.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "klest-workspace-v1-client-protocol.h"
#include "wayland/font.h"
#include "wayland/layer_client.h"
#include "wayland/shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define KLEST_PANEL_HEIGHT 36U
#define KLEST_GLYPH_SCALE 2U
#define KLEST_WS_ORIGIN_X 96U
#define KLEST_WS_BOX_WIDTH 26U
#define KLEST_PANEL_MAX_WORKSPACES 9U
#define KLEST_WINDOW_ORIGIN_X (KLEST_WS_ORIGIN_X + \
	KLEST_PANEL_MAX_WORKSPACES * KLEST_WS_BOX_WIDTH + 8U)
#define KLEST_WINDOW_BOX_WIDTH 144U
#define KLEST_WINDOW_LABEL_CHARS 14U
#define KLEST_WINDOW_TITLE_LENGTH 128U

struct panel_window {
	struct panel_window *next;
	uint32_t id;
	uint32_t workspace;
	char title[KLEST_WINDOW_TITLE_LENGTH];
	bool active;
	bool minimized;
	bool maximized;
};

struct panel {
	struct klest_layer_client layer;
	struct wl_registry *registry;
	struct klest_workspace_manager_v1 *workspace_manager;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_buffer *buffer;
	uint32_t mapped_width;
	uint32_t mapped_height;
	uint32_t workspace_count;
	uint32_t active_workspace;
	struct panel_window *windows;
	double pointer_x;
	bool needs_redraw;
	time_t drawn_minute;
};


static bool get_clock_text(char text[6], time_t *minute)
{
	time_t now = time(NULL);
	struct tm local_time;
	if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL ||
			strftime(text, 6, "%H:%M", &local_time) == 0) {
		fprintf(stderr, "klest-panel: cannot read local time\n");
		return false;
	}
	*minute = now / 60;
	return true;
}

static void fill_rect(const struct klest_canvas *canvas, uint32_t x,
	uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
	klest_canvas_fill(canvas, x, y, w, h, color);
}

static void draw_text(const struct klest_canvas *canvas, const char *text,
	uint32_t x, uint32_t y, uint32_t scale, uint32_t color)
{
	klest_font_draw(canvas, text, x, y, scale, color);
}

/* Returns the number of workspace indicators that fit, capped for safety. */
static uint32_t visible_workspaces(const struct panel *panel)
{
	uint32_t count = panel->workspace_count;
	return count < KLEST_PANEL_MAX_WORKSPACES ?
		count : KLEST_PANEL_MAX_WORKSPACES;
}

static void draw_workspaces(const struct panel *panel,
	const struct klest_canvas *canvas, uint32_t text_y)
{
	const uint32_t active_bg = 0xff5c8fe8U;
	const uint32_t box_bg = 0xff2a2d3eU;
	const uint32_t text_color = 0xffe8eaf5U;
	uint32_t count = visible_workspaces(panel);
	for (uint32_t i = 0; i < count; i++) {
		uint32_t box_x = KLEST_WS_ORIGIN_X + i * KLEST_WS_BOX_WIDTH;
		if (box_x + KLEST_WS_BOX_WIDTH > canvas->width) {
			break;
		}
		uint32_t bg = i == panel->active_workspace ? active_bg : box_bg;
		fill_rect(canvas, box_x + 1U, 4U, KLEST_WS_BOX_WIDTH - 2U,
			canvas->height - 8U, bg);
		char label[2] = {(char)('1' + i), '\0'};
		draw_text(canvas, label, box_x + 8U, text_y,
			KLEST_GLYPH_SCALE, text_color);
	}
}

static uint32_t panel_clock_x(uint32_t width)
{
	uint32_t clock_width = klest_font_width("00:00", KLEST_GLYPH_SCALE);
	return width > clock_width + 16U ? width - clock_width - 16U : 0U;
}

static void draw_windows(const struct panel *panel,
	const struct klest_canvas *canvas, uint32_t clock_x)
{
	uint32_t x = KLEST_WINDOW_ORIGIN_X;
	uint32_t right = clock_x > 8U ? clock_x - 8U : 0U;
	for (const struct panel_window *window = panel->windows;
			window != NULL; window = window->next) {
		if (window->workspace != panel->active_workspace) {
			continue;
		}
		if (x + KLEST_WINDOW_BOX_WIDTH > right) {
			break;
		}
		uint32_t background = window->active ? 0xff5c8fe8U :
			(window->minimized ? 0xff20222cU : 0xff2a2d3eU);
		fill_rect(canvas, x, 4U, KLEST_WINDOW_BOX_WIDTH - 4U,
			canvas->height - 8U, background);
		char label[KLEST_WINDOW_LABEL_CHARS + 1U];
		size_t length = 0;
		while (length < KLEST_WINDOW_LABEL_CHARS &&
				window->title[length] != '\0') {
			label[length] = window->title[length];
			length++;
		}
		if (length == KLEST_WINDOW_LABEL_CHARS &&
				window->title[length] != '\0') {
			label[length - 3U] = '.';
			label[length - 2U] = '.';
			label[length - 1U] = '.';
		}
		label[length] = '\0';
		uint32_t text_width = klest_font_width(label, 1U);
		uint32_t text_x = x + (KLEST_WINDOW_BOX_WIDTH - text_width) / 2U;
		uint32_t text_y = (canvas->height - KLEST_FONT_GLYPH) / 2U;
		draw_text(canvas, label, text_x, text_y, 1U, 0xffe8eaf5U);
		x += KLEST_WINDOW_BOX_WIDTH;
	}
}

static struct panel_window *window_by_id(struct panel *panel, uint32_t id)
{
	for (struct panel_window *window = panel->windows;
			window != NULL; window = window->next) {
		if (window->id == id) {
			return window;
		}
	}
	return NULL;
}

static struct panel_window *window_at(struct panel *panel, double pointer_x)
{
	uint32_t x = KLEST_WINDOW_ORIGIN_X;
	uint32_t right = panel_clock_x(panel->layer.width);
	if (right > 8U) {
		right -= 8U;
	}
	for (struct panel_window *window = panel->windows;
			window != NULL; window = window->next) {
		if (window->workspace != panel->active_workspace) {
			continue;
		}
		if (x + KLEST_WINDOW_BOX_WIDTH > right) {
			break;
		}
		if (pointer_x >= (double)x &&
				pointer_x < (double)(x + KLEST_WINDOW_BOX_WIDTH - 4U)) {
			return window;
		}
		x += KLEST_WINDOW_BOX_WIDTH;
	}
	return NULL;
}

static bool draw_panel(struct panel *panel)
{
	uint32_t width = panel->layer.width;
	uint32_t height = panel->layer.height;
	if (width == 0 || height == 0 || width > (uint32_t)(INT32_MAX / 4) ||
		height > (uint32_t)(INT32_MAX / ((int32_t)width * 4))) {
		fprintf(stderr, "klest-panel: invalid output dimensions\n");
		return false;
	}
	size_t pixel_count = (size_t)width * (size_t)height;
	uint32_t *pixels = malloc(pixel_count * sizeof(*pixels));
	if (pixels == NULL) {
		fprintf(stderr, "klest-panel: cannot allocate drawing buffer\n");
		return false;
	}
	struct klest_canvas canvas = {
		.pixels = pixels, .width = width, .height = height};
	for (size_t i = 0; i < pixel_count; i++) {
		pixels[i] = 0xff1a1c29U;
	}

	char clock_text[6];
	if (!get_clock_text(clock_text, &panel->drawn_minute)) {
		free(pixels);
		return false;
	}
	const uint32_t text_color = 0xffe8eaf5U;
	uint32_t text_y = (height - 7U * KLEST_GLYPH_SCALE) / 2U;
	draw_text(&canvas, "KLEST", 16U, text_y, KLEST_GLYPH_SCALE, text_color);
	draw_workspaces(panel, &canvas, text_y);
	uint32_t clock_x = panel_clock_x(width);
	draw_text(&canvas, clock_text, clock_x, text_y, KLEST_GLYPH_SCALE, text_color);
	draw_windows(panel, &canvas, clock_x);

	fill_rect(&canvas, 0U, height - 2U, width, 2U, 0xff5c8fe8U);

	struct wl_buffer *buffer = klest_shm_buffer_create_from_data(
		panel->layer.shm, width, height, (int32_t)(width * 4U), pixels);
	free(pixels);
	if (buffer == NULL) {
		return false;
	}
	if (!klest_layer_client_present(&panel->layer, buffer, width, height)) {
		wl_buffer_destroy(buffer);
		return false;
	}

	struct wl_buffer *old_buffer = panel->buffer;
	panel->buffer = buffer;
	panel->mapped_width = width;
	panel->mapped_height = height;
	panel->needs_redraw = false;
	if (old_buffer != NULL) {
		wl_buffer_destroy(old_buffer);
	}
	return true;
}

static void workspace_handle_state(void *data,
	struct klest_workspace_manager_v1 *manager, // NOSONAR: Wayland callback ABI.
	uint32_t count, uint32_t active)
{
	struct panel *panel = data;
	(void)manager;
	panel->workspace_count = count;
	panel->active_workspace = active;
	panel->needs_redraw = true;
}

static void workspace_handle_window(void *data,
	struct klest_workspace_manager_v1 *manager, // NOSONAR: Wayland callback ABI.
	uint32_t id, uint32_t workspace, const char *title, uint32_t active,
	uint32_t minimized, uint32_t maximized)
{
	struct panel *panel = data;
	(void)manager;
	struct panel_window *window = window_by_id(panel, id);
	if (window == NULL) {
		window = calloc(1, sizeof(*window));
		if (window == NULL) {
			return;
		}
		window->id = id;
		window->next = panel->windows;
		panel->windows = window;
	}
	window->workspace = workspace;
	snprintf(window->title, sizeof(window->title), "%s", title);
	window->active = active != 0;
	window->minimized = minimized != 0;
	window->maximized = maximized != 0;
	panel->needs_redraw = true;
}

static void workspace_handle_window_closed(void *data,
	struct klest_workspace_manager_v1 *manager, // NOSONAR: Wayland callback ABI.
	uint32_t id)
{
	struct panel *panel = data;
	(void)manager;
	struct panel_window **link = &panel->windows;
	while (*link != NULL) {
		if ((*link)->id == id) {
			struct panel_window *dead = *link;
			*link = dead->next;
			free(dead);
			panel->needs_redraw = true;
			return;
		}
		link = &(*link)->next;
	}
}

static const struct klest_workspace_manager_v1_listener workspace_listener = {
	.state = workspace_handle_state,
	.window = workspace_handle_window,
	.window_closed = workspace_handle_window_closed,
};

/* Maps a pointer x coordinate to a workspace index, or -1 if none. */
static int workspace_at(const struct panel *panel, double x)
{
	if (x < (double)KLEST_WS_ORIGIN_X) {
		return -1;
	}
	uint32_t index = (uint32_t)((x - (double)KLEST_WS_ORIGIN_X) /
		(double)KLEST_WS_BOX_WIDTH);
	return index < visible_workspaces(panel) ? (int)index : -1;
}

static void pointer_handle_enter(void *data,
	struct wl_pointer *pointer, // NOSONAR: Wayland callback ABI.
	uint32_t serial,
	struct wl_surface *surface, // NOSONAR: Wayland callback ABI.
	wl_fixed_t sx, wl_fixed_t sy)
{
	struct panel *panel = data;
	(void)pointer;
	(void)serial;
	(void)surface;
	(void)sy;
	panel->pointer_x = wl_fixed_to_double(sx);
}

static void pointer_handle_leave(void *data,
	struct wl_pointer *pointer, // NOSONAR: Wayland callback ABI.
	uint32_t serial,
	struct wl_surface *surface) // NOSONAR: Wayland callback ABI.
{
	(void)data;
	(void)pointer;
	(void)serial;
	(void)surface;
}

static void pointer_handle_motion(void *data,
	struct wl_pointer *pointer, // NOSONAR: Wayland callback ABI.
	uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	struct panel *panel = data;
	(void)pointer;
	(void)time;
	(void)sy;
	panel->pointer_x = wl_fixed_to_double(sx);
}

static void pointer_handle_button(void *data,
	struct wl_pointer *pointer, // NOSONAR: Wayland callback ABI.
	uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	struct panel *panel = data;
	(void)pointer;
	(void)serial;
	(void)time;
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED ||
			panel->workspace_manager == NULL) {
		return;
	}
	int index = workspace_at(panel, panel->pointer_x);
	if (index >= 0) {
		klest_workspace_manager_v1_activate(panel->workspace_manager,
			(uint32_t)index);
		return;
	}
	if (wl_proxy_get_version((struct wl_proxy *)panel->workspace_manager) < 2) {
		return;
	}
	struct panel_window *window = window_at(panel, panel->pointer_x);
	if (window == NULL) {
		return;
	}
	if (window->active && !window->minimized) {
		klest_workspace_manager_v1_minimize_toplevel(
			panel->workspace_manager, window->id);
	} else {
		klest_workspace_manager_v1_activate_toplevel(
			panel->workspace_manager, window->id);
	}
}

static void pointer_handle_axis(void *data,
	struct wl_pointer *pointer, // NOSONAR: Wayland callback ABI.
	uint32_t time, uint32_t axis, wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
	uint32_t capabilities)
{
	struct panel *panel = data;
	bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
	if (has_pointer && panel->pointer == NULL) {
		panel->pointer = wl_seat_get_pointer(seat);
		if (panel->pointer != NULL) {
			wl_pointer_add_listener(panel->pointer, &pointer_listener, panel);
		}
	} else if (!has_pointer && panel->pointer != NULL) {
		wl_pointer_destroy(panel->pointer);
		panel->pointer = NULL;
	}
}

static void seat_handle_name(void *data,
	struct wl_seat *seat, // NOSONAR: Wayland callback ABI.
	const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version)
{
	struct panel *panel = data;
	if (strcmp(interface, klest_workspace_manager_v1_interface.name) == 0) {
		uint32_t bind_version = version < 2U ? version : 2U;
		panel->workspace_manager = wl_registry_bind(registry, name,
			&klest_workspace_manager_v1_interface, bind_version);
		if (panel->workspace_manager != NULL) {
			klest_workspace_manager_v1_add_listener(
				panel->workspace_manager, &workspace_listener, panel);
		}
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		panel->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
		if (panel->seat != NULL) {
			wl_seat_add_listener(panel->seat, &seat_listener, panel);
		}
	}
}

static void registry_global_remove(void *data,
	struct wl_registry *registry, // NOSONAR: Wayland callback ABI.
	uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

/* Binds the workspace manager and seat. A missing global is not fatal. */
static bool panel_bind_controls(struct panel *panel)
{
	panel->registry = wl_display_get_registry(panel->layer.display);
	if (panel->registry == NULL || wl_registry_add_listener(panel->registry,
			&registry_listener, panel) != 0 ||
			wl_display_roundtrip(panel->layer.display) < 0) {
		fprintf(stderr, "klest-panel: registry setup failed\n");
		return false;
	}
	/* A second roundtrip delivers the initial workspace state and seat
	 * capabilities, which arrive after their globals are bound. */
	if (wl_display_roundtrip(panel->layer.display) < 0) {
		return false;
	}
	return true;
}

static int milliseconds_until_minute(time_t now)
{
	long seconds = 60 - (long)(now % 60);
	return (int)(seconds * 1000);
}

static bool run_panel(struct panel *panel, bool once)
{
	if (!draw_panel(panel)) {
		return false;
	}
	if (once) {
		return klest_layer_client_unmap(&panel->layer);
	}

	int display_fd = wl_display_get_fd(panel->layer.display);
	while (!panel->layer.closed) {
		struct pollfd descriptor = {
			.fd = display_fd,
			.events = POLLIN,
		};
		time_t now = time(NULL);
		if (now == (time_t)-1) {
			fprintf(stderr, "klest-panel: cannot read system clock\n");
			return false;
		}
		int result;
		do {
			result = poll(&descriptor, 1, milliseconds_until_minute(now));
		} while (result < 0 && errno == EINTR);
		if (result < 0) {
			perror("klest-panel: poll");
			return false;
		}
		if (result > 0 && wl_display_dispatch(panel->layer.display) < 0) {
			return true;
		}

		time_t current_time = time(NULL);
		if (current_time == (time_t)-1) {
			fprintf(stderr, "klest-panel: cannot read system clock\n");
			return false;
		}
		if (panel->needs_redraw || current_time / 60 != panel->drawn_minute ||
				panel->layer.width != panel->mapped_width ||
				panel->layer.height != panel->mapped_height) {
			if (!draw_panel(panel)) {
				return false;
			}
		}
	}
	return true;
}

static void panel_finish(struct panel *panel)
{
	struct panel_window *window = panel->windows;
	while (window != NULL) {
		struct panel_window *next = window->next;
		free(window);
		window = next;
	}
	if (panel->pointer != NULL) {
		wl_pointer_destroy(panel->pointer);
	}
	if (panel->seat != NULL) {
		wl_seat_destroy(panel->seat);
	}
	if (panel->workspace_manager != NULL) {
		klest_workspace_manager_v1_destroy(panel->workspace_manager);
	}
	if (panel->registry != NULL) {
		wl_registry_destroy(panel->registry);
	}
	if (panel->buffer != NULL) {
		wl_buffer_destroy(panel->buffer);
	}
	klest_layer_client_finish(&panel->layer);
}

static int usage(const char *program)
{
	fprintf(stderr, "Usage: %s [--once]\n", program);
	return 2;
}

int main(int argc, char *argv[])
{
	bool once = false;
	if (argc == 2 && strcmp(argv[1], "--once") == 0) {
		once = true;
	} else if (argc != 1) {
		return usage(argv[0]);
	}

	struct panel panel = {0};
	bool ok = klest_layer_client_connect(&panel.layer) &&
		panel_bind_controls(&panel) &&
		klest_layer_client_create_surface(&panel.layer,
			ZWLR_LAYER_SHELL_V1_LAYER_TOP, "klest-panel",
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
			0, KLEST_PANEL_HEIGHT, KLEST_PANEL_HEIGHT, true, false) &&
		klest_layer_client_wait_configure(&panel.layer);
	if (ok) {
		ok = run_panel(&panel, once);
	}
	panel_finish(&panel);
	return ok ? 0 : 1;
}
