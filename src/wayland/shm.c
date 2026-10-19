/* Shared Wayland SHM buffer allocation for Klest clients. */
#define _POSIX_C_SOURCE 200809L

#include "wayland/shm.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static bool valid_dimensions(uint32_t width, uint32_t height,
	int32_t *stride, int32_t *size)
{
	if (width == 0 || height == 0 || width > (uint32_t)(INT32_MAX / 4)) {
		return false;
	}
	*stride = (int32_t)width * 4;
	if (height > (uint32_t)(INT32_MAX / *stride)) {
		return false;
	}
	*size = *stride * (int32_t)height;
	return true;
}

static struct wl_buffer *create_buffer(struct wl_shm *shm,
	uint32_t width, uint32_t height, int32_t stride, int32_t size,
	const void *pixels, int32_t source_stride, uint32_t fill_pixel)
{
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL || runtime_dir[0] == '\0') {
		fprintf(stderr, "klest: XDG_RUNTIME_DIR is unset\n");
		return NULL;
	}
	char path[4096];
	int path_len = snprintf(path, sizeof(path), "%s/klest-shm-XXXXXX",
		runtime_dir);
	if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
		fprintf(stderr, "klest: shared-memory path is too long\n");
		return NULL;
	}

	int fd = mkstemp(path);
	if (fd < 0) {
		perror("klest: mkstemp");
		return NULL;
	}
	(void)unlink(path);
	if (ftruncate(fd, size) != 0) {
		perror("klest: ftruncate");
		close(fd);
		return NULL;
	}

	void *memory = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (memory == MAP_FAILED) {
		perror("klest: mmap");
		close(fd);
		return NULL;
	}
	if (pixels == NULL) {
		uint32_t *target = memory;
		size_t count = (size_t)width * (size_t)height;
		for (size_t i = 0; i < count; i++) {
			target[i] = fill_pixel;
		}
	} else {
		char *target = memory;
		const char *source = pixels;
		for (uint32_t row = 0; row < height; row++) {
			memcpy(target + (size_t)row * (size_t)stride,
				source + (size_t)row * (size_t)source_stride, (size_t)stride);
		}
	}
	if (munmap(memory, (size_t)size) != 0) {
		perror("klest: munmap");
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	close(fd);
	if (pool == NULL) {
		return NULL;
	}
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
		(int32_t)width, (int32_t)height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	return buffer;
}

static struct wl_buffer *create_solid_buffer(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t pixel)
{
	int32_t stride;
	int32_t size;
	if (shm == NULL || !valid_dimensions(width, height, &stride, &size)) {
		fprintf(stderr, "klest: invalid shared-memory buffer dimensions\n");
		return NULL;
	}
	return create_buffer(shm, width, height, stride, size, NULL, 0, pixel);
}

struct wl_buffer *klest_shm_buffer_create(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t pixel)
{
	return create_solid_buffer(shm, width, height, pixel);
}

struct wl_buffer *klest_shm_buffer_create_from_data(struct wl_shm *shm,
	uint32_t width, uint32_t height, int32_t source_stride,
	const void *pixels)
{
	int32_t stride;
	int32_t size;
	if (shm == NULL || pixels == NULL ||
			!valid_dimensions(width, height, &stride, &size) ||
			source_stride < stride ||
			(size_t)source_stride > SIZE_MAX / (size_t)height) {
		fprintf(stderr, "klest: invalid shared-memory image data\n");
		return NULL;
	}
	return create_buffer(shm, width, height, stride, size, pixels,
		source_stride, 0);
}
