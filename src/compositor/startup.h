/* Klest — launching helper programs from the compositor (ADR 0009). */

#ifndef KLEST_COMPOSITOR_STARTUP_H
#define KLEST_COMPOSITOR_STARTUP_H

struct klest_config;

/*
 * Launches `cmd` via the shell, detached from the compositor. A NULL or empty
 * command is a no-op. Failures are logged, never fatal.
 */
void klest_startup_exec(const char *cmd);

/* Launches every command in `config->autostart` (no-op when config is NULL). */
void klest_startup_run(const struct klest_config *config);

#endif /* KLEST_COMPOSITOR_STARTUP_H */
