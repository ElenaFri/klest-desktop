/* Starts Klest headlessly and drives a small xdg-shell test client. */
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
			fprintf(stderr, "integration test: compositor exited during startup\n");
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
	fprintf(stderr, "integration test: timed out waiting for Wayland socket\n");
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

static pid_t start_process(const char *path, const char *argument)
{
	pid_t pid = fork();
	if (pid != 0) {
		return pid;
	}
	if (argument != NULL) {
		execl(path, path, argument, (char *)NULL);
	} else {
		execl(path, path, (char *)NULL);
	}
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
		fprintf(stderr, "integration test: compositor did not stop cleanly\n");
		return false;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "integration test: compositor exited abnormally\n");
		return false;
	}
	return true;
}

int main(int argc, char *argv[])
{
	if (argc != 4 && argc != 5) {
		fprintf(stderr, "usage: %s KLEST CLIENT RUNTIME_BASE [CLIENT_ARG]\n",
			argv[0]);
		return 2;
	}

	char runtime_dir[4096];
	int runtime_len = snprintf(runtime_dir, sizeof(runtime_dir),
		"%s/k-XXXXXX", argv[3]);
	if (runtime_len < 0 || (size_t)runtime_len >= sizeof(runtime_dir)) {
		fprintf(stderr, "integration test: runtime path too long\n");
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
		perror("configure integration-test environment");
		(void)rmdir(runtime_dir);
		return 1;
	}

	char socket_path[4096];
	char lock_path[4096];
	int socket_len = snprintf(socket_path, sizeof(socket_path),
		"%s/wayland-0", runtime_dir);
	int lock_len = snprintf(lock_path, sizeof(lock_path),
		"%s/wayland-0.lock", runtime_dir);
	if (socket_len < 0 || (size_t)socket_len >= sizeof(socket_path) ||
			lock_len < 0 || (size_t)lock_len >= sizeof(lock_path)) {
		fprintf(stderr, "integration test: runtime path too long\n");
		(void)rmdir(runtime_dir);
		return 1;
	}

	pid_t server_pid = start_process(argv[1], NULL);
	if (server_pid < 0) {
		perror("fork compositor");
		(void)rmdir(runtime_dir);
		return 1;
	}

	bool ok = wait_for_socket(&server_pid, socket_path);
	if (ok) {
		pid_t client_pid = start_process(argv[2], argc == 5 ? argv[4] : NULL);
		if (client_pid < 0) {
			perror("fork client");
			ok = false;
		} else {
			int client_status;
			if (!wait_for_child(client_pid, &client_status, 10000)) {
				(void)kill(client_pid, SIGKILL);
				(void)waitpid(client_pid, &client_status, 0);
				fprintf(stderr, "integration test: client timed out\n");
				ok = false;
			} else if (!WIFEXITED(client_status) ||
					WEXITSTATUS(client_status) != 0) {
				fprintf(stderr, "integration test: client failed\n");
				ok = false;
			}
		}
	}

	if (server_pid > 0 && !stop_compositor(server_pid)) {
		ok = false;
	}
	(void)unlink(socket_path);
	(void)unlink(lock_path);
	(void)rmdir(runtime_dir);
	return ok ? 0 : 1;
}
