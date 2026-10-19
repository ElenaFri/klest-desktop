/* Shared Wayland shared-memory buffer helper. */
#ifndef KLEST_WAYLAND_SHM_H
#define KLEST_WAYLAND_SHM_H

#include <stdint.h>

#include <wayland-client.h>

/* Creates an opaque ARGB8888 buffer filled with `pixel`.
 * Returns NULL if dimensions, runtime storage, or Wayland allocation fail. */
struct wl_buffer *klest_shm_buffer_create(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t pixel);

#endif /* KLEST_WAYLAND_SHM_H */
