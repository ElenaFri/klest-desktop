/* Klest — configuration unit tests (ADR 0007). */

#include "config/config.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void)
{
	test_defaults();
	test_parse_color();
	test_format_color();
	test_save_round_trip();
	test_missing_file_keeps_defaults();
	test_overlay_from_file();
	test_parse_error_returns_false();
	return 0;
}
