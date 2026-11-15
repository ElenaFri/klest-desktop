/*
 * Starts Klest headlessly with an autostart configuration and verifies the
 * session commands actually run (ADR 0009) — and that a failing command leaves
 * the compositor running.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_milliseconds(long milliseconds)
{
	struct timespec delay = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (milliseconds % 1000) * 1000000,
	};
	while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
		/* Retry the remaining delay after an interrupting signal. */
	}
}

static bool wait_for_socket(pid_t *server_pid, const char *socket_path)
{
	for (int attempt = 0; attempt < 200; attempt++) {
		int status;
		pid_t result = waitpid(*server_pid, &status, WNOHANG);
		if (result == *server_pid) {
			*server_pid = -1;
			fprintf(stderr, "autostart test: compositor exited during startup\n");
			return false;
		}
		if (result < 0 && errno != EINTR) {
			perror("waitpid compositor");
			return false;
		}
		if (access(socket_path, F_OK) == 0) {
			return true;
		}
		sleep_milliseconds(50);
	}
	fprintf(stderr, "autostart test: timed out waiting for Wayland socket\n");
	return false;
}

static bool wait_for_child(pid_t pid, int *status, int timeout_ms)
{
	int attempts = timeout_ms / 50;
	for (int attempt = 0; attempt < attempts; attempt++) {
		pid_t result = waitpid(pid, status, WNOHANG);
		if (result == pid) {
			return true;
		}
		if (result < 0 && errno != EINTR) {
			perror("waitpid child");
			return false;
		}
		sleep_milliseconds(50);
	}
	return false;
}

static pid_t start_compositor(const char *path)
{
	pid_t pid = fork();
	if (pid != 0) {
		return pid;
	}
	execl(path, path, (char *)NULL);
	perror(path);
	_exit(127);
}

static bool stop_compositor(pid_t pid)
{
	if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
		perror("kill compositor");
		return false;
	}
	int status;
	if (!wait_for_child(pid, &status, 10000)) {
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		fprintf(stderr, "autostart test: compositor did not stop cleanly\n");
		return false;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "autostart test: compositor exited abnormally\n");
		return false;
	}
	return true;
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s KLEST RUNTIME_BASE\n", argv[0]);
		return 2;
	}

	char runtime_dir[4096];
	int runtime_len = snprintf(runtime_dir, sizeof(runtime_dir),
		"%s/a-XXXXXX", argv[2]);
	if (runtime_len < 0 || (size_t)runtime_len >= sizeof(runtime_dir)) {
		fprintf(stderr, "autostart test: runtime path too long\n");
		return 1;
	}
	if (mkdtemp(runtime_dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	if (chmod(runtime_dir, 0700) != 0 ||
			setenv("XDG_RUNTIME_DIR", runtime_dir, 1) != 0 ||
			setenv("WAYLAND_DISPLAY", "wayland-0", 1) != 0 ||
			setenv("WLR_BACKENDS", "headless", 1) != 0 ||
			setenv("WLR_RENDERER", "pixman", 1) != 0 ||
			setenv("WLR_HEADLESS_OUTPUTS", "1", 1) != 0 ||
			setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1) != 0) {
		perror("configure autostart-test environment");
		(void)rmdir(runtime_dir);
		return 1;
	}

	char socket_path[4096];
	char marker_path[4096];
	char config_path[4096];
	int socket_len = snprintf(socket_path, sizeof(socket_path),
		"%s/wayland-0", runtime_dir);
	int marker_len = snprintf(marker_path, sizeof(marker_path),
		"%s/autostart-marker", runtime_dir);
	int config_len = snprintf(config_path, sizeof(config_path),
		"%s/autostart.toml", runtime_dir);
	if (socket_len < 0 || (size_t)socket_len >= sizeof(socket_path) ||
			marker_len < 0 || (size_t)marker_len >= sizeof(marker_path) ||
			config_len < 0 || (size_t)config_len >= sizeof(config_path)) {
		fprintf(stderr, "autostart test: runtime path too long\n");
		(void)rmdir(runtime_dir);
		return 1;
	}

	/* One working command and one that cannot exist: the session must start
	 * the list regardless, and survive the failing entry. */
	FILE *config = fopen(config_path, "w");
	if (config == NULL) {
		perror("fopen config");
		(void)rmdir(runtime_dir);
		return 1;
	}
	fprintf(config, "[startup]\nautostart = [\"touch %s\", "
		"\"klest-no-such-command-xyz\"]\n", marker_path);
	bool write_ok = ferror(config) == 0;
	fclose(config);
	if (!write_ok || setenv("KLEST_CONFIG", config_path, 1) != 0) {
		fprintf(stderr, "autostart test: cannot set up config\n");
		(void)rmdir(runtime_dir);
		return 1;
	}

	pid_t server_pid = start_compositor(argv[1]);
	if (server_pid < 0) {
		perror("fork compositor");
		(void)rmdir(runtime_dir);
		return 1;
	}

	bool ok = wait_for_socket(&server_pid, socket_path);

	/* The marker proves the autostart command ran. */
	bool marker_seen = false;
	for (int attempt = 0; attempt < 200 && ok; attempt++) {
		if (access(marker_path, F_OK) == 0) {
			marker_seen = true;
			break;
		}
		sleep_milliseconds(50);
	}
	if (ok && !marker_seen) {
		fprintf(stderr, "autostart test: marker never appeared\n");
		ok = false;
	}

	/* The failing entry must not have taken the compositor down. */
	if (ok) {
		int status;
		pid_t result = waitpid(server_pid, &status, WNOHANG);
		if (result == server_pid) {
			fprintf(stderr, "autostart test: compositor died after a "
				"failing autostart entry\n");
			ok = false;
		}
	}

	if (server_pid > 0 && !stop_compositor(server_pid)) {
		ok = false;
	}
	(void)unlink(marker_path);
	(void)unlink(config_path);
	(void)unlink(socket_path);
	(void)rmdir(runtime_dir);
	return ok ? 0 : 1;
}
