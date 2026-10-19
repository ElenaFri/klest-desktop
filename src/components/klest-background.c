/*
 * Klest — solid-colour Wayland background client (ADR 0009).
 *
 * Reads the shared appearance.background_color setting and fills its output.
 * Image loading can be added later without changing the compositor interface.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client.h>

#include "config/config.h"
#include "wayland/layer_client.h"
#include "wayland/shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct background {
	struct klest_layer_client layer;
	struct wl_buffer *buffer;
	float color[4];
	uint32_t mapped_width;
	uint32_t mapped_height;
};

static uint8_t color_byte(float value)
{
	if (value < 0.0f) {
		value = 0.0f;
	} else if (value > 1.0f) {
		value = 1.0f;
	}
	return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t premultiplied_argb(const float color[4])
{
	uint8_t alpha = color_byte(color[3]);
	uint8_t red = (uint8_t)((color_byte(color[0]) * alpha + 127U) / 255U);
	uint8_t green = (uint8_t)((color_byte(color[1]) * alpha + 127U) / 255U);
	uint8_t blue = (uint8_t)((color_byte(color[2]) * alpha + 127U) / 255U);
	return ((uint32_t)alpha << 24) | ((uint32_t)red << 16) |
		((uint32_t)green << 8) | (uint32_t)blue;
}

static bool update_background_buffer(struct background *background)
{
	struct klest_layer_client *layer = &background->layer;
	uint32_t pixel = premultiplied_argb(background->color);
	struct wl_buffer *buffer = klest_shm_buffer_create(layer->shm,
		layer->width, layer->height, pixel);
	if (buffer == NULL) {
		return false;
	}
	if (!klest_layer_client_present(layer, buffer, layer->width, layer->height)) {
		wl_buffer_destroy(buffer);
		return false;
	}
	struct wl_buffer *old_buffer = background->buffer;
	background->buffer = buffer;
	background->mapped_width = layer->width;
	background->mapped_height = layer->height;
	if (old_buffer != NULL) {
		wl_buffer_destroy(old_buffer);
	}
	return true;
}

static bool run_background(struct background *background, bool once)
{
	if (!update_background_buffer(background)) {
		return false;
	}
	if (once) {
		return klest_layer_client_unmap(&background->layer);
	}

	while (!background->layer.closed &&
			wl_display_dispatch(background->layer.display) >= 0) {
		if ((background->layer.width != background->mapped_width ||
				background->layer.height != background->mapped_height) &&
				!update_background_buffer(background)) {
			return false;
		}
	}
	return true;
}

static void finish_background(struct background *background)
{
	if (background->buffer != NULL) {
		wl_buffer_destroy(background->buffer);
	}
	klest_layer_client_finish(&background->layer);
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

	struct klest_config config;
	(void)klest_config_load(&config, NULL);
	struct background background = {0};
	memcpy(background.color, config.background_color,
		sizeof(background.color));

	struct klest_layer_client *layer = &background.layer;
	bool ok = klest_layer_client_connect(layer) &&
		klest_layer_client_create_surface(layer,
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
			"klest-background",
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
			0, 0, -1, false) &&
		klest_layer_client_wait_configure(layer);
	if (ok) {
		ok = run_background(&background, once);
	}

	finish_background(&background);
	return ok ? 0 : 1;
}
