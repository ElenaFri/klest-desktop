/*
 * Klest — a lean, adaptable Wayland desktop environment in C.
 *
 * Entry point: `--version` reports the version; otherwise Klest launches its
 * Wayland compositor (ADR 0002, 0004).
 */

#include "klest.h"

#include <config.h>

#include <stdio.h>
#include <string.h>

#if KLEST_HAVE_COMPOSITOR
#include "compositor/server.h"
#include "config/config.h"
#endif

static int run_compositor(void)
{
#if KLEST_HAVE_COMPOSITOR
	struct klest_config config;
	klest_config_load(&config, NULL);

	struct klest_server server = {0};
	server.config = &config;
	if (!klest_server_init(&server)) {
		return 1;
	}
	int status = klest_server_run(&server) ? 0 : 1;
	klest_server_finish(&server);
	return status;
#else
	fprintf(stderr, "Klest was built without compositor support.\n");
	return 1;
#endif
}

int main(int argc, char *argv[])
{
	if (argc > 1 &&
			(strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
		printf("Klest %s\n", klest_version());
		return 0;
	}

	return run_compositor();
}
