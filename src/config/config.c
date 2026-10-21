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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
	snprintf(cfg->terminal, sizeof(cfg->terminal), "foot");
	cfg->launcher[0] = '\0';
	cfg->autostart_count = 0;
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

static int to_byte(float v)
{
	if (v < 0.0f) {
		v = 0.0f;
	} else if (v > 1.0f) {
		v = 1.0f;
	}
	return (int)(v * 255.0f + 0.5f);
}

bool klest_config_format_color(const float rgba[4], char *buf, size_t cap)
{
	int r = to_byte(rgba[0]);
	int g = to_byte(rgba[1]);
	int b = to_byte(rgba[2]);
	int a = to_byte(rgba[3]);

	int n;
	if (a >= 255) {
		n = snprintf(buf, cap, "#%02x%02x%02x", r, g, b);
	} else {
		n = snprintf(buf, cap, "#%02x%02x%02x%02x", r, g, b, a);
	}
	return n > 0 && (size_t)n < cap;
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

/* Reads [startup].autostart, an array of command strings, into `cfg`. */
static void apply_autostart(struct klest_config *cfg, toml_datum_t root)
{
	toml_datum_t arr = toml_seek(root, "startup.autostart");
	if (arr.type == TOML_UNKNOWN) {
		return;
	}
	if (arr.type != TOML_ARRAY) {
		fprintf(stderr, "klest: config: startup.autostart must be an array\n");
		return;
	}

	size_t count = 0;
	for (int32_t i = 0; i < arr.u.arr.size; i++) {
		if (count >= KLEST_AUTOSTART_MAX) {
			fprintf(stderr, "klest: config: startup.autostart truncated "
				"to %d entries\n", KLEST_AUTOSTART_MAX);
			break;
		}
		toml_datum_t elem = arr.u.arr.elem[i];
		if (elem.type != TOML_STRING) {
			fprintf(stderr,
				"klest: config: startup.autostart entries must be strings\n");
			continue;
		}
		int n = snprintf(cfg->autostart[count], KLEST_AUTOSTART_CMD_MAX,
			"%s", elem.u.s);
		if (n < 0 || (size_t)n >= KLEST_AUTOSTART_CMD_MAX) {
			fprintf(stderr, "klest: config: autostart command too long "
				"(max %d bytes)\n", KLEST_AUTOSTART_CMD_MAX - 1);
			continue;
		}
		count++;
	}
	cfg->autostart_count = count;
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
	apply_string(result.toptab, "keybindings.launcher",
		cfg->launcher, sizeof(cfg->launcher));
	apply_autostart(cfg, result.toptab);

	toml_free(result);
	return true;
}

/* Creates `dir` and all missing parents, like `mkdir -p`. */
static bool mkdir_p(const char *dir)
{
	char tmp[4096];
	int n = snprintf(tmp, sizeof(tmp), "%s", dir);
	if (n < 0 || (size_t)n >= sizeof(tmp)) {
		return false;
	}
	for (char *p = tmp + 1; *p != '\0'; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
			return false;
		}
		*p = '/';
	}
	return mkdir(tmp, 0700) == 0 || errno == EEXIST;
}

/* Ensures the directory containing `path` exists. */
static bool ensure_parent_dir(const char *path)
{
	const char *slash = strrchr(path, '/');
	if (slash == NULL || slash == path) {
		return true; /* no directory component, or the root */
	}
	char dir[4096];
	size_t len = (size_t)(slash - path);
	if (len >= sizeof(dir)) {
		return false;
	}
	memcpy(dir, path, len);
	dir[len] = '\0';
	return mkdir_p(dir);
}

/* Writes the body of a TOML basic string (no surrounding quotes), escaped. */
static void write_escaped(FILE *f, const char *value)
{
	for (const char *p = value; *p != '\0'; p++) {
		switch (*p) {
		case '\\': fputs("\\\\", f); break;
		case '"':  fputs("\\\"", f); break;
		case '\n': fputs("\\n", f); break;
		case '\t': fputs("\\t", f); break;
		default:   fputc(*p, f); break;
		}
	}
}

/* Writes `value` as a TOML basic string: key = "...", with escaping. */
static void write_toml_string(FILE *f, const char *key, const char *value)
{
	fprintf(f, "%s = \"", key);
	write_escaped(f, value);
	fputs("\"\n", f);
}

/* Writes the [startup] section with the autostart command array. */
static void write_autostart(FILE *f, const struct klest_config *cfg)
{
	fputs("\n[startup]\nautostart = [", f);
	for (size_t i = 0; i < cfg->autostart_count; i++) {
		if (i > 0) {
			fputs(", ", f);
		}
		fputc('"', f);
		write_escaped(f, cfg->autostart[i]);
		fputc('"', f);
	}
	fputs("]\n", f);
}

bool klest_config_save(const struct klest_config *cfg, const char *path)
{
	char resolved[4096];
	if (path == NULL) {
		if (!klest_config_default_path(resolved, sizeof(resolved))) {
			fprintf(stderr, "klest: config: cannot determine a config path\n");
			return false;
		}
		path = resolved;
	}

	if (!ensure_parent_dir(path)) {
		fprintf(stderr, "klest: config: cannot create directory for %s: %s\n",
			path, strerror(errno));
		return false;
	}

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "klest: config: cannot write %s: %s\n",
			path, strerror(errno));
		return false;
	}

	char color[16];
	if (!klest_config_format_color(cfg->background_color, color, sizeof(color))) {
		snprintf(color, sizeof(color), "#1a1c29");
	}

	fprintf(f, "# Klest configuration (written by klest-settings).\n\n");
	fprintf(f, "[appearance]\n");
	write_toml_string(f, "background_color", color);
	fprintf(f, "\n[keyboard]\n");
	fprintf(f, "repeat_rate = %d\n", cfg->keyboard_repeat_rate);
	fprintf(f, "repeat_delay = %d\n", cfg->keyboard_repeat_delay);
	fprintf(f, "\n[keybindings]\n");
	write_toml_string(f, "terminal", cfg->terminal);
	write_toml_string(f, "launcher", cfg->launcher);
	write_autostart(f, cfg);

	bool ok = ferror(f) == 0;
	if (fclose(f) != 0) {
		ok = false;
	}
	if (!ok) {
		fprintf(stderr, "klest: config: error writing %s\n", path);
	}
	return ok;
}
