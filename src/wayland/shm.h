/* Shared Wayland shared-memory buffer helper. */
#ifndef KLEST_WAYLAND_SHM_H
#define KLEST_WAYLAND_SHM_H

#include <stdint.h>

#include <wayland-client.h>

/* Creates an opaque ARGB8888 buffer filled with `pixel`. */
struct wl_buffer *klest_shm_buffer_create(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t pixel);

/* Copies ARGB8888 image data into an opaque Wayland SHM buffer. */
struct wl_buffer *klest_shm_buffer_create_from_data(struct wl_shm *shm,
	uint32_t width, uint32_t height, int32_t source_stride,
	const void *pixels);

#endif /* KLEST_WAYLAND_SHM_H */
