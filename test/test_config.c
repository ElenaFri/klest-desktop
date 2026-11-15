/* Klest — configuration unit tests (ADR 0007). */

#define _POSIX_C_SOURCE 200809L

#include "config/config.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool near(float a, float b)
{
	return fabsf(a - b) < 0.01f;
}

static void test_defaults(void)
{
	struct klest_config cfg;
	klest_config_set_defaults(&cfg);

	assert(cfg.keyboard_repeat_rate == 25);
	assert(cfg.keyboard_repeat_delay == 600);
	assert(strcmp(cfg.terminal, "foot") == 0);
	assert(near(cfg.background_color[3], 1.0f));
	assert(cfg.autostart_count == 0);
}

static void test_parse_color(void)
{
	float c[4];

	assert(klest_config_parse_color("#ff8800", c));
	assert(near(c[0], 1.0f) && near(c[1], 0.533f) && near(c[2], 0.0f));
	assert(near(c[3], 1.0f));

	assert(klest_config_parse_color("#00000080", c));
	assert(near(c[3], 0.5f));

	assert(!klest_config_parse_color("ff8800", c));  /* no leading # */
	assert(!klest_config_parse_color("#fff", c));    /* wrong length */
	assert(!klest_config_parse_color("#gggggg", c)); /* non-hex */
	assert(!klest_config_parse_color(NULL, c));
}

static void test_format_color(void)
{
	char buf[16];
	float opaque[4] = {1.0f, 0.533f, 0.0f, 1.0f};
	assert(klest_config_format_color(opaque, buf, sizeof(buf)));
	assert(strcmp(buf, "#ff8800") == 0);

	float translucent[4] = {0.0f, 0.0f, 0.0f, 0.5f};
	assert(klest_config_format_color(translucent, buf, sizeof(buf)));
	assert(strcmp(buf, "#00000080") == 0);

	assert(!klest_config_format_color(opaque, buf, 4)); /* too small */
}

static void test_save_round_trip(void)
{
	const char *path = "klest-test-roundtrip.toml";

	struct klest_config out;
	klest_config_set_defaults(&out);
	out.keyboard_repeat_rate = 33;
	out.keyboard_repeat_delay = 275;
	out.background_color[0] = 1.0f;
	out.background_color[1] = 0.0f;
	out.background_color[2] = 0.5f;
	snprintf(out.terminal, sizeof(out.terminal), "foot -e \"tmux\"");
	snprintf(out.launcher, sizeof(out.launcher), "klest-launcher");
	out.autostart_count = 2;
	snprintf(out.autostart[0], KLEST_AUTOSTART_CMD_MAX, "klest-background");
	snprintf(out.autostart[1], KLEST_AUTOSTART_CMD_MAX, "echo \"hi\"");

	assert(klest_config_save(&out, path));

	struct klest_config in;
	bool ok = klest_config_load(&in, path);
	remove(path);

	assert(ok);
	assert(in.keyboard_repeat_rate == 33);
	assert(in.keyboard_repeat_delay == 275);
	assert(near(in.background_color[0], 1.0f));
	assert(near(in.background_color[2], 0.5f));
	assert(strcmp(in.terminal, "foot -e \"tmux\"") == 0); /* quotes survive escaping */
	assert(strcmp(in.launcher, "klest-launcher") == 0);
	assert(in.autostart_count == 2);
	assert(strcmp(in.autostart[0], "klest-background") == 0);
	assert(strcmp(in.autostart[1], "echo \"hi\"") == 0); /* quotes survive escaping */
}

static void test_missing_file_keeps_defaults(void)
{
	struct klest_config cfg;
	bool ok = klest_config_load(&cfg, "klest-does-not-exist-xyz.toml");

	assert(ok); /* a missing file is not an error */
	assert(cfg.keyboard_repeat_rate == 25);
	assert(strcmp(cfg.terminal, "foot") == 0);
	assert(cfg.launcher[0] == '\0');
}

static void test_overlay_from_file(void)
{
	const char *path = "klest-test-config.toml";
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fputs("[appearance]\n"
		"background_color = \"#101828\"\n"
		"[keyboard]\n"
		"repeat_rate = 40\n"
		"[keybindings]\n"
		"terminal = \"foot\"\n"
		"launcher = \"klest-launcher\"\n"
		"[startup]\n"
		"autostart = [\"klest-background\", \"klest-panel\"]\n",
		f);
	fclose(f);

	struct klest_config cfg;
	bool ok = klest_config_load(&cfg, path);
	remove(path);

	assert(ok);
	assert(cfg.keyboard_repeat_rate == 40);     /* overridden */
	assert(cfg.keyboard_repeat_delay == 600);   /* kept from defaults */
	assert(near(cfg.background_color[0], 0.063f));
	assert(strcmp(cfg.terminal, "foot") == 0);
	assert(strcmp(cfg.launcher, "klest-launcher") == 0);
	assert(cfg.autostart_count == 2);
	assert(strcmp(cfg.autostart[0], "klest-background") == 0);
	assert(strcmp(cfg.autostart[1], "klest-panel") == 0);
}

static void test_parse_error_returns_false(void)
{
	const char *path = "klest-test-bad.toml";
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fputs("this is = not valid = toml\n", f);
	fclose(f);

	struct klest_config cfg;
	bool ok = klest_config_load(&cfg, path);
	remove(path);

	assert(!ok);
	assert(cfg.keyboard_repeat_rate == 25); /* still usable */
}

/* ------------------------------------------------------------------ */
/* Malformed values: the ADR 0007 contract is "warn and keep the defaults,
 * so the compositor always starts". Each key must uphold it on its own. */

static void write_config(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fputs(content, f);
	fclose(f);
}

/* Wrong types and out-of-range values must not take any key down. */
static void test_malformed_values_keep_defaults(void)
{
	const char *path = "klest-test-bad-values.toml";
	write_config(path,
		"[appearance]\n"
		"background_color = 12\n"        /* not a string */
		"[keyboard]\n"
		"repeat_rate = \"fast\"\n"         /* not an integer */
		"repeat_delay = 0\n"              /* out of range */
		"[keybindings]\n"
		"terminal = 42\n"                 /* not a string */
		"launcher = 42\n"
		"[startup]\n"
		"autostart = \"not-an-array\"\n");

	struct klest_config cfg;
	bool ok = klest_config_load(&cfg, path);
	remove(path);

	assert(ok); /* warnings only: the file is valid TOML */
	assert(near(cfg.background_color[0], 0.10f));
	assert(cfg.keyboard_repeat_rate == 25);
	assert(cfg.keyboard_repeat_delay == 600);
	assert(strcmp(cfg.terminal, "foot") == 0);
	assert(cfg.launcher[0] == '\0');
	assert(cfg.autostart_count == 0);
}

/* A syntactically fine but semantically wrong colour keeps the default. */
static void test_invalid_color_keeps_default(void)
{
	const char *path = "klest-test-bad-color.toml";
	write_config(path, "[appearance]\nbackground_color = \"red\"\n");

	struct klest_config cfg;
	assert(klest_config_load(&cfg, path));
	remove(path);

	assert(near(cfg.background_color[0], 0.10f));
	assert(near(cfg.background_color[2], 0.16f));
}

/* A too-long command disables the binding instead of truncating garbage. */
static void test_oversized_command_clears_binding(void)
{
	const char *path = "klest-test-long-cmd.toml";
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fputs("[keybindings]\nterminal = \"", f);
	for (int i = 0; i < 300; i++) {
		fputc('x', f);
	}
	fputs("\"\n", f);
	fclose(f);

	struct klest_config cfg;
	assert(klest_config_load(&cfg, path));
	remove(path);

	assert(cfg.terminal[0] == '\0');
}

/* Autostart keeps only the usable entries: wrong types and oversized commands
 * are skipped, and the list never exceeds its capacity. */
static void test_autostart_filters_bad_entries(void)
{
	const char *path = "klest-test-autostart.toml";
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fputs("[startup]\nautostart = [42, \"true-cmd\", ", f);
	fputc('"', f);
	for (int i = 0; i < 300; i++) {
		fputc('y', f);
	}
	fputs("\", \"other-cmd\"]\n", f);
	fclose(f);

	struct klest_config cfg;
	assert(klest_config_load(&cfg, path));
	remove(path);

	assert(cfg.autostart_count == 2);
	assert(strcmp(cfg.autostart[0], "true-cmd") == 0);
	assert(strcmp(cfg.autostart[1], "other-cmd") == 0);
}

static void test_autostart_truncated_at_capacity(void)
{
	const char *path = "klest-test-autostart-max.toml";
	FILE *f = fopen(path, "w");
	assert(f != NULL);
	fputs("[startup]\nautostart = [", f);
	for (int i = 0; i < KLEST_AUTOSTART_MAX + 5; i++) {
		fprintf(f, "%s\"cmd-%d\"", i > 0 ? ", " : "", i);
	}
	fputs("]\n", f);
	fclose(f);

	struct klest_config cfg;
	assert(klest_config_load(&cfg, path));
	remove(path);

	assert(cfg.autostart_count == KLEST_AUTOSTART_MAX);
	assert(strcmp(cfg.autostart[0], "cmd-0") == 0);
	assert(strcmp(cfg.autostart[KLEST_AUTOSTART_MAX - 1],
		"cmd-15") == 0);
}

/* ------------------------------------------------------------------ */
/* Default path resolution: KLEST_CONFIG, then XDG_CONFIG_HOME, then HOME. */

static void test_default_path_resolution(void)
{
	char buf[4096];

	/* Explicit override wins. */
	setenv("KLEST_CONFIG", "/explicit/klest.toml", 1);
	assert(klest_config_default_path(buf, sizeof(buf)));
	assert(strcmp(buf, "/explicit/klest.toml") == 0);

	/* XDG_CONFIG_HOME is the next choice. */
	unsetenv("KLEST_CONFIG");
	setenv("XDG_CONFIG_HOME", "/xdg-home", 1);
	assert(klest_config_default_path(buf, sizeof(buf)));
	assert(strcmp(buf, "/xdg-home/klest/config.toml") == 0);

	/* HOME is the fallback. */
	unsetenv("XDG_CONFIG_HOME");
	setenv("HOME", "/home/someone", 1);
	assert(klest_config_default_path(buf, sizeof(buf)));
	assert(strcmp(buf, "/home/someone/.config/klest/config.toml") == 0);

	/* No location at all: not an error, just nothing to read. */
	unsetenv("HOME");
	assert(!klest_config_default_path(buf, sizeof(buf)));

	/* Restore the environment for the other tests. */
	setenv("HOME", "/root", 1);
}

/* klest_config_load(NULL) reads the resolved default location. */
static void test_load_uses_klest_config_env(void)
{
	const char *path = "klest-test-env-target.toml";
	write_config(path, "[keyboard]\nrepeat_rate = 40\n");
	setenv("KLEST_CONFIG", path, 1);

	struct klest_config cfg;
	bool ok = klest_config_load(&cfg, NULL);
	unsetenv("KLEST_CONFIG");
	remove(path);

	assert(ok);
	assert(cfg.keyboard_repeat_rate == 40);
}

/* ------------------------------------------------------------------ */
/* Saving: parent directories are created, and failures are reported. */

static void test_save_creates_parent_dirs(void)
{
	const char *path = "klest-test-nested/a/b/config.toml";
	struct klest_config cfg;
	klest_config_set_defaults(&cfg);

	assert(klest_config_save(&cfg, path));

	struct klest_config in;
	assert(klest_config_load(&in, path));
	assert(in.keyboard_repeat_rate == 25);

	remove(path);
	rmdir("klest-test-nested/a/b");
	rmdir("klest-test-nested/a");
	rmdir("klest-test-nested");
}

/* A path whose parent is a regular file must fail, not create anything. */
static void test_save_reports_failures(void)
{
	write_config("klest-test-not-a-dir", "x");

	struct klest_config cfg;
	klest_config_set_defaults(&cfg);
	assert(!klest_config_save(&cfg, "klest-test-not-a-dir/config.toml"));

	remove("klest-test-not-a-dir");
}

/* Colours outside 0..1 clamp instead of producing garbage bytes. */
static void test_format_color_clamps(void)
{
	float hot[4] = {1.5f, -0.5f, 0.0f, 2.0f};
	char buf[16];
	assert(klest_config_format_color(hot, buf, sizeof(buf)));
	assert(strcmp(buf, "#ff0000") == 0);
}

int main(void)
{
	test_defaults();
	test_parse_color();
	test_format_color();
	test_save_round_trip();
	test_missing_file_keeps_defaults();
	test_overlay_from_file();
	test_parse_error_returns_false();
	test_malformed_values_keep_defaults();
	test_invalid_color_keeps_default();
	test_oversized_command_clears_binding();
	test_autostart_filters_bad_entries();
	test_autostart_truncated_at_capacity();
	test_default_path_resolution();
	test_load_uses_klest_config_env();
	test_save_creates_parent_dirs();
	test_save_reports_failures();
	test_format_color_clamps();
	return 0;
}
