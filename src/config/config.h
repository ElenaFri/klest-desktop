/* Klest — runtime configuration (ADR 0007). */

#ifndef KLEST_CONFIG_CONFIG_H
#define KLEST_CONFIG_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Klest's configuration contract (ADR 0007):
 *   - Built-in defaults make Klest work out of the box, with no config file.
 *   - A TOML file overlays only the keys it sets; everything else stays default.
 *   - A missing file is not an error; a malformed file logs a warning and the
 *     defaults are kept, so the compositor always starts.
 */
struct klest_config {
	float background_color[4];     /* RGBA, 0..1 (opaque by default) */
	int32_t keyboard_repeat_rate;  /* key repeats per second */
	int32_t keyboard_repeat_delay; /* delay before repeat, milliseconds */
	char terminal[256];            /* command spawned on Alt+Return; "" disables */
};

/* Fills `cfg` with Klest's built-in defaults. */
void klest_config_set_defaults(struct klest_config *cfg);

/*
 * Loads configuration into `cfg`, starting from the built-in defaults and
 * overlaying any keys present in the TOML file.
 *
 * If `path` is NULL, the default location is resolved (see
 * klest_config_default_path). A missing file is not an error: `cfg` keeps the
 * defaults and true is returned. On a parse error, `cfg` is left filled with
 * defaults and false is returned.
 */
bool klest_config_load(struct klest_config *cfg, const char *path);

/*
 * Resolves Klest's default config file path into `buf` (capacity `cap`):
 *   $KLEST_CONFIG, else $XDG_CONFIG_HOME/klest/config.toml,
 *   else $HOME/.config/klest/config.toml.
 * Returns false if none could be determined or `buf` is too small.
 */
bool klest_config_default_path(char *buf, size_t cap);

/*
 * Parses a "#rrggbb" or "#rrggbbaa" colour string into `out` (RGBA, 0..1).
 * Missing alpha defaults to 1.0. Returns false on malformed input.
 */
bool klest_config_parse_color(const char *hex, float out[4]);

#endif /* KLEST_CONFIG_CONFIG_H */
