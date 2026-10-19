/*
 * Klest — desktop notification daemon (ADR 0011).
 *
 * Serves the standard org.freedesktop.Notifications D-Bus interface and renders
 * active notifications as a stack of cards in the top-right corner, on a
 * transparent, click-through layer-shell overlay. A single poll loop
 * multiplexes the Wayland and D-Bus file descriptors and expires notifications
 * on their timeout. D-Bus is handled with sd-bus (libsystemd).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <systemd/sd-bus.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <config.h>

#include "wayland/font.h"
#include "wayland/layer_client.h"
#include "wayland/shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define NOTIFY_PATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
#define NOTIFY_NAME "org.freedesktop.Notifications"

#define NOTIFY_WIDTH 380U
#define NOTIFY_MARGIN 12U
#define NOTIFY_PAD 12U
#define NOTIFY_CARD_H 72U
#define NOTIFY_MAX 5U
#define NOTIFY_DEFAULT_MS 5000

#define NOTIFY_CARD_BG 0xff242736U
#define NOTIFY_ACCENT 0xff5c8fe8U
#define NOTIFY_FG 0xffe8eaf5U
#define NOTIFY_DIM 0xff9aa0b5U

/* Close reasons, per the freedesktop specification. */
#define NOTIFY_REASON_EXPIRED 1U
#define NOTIFY_REASON_CLOSED 3U

struct notification {
	uint32_t id;
	int expire_ms;       /* 0 = never expires */
	uint64_t shown_usec; /* CLOCK_MONOTONIC, when shown */
	char app_name[64];
	char summary[96];
	char body[160];
};

struct notify {
	struct klest_layer_client layer;
	sd_bus *bus;
	sd_bus_slot *slot;

	struct notification items[NOTIFY_MAX];
	size_t count;
	uint32_t next_id;

	struct wl_buffer *buffer;
	uint32_t mapped_width;
	uint32_t mapped_height;
	bool needs_redraw;
	bool running;
};

static uint64_t now_monotonic_usec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000U + (uint64_t)ts.tv_nsec / 1000U;
}

/* ---------------------------------------------------------------- model */

static void copy_bounded(char *dst, size_t cap, const char *src)
{
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	int n = snprintf(dst, cap, "%s", src);
	if (n < 0) {
		dst[0] = '\0';
	}
}

static int resolve_timeout(int32_t requested)
{
	if (requested < 0) {
		return NOTIFY_DEFAULT_MS;
	}
	return requested; /* 0 means "never", honoured as-is */
}

static struct notification *find_by_id(struct notify *n, uint32_t id)
{
	for (size_t i = 0; i < n->count; i++) {
		if (n->items[i].id == id) {
			return &n->items[i];
		}
	}
	return NULL;
}

/* Returns a slot for a new or replaced notification, dropping the oldest when
 * the stack is full. */
static struct notification *acquire_slot(struct notify *n, uint32_t replaces_id)
{
	if (replaces_id != 0) {
		struct notification *existing = find_by_id(n, replaces_id);
		if (existing != NULL) {
			return existing;
		}
	}
	if (n->count < NOTIFY_MAX) {
		return &n->items[n->count++];
	}
	memmove(&n->items[0], &n->items[1],
		(NOTIFY_MAX - 1) * sizeof(n->items[0]));
	return &n->items[NOTIFY_MAX - 1];
}

static uint32_t notify_add(struct notify *n, uint32_t replaces_id,
	const char *app, const char *summary, const char *body, int32_t expire)
{
	struct notification *slot = acquire_slot(n, replaces_id);
	uint32_t id = replaces_id != 0 ? replaces_id : n->next_id++;
	if (n->next_id == 0) {
		n->next_id = 1; /* ids are never zero */
	}
	slot->id = id;
	slot->expire_ms = resolve_timeout(expire);
	slot->shown_usec = now_monotonic_usec();
	copy_bounded(slot->app_name, sizeof(slot->app_name), app);
	copy_bounded(slot->summary, sizeof(slot->summary), summary);
	copy_bounded(slot->body, sizeof(slot->body), body);
	n->needs_redraw = true;
	return id;
}

static void notify_close_at(struct notify *n, size_t index, uint32_t reason)
{
	uint32_t id = n->items[index].id;
	if (n->bus != NULL) {
		sd_bus_emit_signal(n->bus, NOTIFY_PATH, NOTIFY_IFACE,
			"NotificationClosed", "uu", id, reason);
	}
	size_t rest = n->count - index - 1;
	if (rest > 0) {
		memmove(&n->items[index], &n->items[index + 1],
			rest * sizeof(n->items[0]));
	}
	n->count--;
	n->needs_redraw = true;
}

static bool notify_close_id(struct notify *n, uint32_t id, uint32_t reason)
{
	for (size_t i = 0; i < n->count; i++) {
		if (n->items[i].id == id) {
			notify_close_at(n, i, reason);
			return true;
		}
	}
	return false;
}

static void notify_expire(struct notify *n)
{
	uint64_t now = now_monotonic_usec();
	size_t i = 0;
	while (i < n->count) {
		struct notification *it = &n->items[i];
		long elapsed = (long)((now - it->shown_usec) / 1000U);
		if (it->expire_ms > 0 && elapsed >= it->expire_ms) {
			notify_close_at(n, i, NOTIFY_REASON_EXPIRED);
		} else {
			i++;
		}
	}
}

/* Milliseconds until the next expiry, or -1 if nothing expires. */
static int notify_next_expiry(struct notify *n)
{
	uint64_t now = now_monotonic_usec();
	long best = -1;
	for (size_t i = 0; i < n->count; i++) {
		if (n->items[i].expire_ms <= 0) {
			continue;
		}
		long elapsed = (long)((now - n->items[i].shown_usec) / 1000U);
		long remaining = n->items[i].expire_ms - elapsed;
		if (remaining < 0) {
			remaining = 0;
		}
		if (best < 0 || remaining < best) {
			best = remaining;
		}
	}
	return (int)best;
}

/* ---------------------------------------------------------------- rendering */

static void draw_clipped(const struct klest_canvas *canvas, const char *text,
	uint32_t x, uint32_t y, uint32_t scale, uint32_t color, uint32_t max_w)
{
	uint32_t per = KLEST_FONT_GLYPH * scale;
	uint32_t max_chars = per != 0 ? max_w / per : 0;
	char buf[160];
	size_t i = 0;
	while (text[i] != '\0' && i < max_chars && i < sizeof(buf) - 1) {
		buf[i] = text[i];
		i++;
	}
	buf[i] = '\0';
	klest_font_draw(canvas, buf, x, y, scale, color);
}

static void draw_card(const struct klest_canvas *canvas,
	const struct notification *it, uint32_t card_y)
{
	uint32_t x = NOTIFY_MARGIN;
	uint32_t w = canvas->width > 2U * NOTIFY_MARGIN ?
		canvas->width - 2U * NOTIFY_MARGIN : canvas->width;
	klest_canvas_fill(canvas, x, card_y, w, NOTIFY_CARD_H, NOTIFY_CARD_BG);
	klest_canvas_fill(canvas, x, card_y, 4U, NOTIFY_CARD_H, NOTIFY_ACCENT);

	uint32_t tx = x + NOTIFY_PAD + 4U;
	uint32_t max_w = w > 2U * NOTIFY_PAD + 4U ? w - 2U * NOTIFY_PAD - 4U : 0U;
	draw_clipped(canvas, it->app_name, tx, card_y + 10U, 1U, NOTIFY_DIM, max_w);
	draw_clipped(canvas, it->summary, tx, card_y + 24U, 2U, NOTIFY_FG, max_w);
	draw_clipped(canvas, it->body, tx, card_y + 48U, 1U, NOTIFY_FG, max_w);
}

static bool present_cards(struct notify *n, uint32_t width, uint32_t height)
{
	size_t pixel_count = (size_t)width * (size_t)height;
	uint32_t *pixels = malloc(pixel_count * sizeof(*pixels));
	if (pixels == NULL) {
		fprintf(stderr, "klest-notify: cannot allocate drawing buffer\n");
		return false;
	}
	struct klest_canvas canvas = {
		.pixels = pixels, .width = width, .height = height,
	};
	klest_canvas_clear(&canvas, 0x00000000U); /* transparent */
	for (size_t i = 0; i < n->count; i++) {
		uint32_t card_y = NOTIFY_MARGIN +
			(uint32_t)i * (NOTIFY_CARD_H + NOTIFY_MARGIN);
		if (card_y + NOTIFY_CARD_H > height) {
			break;
		}
		draw_card(&canvas, &n->items[i], card_y);
	}

	struct wl_buffer *buffer = klest_shm_buffer_create_from_data(
		n->layer.shm, width, height, (int32_t)(width * 4U), pixels);
	free(pixels);
	if (buffer == NULL) {
		return false;
	}
	if (!klest_layer_client_present(&n->layer, buffer, width, height)) {
		wl_buffer_destroy(buffer);
		return false;
	}
	struct wl_buffer *old = n->buffer;
	n->buffer = buffer;
	n->mapped_width = width;
	n->mapped_height = height;
	if (old != NULL) {
		wl_buffer_destroy(old);
	}
	return true;
}

static bool notify_redraw(struct notify *n)
{
	n->needs_redraw = false;
	if (n->count == 0) {
		n->mapped_width = 0;
		n->mapped_height = 0;
		return klest_layer_client_unmap(&n->layer);
	}
	uint32_t width = n->layer.width;
	uint32_t height = n->layer.height;
	if (width == 0 || height == 0 || width > (uint32_t)(INT32_MAX / 4)) {
		fprintf(stderr, "klest-notify: invalid surface dimensions\n");
		return false;
	}
	return present_cards(n, width, height);
}

/* ---------------------------------------------------------------- D-Bus */

static int method_get_server_information(sd_bus_message *m, void *userdata,
	sd_bus_error *ret_error)
{
	(void)userdata;
	(void)ret_error;
	return sd_bus_reply_method_return(m, "ssss", "klest-notify", "Klest",
		KLEST_VERSION, "1.2");
}

static int method_get_capabilities(sd_bus_message *m, void *userdata,
	sd_bus_error *ret_error)
{
	struct notify *n = userdata;
	(void)ret_error;
	char *caps[] = {(char *)"body", NULL};
	sd_bus_message *reply = NULL;
	int r = sd_bus_message_new_method_return(m, &reply);
	if (r < 0) {
		return r;
	}
	r = sd_bus_message_append_strv(reply, caps);
	if (r >= 0) {
		r = sd_bus_send(n->bus, reply, NULL);
	}
	sd_bus_message_unref(reply);
	return r;
}

static int method_notify(sd_bus_message *m, void *userdata,
	sd_bus_error *ret_error)
{
	struct notify *n = userdata;
	(void)ret_error;
	const char *app_name;
	const char *app_icon;
	const char *summary;
	const char *body;
	uint32_t replaces_id;
	int32_t expire;

	int r = sd_bus_message_read(m, "susss", &app_name, &replaces_id,
		&app_icon, &summary, &body);
	if (r < 0) {
		return r;
	}
	r = sd_bus_message_skip(m, "as");    /* actions (unsupported in v1) */
	if (r < 0) {
		return r;
	}
	r = sd_bus_message_skip(m, "a{sv}"); /* hints (unused in v1) */
	if (r < 0) {
		return r;
	}
	r = sd_bus_message_read(m, "i", &expire);
	if (r < 0) {
		return r;
	}

	uint32_t id = notify_add(n, replaces_id, app_name, summary, body, expire);
	return sd_bus_reply_method_return(m, "u", id);
}

static int method_close_notification(sd_bus_message *m, void *userdata,
	sd_bus_error *ret_error)
{
	struct notify *n = userdata;
	(void)ret_error;
	uint32_t id;
	int r = sd_bus_message_read(m, "u", &id);
	if (r < 0) {
		return r;
	}
	(void)notify_close_id(n, id, NOTIFY_REASON_CLOSED);
	return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable notify_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("GetServerInformation", "", "ssss",
		method_get_server_information, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetCapabilities", "", "as",
		method_get_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Notify", "susssasa{sv}i", "u",
		method_notify, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CloseNotification", "u", "",
		method_close_notification, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
	SD_BUS_SIGNAL("ActionInvoked", "us", 0),
	SD_BUS_VTABLE_END,
};

static bool notify_bus_connect(struct notify *n)
{
	int r = sd_bus_open_user(&n->bus);
	if (r < 0) {
		fprintf(stderr, "klest-notify: cannot connect to the session bus: "
			"%s\n", strerror(-r));
		return false;
	}
	r = sd_bus_add_object_vtable(n->bus, &n->slot, NOTIFY_PATH, NOTIFY_IFACE,
		notify_vtable, n);
	if (r < 0) {
		fprintf(stderr, "klest-notify: cannot register object: %s\n",
			strerror(-r));
		return false;
	}
	r = sd_bus_request_name(n->bus, NOTIFY_NAME, 0);
	if (r < 0) {
		fprintf(stderr, "klest-notify: cannot acquire %s: %s "
			"(another notification daemon running?)\n",
			NOTIFY_NAME, strerror(-r));
		return false;
	}
	return true;
}

/* ---------------------------------------------------------------- loop */

/* Drains all pending bus work. Returns false on a fatal bus error. */
static bool drain_bus(struct notify *n)
{
	for (;;) {
		int r = sd_bus_process(n->bus, NULL);
		if (r < 0) {
			fprintf(stderr, "klest-notify: bus error: %s\n", strerror(-r));
			return false;
		}
		if (r == 0) {
			return true;
		}
	}
}

/* Combines the next notification expiry with the bus's own timeout. */
static int poll_timeout(struct notify *n)
{
	int expiry = notify_next_expiry(n);
	uint64_t until = UINT64_MAX;
	(void)sd_bus_get_timeout(n->bus, &until);
	long bus_ms = -1;
	if (until != UINT64_MAX) {
		uint64_t now = now_monotonic_usec();
		bus_ms = until <= now ? 0 : (long)((until - now) / 1000U);
	}
	if (expiry < 0) {
		return (int)bus_ms;
	}
	if (bus_ms < 0) {
		return expiry;
	}
	return expiry < bus_ms ? expiry : (int)bus_ms;
}

static bool notify_run(struct notify *n)
{
	int wl_fd = wl_display_get_fd(n->layer.display);
	int bus_fd = sd_bus_get_fd(n->bus);
	if (bus_fd < 0) {
		fprintf(stderr, "klest-notify: cannot get bus descriptor\n");
		return false;
	}

	while (n->running && !n->layer.closed) {
		if (!drain_bus(n)) {
			return false;
		}
		notify_expire(n);
		if (n->count > 0 && (n->layer.width != n->mapped_width ||
				n->layer.height != n->mapped_height)) {
			n->needs_redraw = true; /* output was reconfigured */
		}
		if (n->needs_redraw && !notify_redraw(n)) {
			return false;
		}
		if (wl_display_flush(n->layer.display) < 0 && errno != EAGAIN) {
			perror("klest-notify: wl_display_flush");
			return false;
		}

		struct pollfd fds[2] = {
			{.fd = wl_fd, .events = POLLIN},
			{.fd = bus_fd, .events = (short)sd_bus_get_events(n->bus)},
		};
		int r;
		do {
			r = poll(fds, 2, poll_timeout(n));
		} while (r < 0 && errno == EINTR);
		if (r < 0) {
			perror("klest-notify: poll");
			return false;
		}
		if ((fds[0].revents & POLLIN) &&
				wl_display_dispatch(n->layer.display) < 0) {
			return true; /* compositor went away */
		}
	}
	return true;
}

static void notify_finish(struct notify *n)
{
	if (n->buffer != NULL) {
		wl_buffer_destroy(n->buffer);
	}
	klest_layer_client_finish(&n->layer);
	if (n->slot != NULL) {
		sd_bus_slot_unref(n->slot);
	}
	if (n->bus != NULL) {
		sd_bus_unref(n->bus);
	}
}

static bool notify_create_surface(struct notify *n)
{
	/* Full-height column on the right edge: cards render at the top, the rest
	 * stays transparent and click-through (empty input region). */
	return klest_layer_client_create_surface(&n->layer,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "klest-notify",
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
		NOTIFY_WIDTH, 0, 0, false, false) &&
		klest_layer_client_wait_configure(&n->layer);
}

static int usage(const char *program)
{
	fprintf(stderr, "Usage: %s [--once]\n", program);
	return 2;
}

int main(int argc, char *argv[])
{
	bool once = false;
	if (argc == 2 && strcmp(argv[1], "--once") == 0) {
		once = true;
	} else if (argc != 1) {
		return usage(argv[0]);
	}

	struct notify n = {.next_id = 1, .running = true};
	if (!klest_layer_client_connect(&n.layer) || !notify_create_surface(&n)) {
		notify_finish(&n);
		return 1;
	}

	bool ok;
	if (once) {
		/* Self-test: render one synthetic card once, then unmap and exit. */
		notify_add(&n, 0, "klest", "Test notification",
			"Rendering self-test", 0);
		ok = notify_redraw(&n) && klest_layer_client_unmap(&n.layer);
	} else if (!notify_bus_connect(&n)) {
		ok = false;
	} else {
		ok = notify_run(&n);
	}

	notify_finish(&n);
	return ok ? 0 : 1;
}
