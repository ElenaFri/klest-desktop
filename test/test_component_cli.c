/*
 * Klest — component command-line and no-compositor behaviour.
 *
 * Every bundled client is a tiny program with the same CLI contract: it accepts
 * only an optional "--once", rejects anything else with exit code 2, and fails
 * cleanly (exit code 1) when it cannot reach a compositor. The compositor binary
 * itself must answer "--version". These are the outward promises packaging and
 * session scripts rely on, so they are pinned here.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Runs `path` with the given single argument (NULL for none) and the given
 * environment tweak, and returns its exit code, or -1 if it did not exit
 * normally. When `no_compositor` is set, the child is pointed at a Wayland
 * socket that does not exist so its connection attempt fails. */
static int run_with(const char *path, const char *argument, bool no_compositor)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		if (no_compositor) {
			(void)setenv("XDG_RUNTIME_DIR", "/tmp", 1);
			(void)setenv("WAYLAND_DISPLAY", "klest-cli-test-no-such", 1);
		}
		if (argument != NULL) {
			execl(path, path, argument, (char *)NULL);
		} else {
			execl(path, path, (char *)NULL);
		}
		perror(path);
		_exit(127);
	}
	int status = -1;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		/* Reap after an interrupting signal. */
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* A component rejects an unknown argument with the usage exit code (2) and, with
 * no compositor to connect to, gives up cleanly with code 1 (never a crash). */
static bool check_component(const char *path)
{
	int usage_code = run_with(path, "--not-a-flag", false);
	if (usage_code != 2) {
		fprintf(stderr, "cli test: %s: bad argument gave %d, expected 2\n",
			path, usage_code);
		return false;
	}
	int no_compositor_code = run_with(path, NULL, true);
	if (no_compositor_code != 1) {
		fprintf(stderr, "cli test: %s: no compositor gave %d, expected 1\n",
			path, no_compositor_code);
		return false;
	}
	return true;
}

/* The compositor must fail cleanly (exit 1), not crash, when its backend cannot
 * be created — e.g. an unsupported WLR_BACKENDS value. This drives the server's
 * init-failure cleanup path. */
static int run_broken_backend(const char *klest)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		(void)setenv("WLR_BACKENDS", "klest-invalid-backend", 1);
		(void)setenv("XDG_RUNTIME_DIR", "/tmp", 1);
		(void)unsetenv("WAYLAND_DISPLAY");
		(void)unsetenv("DISPLAY");
		execl(klest, klest, (char *)NULL);
		perror(klest);
		_exit(127);
	}
	int status = -1;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		/* Reap after an interrupting signal. */
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s KLEST COMPONENT...\n", argv[0]);
		return 2;
	}

	/* The compositor binary reports its version and exits successfully. */
	int version_code = run_with(argv[1], "--version", false);
	if (version_code != 0) {
		fprintf(stderr, "cli test: klest --version gave %d, expected 0\n",
			version_code);
		return 1;
	}

	int init_fail_code = run_broken_backend(argv[1]);
	if (init_fail_code != 1) {
		fprintf(stderr, "cli test: klest with a broken backend gave %d, "
			"expected 1\n", init_fail_code);
		return 1;
	}

	for (int i = 2; i < argc; i++) {
		if (!check_component(argv[i])) {
			return 1;
		}
	}
	return 0;
}
