/* Exercises notification delivery, explicit close, and expiry over D-Bus. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#define NOTIFY_SERVICE "org.freedesktop.Notifications"
#define NOTIFY_PATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
#define NOTIFY_CLOSED_MATCH \
	"type='signal',sender='org.freedesktop.Notifications'," \
	"path='/org/freedesktop/Notifications'," \
	"interface='org.freedesktop.Notifications',member='NotificationClosed'"

#define CLOSED_REASON_EXPIRED 1U
#define CLOSED_REASON_CLOSED 3U
#define TEST_EXPIRE_MS 150
#define TEST_TIMEOUT_MS 5000
#define TEST_MAX_EVENTS 8U

struct closed_event {
	uint32_t id;
	uint32_t reason;
};

struct notify_test {
	sd_bus *bus;
	sd_bus_slot *closed_slot;
	struct closed_event events[TEST_MAX_EVENTS];
	size_t event_count;
};

static uint64_t monotonic_milliseconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static int record_notification_closed(sd_bus_message *message, void *userdata,
	sd_bus_error *ret_error)
{
	struct notify_test *test = userdata;
	(void)ret_error;
	uint32_t id;
	uint32_t reason;
	int r = sd_bus_message_read(message, "uu", &id, &reason);
	if (r < 0) {
		return r;
	}
	if (test->event_count < TEST_MAX_EVENTS) {
		test->events[test->event_count++] = (struct closed_event){id, reason};
	}
	return 0;
}

static bool test_bus_connect(struct notify_test *test)
{
	int r = sd_bus_open_user(&test->bus);
	if (r < 0) {
		fprintf(stderr, "notify test: cannot connect to private session bus: %s\n",
			strerror(-r));
		return false;
	}
	r = sd_bus_add_match(test->bus, &test->closed_slot, NOTIFY_CLOSED_MATCH,
		record_notification_closed, test);
	if (r < 0) {
		fprintf(stderr, "notify test: cannot subscribe to close signal: %s\n",
			strerror(-r));
		return false;
	}
	return true;
}

static bool wait_for_daemon(struct notify_test *test)
{
	uint64_t deadline = monotonic_milliseconds() + TEST_TIMEOUT_MS;
	while (monotonic_milliseconds() < deadline) {
		sd_bus_error error = SD_BUS_ERROR_NULL;
		sd_bus_message *reply = NULL;
		int r = sd_bus_call_method(test->bus, NOTIFY_SERVICE, NOTIFY_PATH,
			NOTIFY_IFACE, "GetServerInformation", &error, &reply, "");
		sd_bus_message_unref(reply);
		sd_bus_error_free(&error);
		if (r >= 0) {
			return true;
		}
		struct timespec delay = {.tv_sec = 0, .tv_nsec = 20000000L};
		while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
			/* Resume the short retry delay after a signal. */
		}
	}
	fprintf(stderr, "notify test: daemon did not acquire its D-Bus name\n");
	return false;
}

static bool append_empty_array(sd_bus_message *message, char type,
	const char *contents)
{
	return sd_bus_message_open_container(message, type, contents) >= 0 &&
		sd_bus_message_close_container(message) >= 0;
}

static bool send_notification(struct notify_test *test, const char *summary,
	int32_t expire_ms, uint32_t *id)
{
	sd_bus_message *request = NULL;
	sd_bus_message *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_message_new_method_call(test->bus, &request,
		NOTIFY_SERVICE, NOTIFY_PATH, NOTIFY_IFACE, "Notify");
	if (r >= 0) {
		r = sd_bus_message_append(request, "susss", "Klest test", 0U, "",
			summary, "test body");
	}
	if (r >= 0 && !append_empty_array(request, 'a', "s")) {
		r = -EINVAL;
	}
	if (r >= 0 && !append_empty_array(request, 'a', "{sv}")) {
		r = -EINVAL;
	}
	if (r >= 0) {
		r = sd_bus_message_append(request, "i", expire_ms);
	}
	if (r >= 0) {
		r = sd_bus_call(test->bus, request, 2U * 1000000U, &error, &reply);
	}
	if (r >= 0) {
		r = sd_bus_message_read(reply, "u", id);
	}
	if (r < 0) {
		fprintf(stderr, "notify test: Notify failed: %s\n",
			error.message != NULL ? error.message : strerror(-r));
	}
	sd_bus_error_free(&error);
	sd_bus_message_unref(reply);
	sd_bus_message_unref(request);
	return r >= 0 && *id != 0;
}

static bool close_notification(struct notify_test *test, uint32_t id)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(test->bus, NOTIFY_SERVICE, NOTIFY_PATH,
		NOTIFY_IFACE, "CloseNotification", &error, NULL, "u", id);
	if (r < 0) {
		fprintf(stderr, "notify test: CloseNotification failed: %s\n",
			error.message != NULL ? error.message : strerror(-r));
	}
	sd_bus_error_free(&error);
	return r >= 0;
}

static bool has_close_event(const struct notify_test *test, size_t start,
	uint32_t id, uint32_t reason)
{
	for (size_t i = start; i < test->event_count; i++) {
		if (test->events[i].id == id && test->events[i].reason == reason) {
			return true;
		}
	}
	return false;
}

static bool wait_for_close_event(struct notify_test *test, uint32_t id,
	uint32_t reason, size_t start)
{
	uint64_t deadline = monotonic_milliseconds() + TEST_TIMEOUT_MS;
	while (monotonic_milliseconds() < deadline) {
		int r;
		do {
			r = sd_bus_process(test->bus, NULL);
		} while (r > 0);
		if (r < 0) {
			fprintf(stderr, "notify test: D-Bus processing failed: %s\n",
				strerror(-r));
			return false;
		}
		if (has_close_event(test, start, id, reason)) {
			return true;
		}
		r = sd_bus_wait(test->bus, 100000U);
		if (r < 0) {
			fprintf(stderr, "notify test: D-Bus wait failed: %s\n",
				strerror(-r));
			return false;
		}
	}
	fprintf(stderr, "notify test: missing close signal for id %u (reason %u)\n",
		id, reason);
	return false;
}

static bool exercise_lifecycle(struct notify_test *test)
{
	if (!wait_for_daemon(test)) {
		return false;
	}

	uint32_t close_id = 0;
	if (!send_notification(test, "explicit close", 0, &close_id)) {
		return false;
	}
	size_t close_event_start = test->event_count;
	if (!close_notification(test, close_id) ||
			!wait_for_close_event(test, close_id, CLOSED_REASON_CLOSED,
				close_event_start)) {
		return false;
	}

	uint32_t expiry_id = 0;
	size_t event_start = test->event_count;
	return send_notification(test, "automatic expiry", TEST_EXPIRE_MS,
			&expiry_id) &&
		wait_for_close_event(test, expiry_id, CLOSED_REASON_EXPIRED,
			event_start);
}

static pid_t start_daemon(const char *path)
{
	pid_t pid = fork();
	if (pid != 0) {
		return pid;
	}
	execl(path, path, (char *)NULL);
	perror("notify test: exec klest-notify");
	_exit(127);
}

static void stop_daemon(pid_t pid)
{
	if (pid <= 0) {
		return;
	}
	if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
		perror("notify test: kill daemon");
	}
	int status;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		/* Reap the daemon after an interrupting signal. */
	}
}

int main(void)
{
	const char *daemon_path = getenv("KLEST_NOTIFY_TEST_EXECUTABLE");
	if (daemon_path == NULL || daemon_path[0] == '\0') {
		fprintf(stderr, "notify test: daemon executable path is unset\n");
		return 1;
	}

	struct notify_test test = {0};
	pid_t daemon_pid = start_daemon(daemon_path);
	if (daemon_pid < 0) {
		perror("notify test: fork daemon");
	}
	bool bus_ready = daemon_pid > 0 && test_bus_connect(&test);
	bool ok = bus_ready && exercise_lifecycle(&test);

	if (test.closed_slot != NULL) {
		sd_bus_slot_unref(test.closed_slot);
	}
	if (test.bus != NULL) {
		sd_bus_unref(test.bus);
	}
	stop_daemon(daemon_pid);
	return ok ? 0 : 1;
}
