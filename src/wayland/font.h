/*
 * Klest — simple ARGB8888 drawing helpers and bitmap text (ADR 0009).
 *
 * Shared by components that render their own surfaces (e.g. the launcher).
 * Text uses the vendored font8x8 basic-Latin font; non-printable characters
 * are skipped.
 */

#ifndef KLEST_WAYLAND_FONT_H
#define KLEST_WAYLAND_FONT_H

#include <stdint.h>

#define KLEST_FONT_GLYPH 8U /* glyph width and height, in unscaled pixels */

/* A software drawing target: tightly packed ARGB8888 pixels, row-major. */
struct klest_canvas {
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

/* Fills an axis-aligned rectangle, clipped to the canvas. */
void klest_canvas_fill(const struct klest_canvas *canvas, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h, uint32_t color);

/* Fills the whole canvas with a single colour. */
void klest_canvas_clear(const struct klest_canvas *canvas, uint32_t color);

/*
 * Draws `text` at (x, y) in `color`, each glyph scaled by `scale` (>= 1).
 * Characters advance by KLEST_FONT_GLYPH * scale pixels.
 */
void klest_font_draw(const struct klest_canvas *canvas, const char *text,
	uint32_t x, uint32_t y, uint32_t scale, uint32_t color);

/* Returns the pixel width `text` would occupy at the given scale. */
uint32_t klest_font_width(const char *text, uint32_t scale);

#endif /* KLEST_WAYLAND_FONT_H */
