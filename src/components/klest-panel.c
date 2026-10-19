/*
 * Klest — minimal Wayland panel (ADR 0009).
 *
 * A top layer-shell bar with a small bitmap label and local clock. Workspace
 * controls are deferred until the compositor has a workspace model to expose.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>

#include "wayland/layer_client.h"
#include "wayland/shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define KLEST_PANEL_HEIGHT 36U
#define KLEST_GLYPH_SCALE 2U

struct panel {
	struct klest_layer_client layer;
	struct wl_buffer *buffer;
	uint32_t mapped_width;
	uint32_t mapped_height;
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

static void draw_glyph(uint32_t *pixels, uint32_t width, uint32_t height,
	char character, uint32_t x, uint32_t y, uint32_t color)
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
			for (uint32_t dy = 0; dy < KLEST_GLYPH_SCALE; dy++) {
				for (uint32_t dx = 0; dx < KLEST_GLYPH_SCALE; dx++) {
					uint32_t px = x + column * KLEST_GLYPH_SCALE + dx;
					uint32_t py = y + row * KLEST_GLYPH_SCALE + dy;
					if (px < width && py < height) {
						pixels[(size_t)py * width + px] = color;
					}
				}
			}
		}
	}
}

static void draw_text(uint32_t *pixels, uint32_t width, uint32_t height,
	const char *text, uint32_t x, uint32_t y, uint32_t color)
{
	for (const char *p = text; *p != '\0'; p++) {
		draw_glyph(pixels, width, height, *p, x, y, color);
		x += 6U * KLEST_GLYPH_SCALE;
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
	draw_text(pixels, width, height, "KLEST", 16U, text_y, text_color);
	uint32_t clock_width = 5U * 6U * KLEST_GLYPH_SCALE;
	uint32_t clock_x = width > clock_width + 16U ? width - clock_width - 16U : 0U;
	draw_text(pixels, width, height, clock_text, clock_x, text_y, text_color);

	for (uint32_t y = height - 2U; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			pixels[(size_t)y * width + x] = 0xff5c8fe8U;
		}
	}

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
	if (old_buffer != NULL) {
		wl_buffer_destroy(old_buffer);
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
		if (current_time / 60 != panel->drawn_minute ||
				panel->layer.width != panel->mapped_width ||
				panel->layer.height != panel->mapped_height) {
			if (!draw_panel(panel)) {
				return false;
			}
		}
	}
	return true;
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
		klest_layer_client_create_surface(&panel.layer,
			ZWLR_LAYER_SHELL_V1_LAYER_TOP, "klest-panel",
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
			0, KLEST_PANEL_HEIGHT, KLEST_PANEL_HEIGHT, false) &&
		klest_layer_client_wait_configure(&panel.layer);
	if (ok) {
		ok = run_panel(&panel, once);
	}
	if (panel.buffer != NULL) {
		wl_buffer_destroy(panel.buffer);
	}
	klest_layer_client_finish(&panel.layer);
	return ok ? 0 : 1;
}
