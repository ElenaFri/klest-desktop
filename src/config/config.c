/*
 * Klest — configuration loading (ADR 0007).
 *
 * Parses a small, well-defined subset of TOML into `struct klest_config`,
 * layering a user file on top of built-in defaults. Parsing is delegated to the
 * vendored tomlc17 library; this module owns the schema and the defaults.
 */

#define _POSIX_C_SOURCE 200809L

#include "config/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tomlc17.h"

/* Built-in defaults. These are the single source of truth for "out of the box"
 * behaviour and must stay in sync with the documented example config. */
static const float KLEST_DEFAULT_BACKGROUND[4] = {0.10f, 0.11f, 0.16f, 1.0f};
#define KLEST_DEFAULT_REPEAT_RATE 25
#define KLEST_DEFAULT_REPEAT_DELAY 600

void klest_config_set_defaults(struct klest_config *cfg)
{
	memcpy(cfg->background_color, KLEST_DEFAULT_BACKGROUND,
		sizeof(cfg->background_color));
	cfg->keyboard_repeat_rate = KLEST_DEFAULT_REPEAT_RATE;
	cfg->keyboard_repeat_delay = KLEST_DEFAULT_REPEAT_DELAY;
	cfg->terminal[0] = '\0';
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	c = (char)tolower((unsigned char)c);
	if (c >= 'a' && c <= 'f') {
		return 10 + (c - 'a');
	}
	return -1;
}

static bool hex_byte(const char *s, float *out)
{
	int hi = hex_nibble(s[0]);
	int lo = hex_nibble(s[1]);
	if (hi < 0 || lo < 0) {
		return false;
	}
	*out = (float)((hi << 4) | lo) / 255.0f;
	return true;
}

bool klest_config_parse_color(const char *hex, float out[4])
{
	if (hex == NULL || hex[0] != '#') {
		return false;
	}
	const char *digits = hex + 1;
	size_t len = strlen(digits);
	if (len != 6 && len != 8) {
		return false;
	}

	float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	size_t components = len / 2;
	for (size_t i = 0; i < components; i++) {
		if (!hex_byte(digits + i * 2, &rgba[i])) {
			return false;
		}
	}

	memcpy(out, rgba, sizeof(rgba));
	return true;
}

bool klest_config_default_path(char *buf, size_t cap)
{
	const char *explicit_path = getenv("KLEST_CONFIG");
	if (explicit_path != NULL && explicit_path[0] != '\0') {
		int n = snprintf(buf, cap, "%s", explicit_path);
		return n > 0 && (size_t)n < cap;
	}

	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		int n = snprintf(buf, cap, "%s/klest/config.toml", xdg);
		return n > 0 && (size_t)n < cap;
	}

	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		int n = snprintf(buf, cap, "%s/.config/klest/config.toml", home);
		return n > 0 && (size_t)n < cap;
	}

	return false;
}

/* Reads an [appearance] colour string, if present and well-formed. */
static void apply_appearance(struct klest_config *cfg, toml_datum_t root)
{
	toml_datum_t color = toml_seek(root, "appearance.background_color");
	if (color.type == TOML_UNKNOWN) {
		return;
	}
	if (color.type != TOML_STRING) {
		fprintf(stderr,
			"klest: config: appearance.background_color must be a string\n");
		return;
	}
	float rgba[4];
	if (!klest_config_parse_color(color.u.s, rgba)) {
		fprintf(stderr, "klest: config: invalid colour '%s' "
			"(expected \"#rrggbb\" or \"#rrggbbaa\")\n", color.u.s);
		return;
	}
	memcpy(cfg->background_color, rgba, sizeof(cfg->background_color));
}

/* Reads a positive-int [keyboard] key, keeping the default on any problem. */
static void apply_positive_int(toml_datum_t root, const char *key,
	int32_t *out, int32_t max)
{
	toml_datum_t datum = toml_seek(root, key);
	if (datum.type == TOML_UNKNOWN) {
		return;
	}
	if (datum.type != TOML_INT64) {
		fprintf(stderr, "klest: config: %s must be an integer\n", key);
		return;
	}
	if (datum.u.int64 <= 0 || datum.u.int64 > max) {
		fprintf(stderr, "klest: config: %s out of range (1..%d)\n", key, max);
		return;
	}
	*out = (int32_t)datum.u.int64;
}

/* Reads a string key into a fixed-size buffer, keeping the default on problems. */
static void apply_string(toml_datum_t root, const char *key, char *out,
	size_t cap)
{
	toml_datum_t datum = toml_seek(root, key);
	if (datum.type == TOML_UNKNOWN) {
		return;
	}
	if (datum.type != TOML_STRING) {
		fprintf(stderr, "klest: config: %s must be a string\n", key);
		return;
	}
	int n = snprintf(out, cap, "%s", datum.u.s);
	if (n < 0 || (size_t)n >= cap) {
		fprintf(stderr, "klest: config: %s is too long (max %zu bytes)\n",
			key, cap - 1);
		out[0] = '\0';
	}
}

bool klest_config_load(struct klest_config *cfg, const char *path)
{
	klest_config_set_defaults(cfg);

	char resolved[4096];
	if (path == NULL) {
		if (!klest_config_default_path(resolved, sizeof(resolved))) {
			/* No location to read from: defaults are a valid outcome. */
			return true;
		}
		path = resolved;
	}

	/* A missing file is the expected "no config yet" case, not an error. */
	if (access(path, F_OK) != 0) {
		return true;
	}

	toml_result_t result = toml_parse_file_ex(path);
	if (!result.ok) {
		fprintf(stderr, "klest: config: %s: %s\n", path, result.errmsg);
		toml_free(result);
		return false;
	}

	apply_appearance(cfg, result.toptab);
	apply_positive_int(result.toptab, "keyboard.repeat_rate",
		&cfg->keyboard_repeat_rate, 1000);
	apply_positive_int(result.toptab, "keyboard.repeat_delay",
		&cfg->keyboard_repeat_delay, 60000);
	apply_string(result.toptab, "keybindings.terminal",
		cfg->terminal, sizeof(cfg->terminal));

	toml_free(result);
	return true;
}
