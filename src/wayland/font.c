/* Klest — ARGB8888 drawing helpers and font8x8 bitmap text (ADR 0009). */

#include "wayland/font.h"

#include <string.h>

#include "font8x8.h"

void klest_canvas_fill(const struct klest_canvas *canvas, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h, uint32_t color)
{
	uint64_t max_x = (uint64_t)x + w;
	uint64_t max_y = (uint64_t)y + h;
	if (max_x > canvas->width) {
		max_x = canvas->width;
	}
	if (max_y > canvas->height) {
		max_y = canvas->height;
	}
	for (uint32_t py = y; py < max_y; py++) {
		for (uint32_t px = x; px < max_x; px++) {
			canvas->pixels[(size_t)py * canvas->width + px] = color;
		}
	}
}

void klest_canvas_clear(const struct klest_canvas *canvas, uint32_t color)
{
	size_t count = (size_t)canvas->width * canvas->height;
	for (size_t i = 0; i < count; i++) {
		canvas->pixels[i] = color;
	}
}

static void draw_glyph(const struct klest_canvas *canvas, unsigned char code,
	uint32_t x, uint32_t y, uint32_t scale, uint32_t color)
{
	if (code >= 128) {
		return;
	}
	for (uint32_t row = 0; row < KLEST_FONT_GLYPH; row++) {
		unsigned char bits = font8x8_basic[code][row];
		for (uint32_t col = 0; col < KLEST_FONT_GLYPH; col++) {
			if ((bits & (1U << col)) == 0) {
				continue;
			}
			klest_canvas_fill(canvas, x + col * scale, y + row * scale,
				scale, scale, color);
		}
	}
}

void klest_font_draw(const struct klest_canvas *canvas, const char *text,
	uint32_t x, uint32_t y, uint32_t scale, uint32_t color)
{
	uint32_t advance = KLEST_FONT_GLYPH * scale;
	for (const char *p = text; *p != '\0'; p++) {
		draw_glyph(canvas, (unsigned char)*p, x, y, scale, color);
		x += advance;
	}
}

uint32_t klest_font_width(const char *text, uint32_t scale)
{
	return (uint32_t)strlen(text) * KLEST_FONT_GLYPH * scale;
}
