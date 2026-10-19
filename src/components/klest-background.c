/*
 * Klest — solid-colour Wayland background client (ADR 0009).
 *
 * This first standalone component validates the layer-shell boundary. It reads
 * the shared appearance.background_color setting and fills the output; image
 * loading can be added later without changing the compositor interface.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "config/config.h"
#include "background-layer-shell-client-protocol.h"

struct background {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	float color[4];
	uint32_t width;
	uint32_t height;
	uint32_t mapped_width;
	uint32_t mapped_height;
	bool configured;
	bool closed;
};

static uint32_t min_version(uint32_t advertised, uint32_t supported)
{
	return advertised < supported ? advertised : supported;
}

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version)
{
	struct background *background = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		background->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, min_version(version, 4));
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		background->shm = wl_registry_bind(registry, name,
			&wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		background->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, min_version(version, 4));
	}
}

static void registry_global_remove(void *data,
	struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void layer_surface_configure(void *data,
	struct zwlr_layer_surface_v1 *layer_surface,
	uint32_t serial, uint32_t width, uint32_t height)
{
	struct background *background = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	background->width = width;
	background->height = height;
	background->configured = true;
}

static void layer_surface_closed(void *data,
	struct zwlr_layer_surface_v1 *layer_surface)
{
	struct background *background = data;
	(void)layer_surface;
	background->closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static bool connect_wayland(struct background *background)
{
	background->display = wl_display_connect(NULL);
	if (background->display == NULL) {
		fprintf(stderr, "klest-background: cannot connect to Wayland display\n");
		return false;
	}
	background->registry = wl_display_get_registry(background->display);
	if (background->registry == NULL ||
			wl_registry_add_listener(background->registry,
				&registry_listener, background) != 0 ||
			wl_display_roundtrip(background->display) < 0) {
		fprintf(stderr, "klest-background: Wayland registry setup failed\n");
		return false;
	}
	if (background->compositor == NULL || background->shm == NULL ||
			background->layer_shell == NULL) {
		fprintf(stderr,
			"klest-background: compositor lacks a required Wayland protocol\n");
		return false;
	}
	return true;
}

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

static struct wl_buffer *create_color_buffer(struct background *background,
	uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0 || width > (uint32_t)(INT32_MAX / 4) ||
		height > (uint32_t)(INT32_MAX / ((int32_t)width * 4))) {
		fprintf(stderr, "klest-background: invalid output dimensions\n");
		return NULL;
	}
	int32_t stride = (int32_t)width * 4;
	int32_t size = stride * (int32_t)height;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL || runtime_dir[0] == '\0') {
		fprintf(stderr, "klest-background: XDG_RUNTIME_DIR is unset\n");
		return NULL;
	}

	char path[4096];
	int path_len = snprintf(path, sizeof(path), "%s/klest-background-XXXXXX",
		runtime_dir);
	if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
		fprintf(stderr, "klest-background: SHM path is too long\n");
		return NULL;
	}
	int fd = mkstemp(path);
	if (fd < 0) {
		perror("klest-background: mkstemp");
		return NULL;
	}
	(void)unlink(path);
	if (ftruncate(fd, size) != 0) {
		perror("klest-background: ftruncate");
		close(fd);
		return NULL;
	}

	void *memory = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (memory == MAP_FAILED) {
		perror("klest-background: mmap");
		close(fd);
		return NULL;
	}
	uint32_t pixel = premultiplied_argb(background->color);
	uint32_t *pixels = memory;
	size_t count = (size_t)width * (size_t)height;
	for (size_t i = 0; i < count; i++) {
		pixels[i] = pixel;
	}
	(void)munmap(memory, (size_t)size);

	struct wl_shm_pool *pool = wl_shm_create_pool(background->shm, fd, size);
	close(fd);
	if (pool == NULL) {
		return NULL;
	}
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
		(int32_t)width, (int32_t)height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	return buffer;
}

static bool create_background_surface(struct background *background)
{
	background->surface = wl_compositor_create_surface(background->compositor);
	if (background->surface == NULL) {
		fprintf(stderr, "klest-background: cannot create surface\n");
		return false;
	}
	struct wl_region *empty_input =
		wl_compositor_create_region(background->compositor);
	if (empty_input == NULL) {
		fprintf(stderr, "klest-background: cannot create input region\n");
		return false;
	}
	wl_surface_set_input_region(background->surface, empty_input);
	wl_region_destroy(empty_input);

	background->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		background->layer_shell, background->surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "klest-background");
	if (background->layer_surface == NULL ||
			zwlr_layer_surface_v1_add_listener(background->layer_surface,
				&layer_surface_listener, background) != 0) {
		fprintf(stderr, "klest-background: cannot create layer surface\n");
		return false;
	}

	zwlr_layer_surface_v1_set_anchor(background->layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(background->layer_surface, -1);
	zwlr_layer_surface_v1_set_size(background->layer_surface, 0, 0);
	wl_surface_commit(background->surface);

	for (int attempt = 0; attempt < 5 && !background->configured; attempt++) {
		if (wl_display_roundtrip(background->display) < 0) {
			fprintf(stderr, "klest-background: configure roundtrip failed\n");
			return false;
		}
	}
	if (!background->configured) {
		fprintf(stderr, "klest-background: compositor sent no configure\n");
		return false;
	}
	return true;
}

static bool update_background_buffer(struct background *background)
{
	struct wl_buffer *buffer = create_color_buffer(background,
		background->width, background->height);
	if (buffer == NULL) {
		return false;
	}
	struct wl_buffer *old_buffer = background->buffer;
	wl_surface_attach(background->surface, buffer, 0, 0);
	wl_surface_damage(background->surface, 0, 0,
		(int32_t)background->width, (int32_t)background->height);
	wl_surface_commit(background->surface);
	background->buffer = buffer;
	background->mapped_width = background->width;
	background->mapped_height = background->height;
	if (wl_display_roundtrip(background->display) < 0) {
		fprintf(stderr, "klest-background: failed to map surface\n");
		return false;
	}
	if (old_buffer != NULL) {
		wl_buffer_destroy(old_buffer);
	}
	return true;
}

static bool map_background(struct background *background, bool once)
{
	if (!update_background_buffer(background)) {
		return false;
	}
	if (!once) {
		while (!background->closed &&
				wl_display_dispatch(background->display) >= 0) {
			if ((background->width != background->mapped_width ||
					background->height != background->mapped_height) &&
					!update_background_buffer(background)) {
				return false;
			}
		}
	}
	return true;
}

static void destroy_background(struct background *background)
{
	if (background->layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(background->layer_surface);
	}
	if (background->surface != NULL) {
		wl_surface_destroy(background->surface);
	}
	if (background->buffer != NULL) {
		wl_buffer_destroy(background->buffer);
	}
	if (background->layer_shell != NULL) {
		zwlr_layer_shell_v1_destroy(background->layer_shell);
	}
	if (background->shm != NULL) {
		wl_shm_destroy(background->shm);
	}
	if (background->compositor != NULL) {
		wl_compositor_destroy(background->compositor);
	}
	if (background->registry != NULL) {
		wl_registry_destroy(background->registry);
	}
	if (background->display != NULL) {
		(void)wl_display_roundtrip(background->display);
		wl_display_disconnect(background->display);
	}
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

	bool ok = connect_wayland(&background) &&
		create_background_surface(&background);
	if (ok) {
		ok = map_background(&background, once);
	}

	destroy_background(&background);
	return ok ? 0 : 1;
}
