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
#include "wayland/layer_client.h"
#include "wayland/shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define KLEST_PANEL_HEIGHT 36U
#define KLEST_GLYPH_SCALE 2U
#define KLEST_WS_ORIGIN_X 96U
#define KLEST_WS_BOX_WIDTH 26U
#define KLEST_PANEL_MAX_WORKSPACES 9U

/* A simple ARGB8888 drawing target. */
struct canvas {
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
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
	double pointer_x;
	bool needs_redraw;
	time_t drawn_minute;
};

static const char GLYPH_KEYS[] = "KLEST0123456789:";
static const uint8_t GLYPH_ROWS[][7] = {
	{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
	{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, /* L */
	{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, /* E */
	{0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, /* S */
	{0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
	{0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, /* 0 */
	{0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, /* 1 */
	{0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, /* 2 */
	{0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}, /* 3 */
	{0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, /* 4 */
	{0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}, /* 5 */
	{0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}, /* 6 */
	{0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
	{0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, /* 8 */
	{0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}, /* 9 */
	{0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}, /* : */
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

static void fill_rect(const struct canvas *canvas, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h, uint32_t color)
{
	for (uint32_t py = y; py < y + h && py < canvas->height; py++) {
		for (uint32_t px = x; px < x + w && px < canvas->width; px++) {
			canvas->pixels[(size_t)py * canvas->width + px] = color;
		}
	}
}

static void draw_glyph(const struct canvas *canvas, char character,
	uint32_t x, uint32_t y, uint32_t color)
{
	const char *key = strchr(GLYPH_KEYS, character);
	if (key == NULL) {
		return;
	}
	size_t glyph_index = (size_t)(key - GLYPH_KEYS);
	for (uint32_t row = 0; row < 7; row++) {
		for (uint32_t column = 0; column < 5; column++) {
			if ((GLYPH_ROWS[glyph_index][row] & (1U << (4U - column))) == 0) {
				continue;
			}
			fill_rect(canvas, x + column * KLEST_GLYPH_SCALE,
				y + row * KLEST_GLYPH_SCALE,
				KLEST_GLYPH_SCALE, KLEST_GLYPH_SCALE, color);
		}
	}
}

static void draw_text(const struct canvas *canvas, const char *text,
	uint32_t x, uint32_t y, uint32_t color)
{
	for (const char *p = text; *p != '\0'; p++) {
		draw_glyph(canvas, *p, x, y, color);
		x += 6U * KLEST_GLYPH_SCALE;
	}
}

/* Returns the number of workspace indicators that fit, capped for safety. */
static uint32_t visible_workspaces(const struct panel *panel)
{
	uint32_t count = panel->workspace_count;
	return count < KLEST_PANEL_MAX_WORKSPACES ?
		count : KLEST_PANEL_MAX_WORKSPACES;
}

static void draw_workspaces(const struct panel *panel,
	const struct canvas *canvas, uint32_t text_y)
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
		draw_text(canvas, label, box_x + 8U, text_y, text_color);
	}
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
	struct canvas canvas = {.pixels = pixels, .width = width, .height = height};
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
	draw_text(&canvas, "KLEST", 16U, text_y, text_color);
	draw_workspaces(panel, &canvas, text_y);
	uint32_t clock_width = 5U * 6U * KLEST_GLYPH_SCALE;
	uint32_t clock_x = width > clock_width + 16U ? width - clock_width - 16U : 0U;
	draw_text(&canvas, clock_text, clock_x, text_y, text_color);

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

static const struct klest_workspace_manager_v1_listener workspace_listener = {
	.state = workspace_handle_state,
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
	(void)version;
	if (strcmp(interface, klest_workspace_manager_v1_interface.name) == 0) {
		panel->workspace_manager = wl_registry_bind(registry, name,
			&klest_workspace_manager_v1_interface, 1);
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
			0, KLEST_PANEL_HEIGHT, KLEST_PANEL_HEIGHT, true) &&
		klest_layer_client_wait_configure(&panel.layer);
	if (ok) {
		ok = run_panel(&panel, once);
	}
	panel_finish(&panel);
	return ok ? 0 : 1;
}
