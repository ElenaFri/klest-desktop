/*
 * Klest — launching helper programs from the compositor (ADR 0009).
 *
 * Both the Alt+Return terminal binding and the session autostart list spawn
 * detached shell commands; this module is the single place that does it.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "startup.h"
#include "config/config.h"

void klest_startup_exec(const char *cmd)
{
	if (cmd == NULL || cmd[0] == '\0') {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
		_exit(127);
	} else if (pid < 0) {
		wlr_log(WLR_ERROR, "failed to fork for '%s'", cmd);
	}
}

void klest_startup_run(const struct klest_config *config)
{
	if (config == NULL) {
		return;
	}
	for (size_t i = 0; i < config->autostart_count; i++) {
		wlr_log(WLR_INFO, "autostart: %s", config->autostart[i]);
		klest_startup_exec(config->autostart[i]);
	}
}
