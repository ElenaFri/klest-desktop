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
	assert(near(cfg.background_color[3], 1.0f));
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

static void test_missing_file_keeps_defaults(void)
{
	struct klest_config cfg;
	bool ok = klest_config_load(&cfg, "klest-does-not-exist-xyz.toml");

	assert(ok); /* a missing file is not an error */
	assert(cfg.keyboard_repeat_rate == 25);
	assert(cfg.terminal[0] == '\0');
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
		"terminal = \"foot\"\n",
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
	test_missing_file_keeps_defaults();
	test_overlay_from_file();
	test_parse_error_returns_false();
	return 0;
}
