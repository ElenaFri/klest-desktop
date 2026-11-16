/*
 * Klest — panel layout model tests.
 *
 * These pin the panel's placement maths and hit-testing: which workspace or
 * window box a click lands on, how long titles are shortened, and where the
 * window strip is clipped. These decisions drive what the user can click, so
 * off-by-one errors here are silently wrong rather than loud crashes.
 */

#include "components/klest-panel-layout.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The indicator count is clamped so a hostile or buggy compositor cannot make
 * the panel draw an unbounded number of boxes. */
static void test_visible_workspaces_is_capped(void)
{
	assert(klest_panel_visible_workspaces(0) == 0);
	assert(klest_panel_visible_workspaces(4) == 4);
	assert(klest_panel_visible_workspaces(KLEST_PANEL_MAX_WORKSPACES) ==
		KLEST_PANEL_MAX_WORKSPACES);
	assert(klest_panel_visible_workspaces(KLEST_PANEL_MAX_WORKSPACES + 5) ==
		KLEST_PANEL_MAX_WORKSPACES);
}

/* Active wins over every other state; a minimized but inactive window gets the
 * dimmed colour; everything else is plain. */
static void test_window_box_color(void)
{
	struct panel_window active = {.active = true, .minimized = true};
	struct panel_window minimized = {.active = false, .minimized = true};
	struct panel_window plain = {0};

	assert(klest_panel_window_box_color(&active) == 0xff5c8fe8U);
	assert(klest_panel_window_box_color(&minimized) == 0xff20222cU);
	assert(klest_panel_window_box_color(&plain) == 0xff2a2d3eU);
}

static void test_truncate_title(void)
{
	char label[KLEST_WINDOW_LABEL_CHARS + 1U];

	/* Shorter than the budget: copied verbatim. */
	klest_panel_truncate_title(label, "foot");
	assert(strcmp(label, "foot") == 0);

	/* Empty title stays empty. */
	klest_panel_truncate_title(label, "");
	assert(label[0] == '\0');

	/* Exactly the budget: no ellipsis, nothing dropped. */
	klest_panel_truncate_title(label, "exactly14chars");
	assert(strlen("exactly14chars") == KLEST_WINDOW_LABEL_CHARS);
	assert(strcmp(label, "exactly14chars") == 0);

	/* One over the budget: last three chars become an ellipsis. */
	klest_panel_truncate_title(label, "fifteen_chars!!");
	assert(strlen(label) == KLEST_WINDOW_LABEL_CHARS);
	assert(strcmp(label, "fifteen_cha...") == 0);
}

static void test_window_by_id(void)
{
	struct panel_window c = {.id = 3, .next = NULL};
	struct panel_window b = {.id = 2, .next = &c};
	struct panel_window a = {.id = 1, .next = &b};

	assert(klest_panel_window_by_id(&a, 1) == &a);
	assert(klest_panel_window_by_id(&a, 3) == &c);
	assert(klest_panel_window_by_id(&a, 99) == NULL);
	assert(klest_panel_window_by_id(NULL, 1) == NULL);
}

/* A wide clock_x means the whole strip is visible. */
#define WIDE_CLOCK 1920U

static void test_window_at_hits_and_gaps(void)
{
	struct panel_window second = {.id = 2, .workspace = 0, .next = NULL};
	struct panel_window first = {.id = 1, .workspace = 0, .next = &second};

	uint32_t box0 = KLEST_WINDOW_ORIGIN_X;
	uint32_t box1 = KLEST_WINDOW_ORIGIN_X + KLEST_WINDOW_BOX_WIDTH;

	/* Left edge and interior of the first box resolve to the first window. */
	assert(klest_panel_window_at(&first, 0, WIDE_CLOCK, box0) == &first);
	assert(klest_panel_window_at(&first, 0, WIDE_CLOCK, box0 + 20) == &first);

	/* The 4px gutter after each box is dead space, not a hit. */
	double gutter = box0 + KLEST_WINDOW_BOX_WIDTH - 2;
	assert(klest_panel_window_at(&first, 0, WIDE_CLOCK, gutter) == NULL);

	/* The second box resolves to the second window. */
	assert(klest_panel_window_at(&first, 0, WIDE_CLOCK, box1 + 10) == &second);

	/* Far to the left of the strip: nothing. */
	assert(klest_panel_window_at(&first, 0, WIDE_CLOCK, 0) == NULL);
}

/* Windows on another workspace are skipped without consuming a slot, so the
 * first same-workspace window still sits in the first box. */
static void test_window_at_skips_other_workspaces(void)
{
	struct panel_window here = {.id = 2, .workspace = 1, .next = NULL};
	struct panel_window elsewhere = {.id = 1, .workspace = 0, .next = &here};

	const struct panel_window *hit = klest_panel_window_at(&elsewhere, 1,
		WIDE_CLOCK, KLEST_WINDOW_ORIGIN_X + 10);
	assert(hit == &here);
}

/* A narrow panel clips the strip: even a pointer over the first box misses
 * because the box would overrun the clock. */
static void test_window_at_respects_clipping(void)
{
	struct panel_window only = {.id = 1, .workspace = 0, .next = NULL};
	uint32_t tight_clock = KLEST_WINDOW_ORIGIN_X + 10;
	assert(klest_panel_window_at(&only, 0, tight_clock,
		KLEST_WINDOW_ORIGIN_X + 1) == NULL);
}

static void test_workspace_at(void)
{
	/* Left of the first indicator: no workspace. */
	assert(klest_panel_workspace_at(4, 0) == -1);
	assert(klest_panel_workspace_at(4, KLEST_WS_ORIGIN_X - 1) == -1);

	/* The origin lands on indicator 0; one box over lands on indicator 1. */
	assert(klest_panel_workspace_at(4, KLEST_WS_ORIGIN_X) == 0);
	assert(klest_panel_workspace_at(4,
		KLEST_WS_ORIGIN_X + KLEST_WS_BOX_WIDTH) == 1);

	/* Past the last existing workspace: no hit even though pixels remain. */
	assert(klest_panel_workspace_at(4,
		KLEST_WS_ORIGIN_X + 4 * KLEST_WS_BOX_WIDTH) == -1);
}

/* A click on an indicator switches workspace regardless of window support. */
static void test_click_at_workspace(void)
{
	struct klest_panel_click click = klest_panel_click_at(4, 0, NULL,
		WIDE_CLOCK, KLEST_WS_ORIGIN_X + KLEST_WS_BOX_WIDTH, true);
	assert(click.action == KLEST_PANEL_ACTION_ACTIVATE_WORKSPACE);
	assert(click.target == 1);
}

/* A click on a window box toggles that window: minimize the active one,
 * activate (restore) any other state. Only two workspaces exist, so the far
 * window-strip coordinate is not mistaken for an indicator. */
static void test_click_at_window_toggles(void)
{
	struct panel_window win = {.id = 7, .workspace = 0, .active = true,
		.minimized = false, .next = NULL};
	double x = KLEST_WINDOW_ORIGIN_X + 10;

	struct klest_panel_click click = klest_panel_click_at(2, 0, &win,
		WIDE_CLOCK, x, true);
	assert(click.action == KLEST_PANEL_ACTION_MINIMIZE_WINDOW);
	assert(click.target == 7);

	win.active = false;
	click = klest_panel_click_at(2, 0, &win, WIDE_CLOCK, x, true);
	assert(click.action == KLEST_PANEL_ACTION_ACTIVATE_WINDOW);
	assert(click.target == 7);

	/* Active but minimized means the user wants it back, not minimized again. */
	win.active = true;
	win.minimized = true;
	click = klest_panel_click_at(2, 0, &win, WIDE_CLOCK, x, true);
	assert(click.action == KLEST_PANEL_ACTION_ACTIVATE_WINDOW);
}

/* An older (v1) workspace manager cannot address windows, so a window-strip
 * click is inert rather than mis-dispatched. */
static void test_click_at_without_window_support(void)
{
	struct panel_window win = {.id = 7, .workspace = 0, .active = false,
		.next = NULL};
	struct klest_panel_click click = klest_panel_click_at(2, 0, &win,
		WIDE_CLOCK, KLEST_WINDOW_ORIGIN_X + 10, false);
	assert(click.action == KLEST_PANEL_ACTION_NONE);
}

/* Empty space (no indicator, no window box) resolves to no action. */
static void test_click_at_empty(void)
{
	struct klest_panel_click click = klest_panel_click_at(2, 0, NULL,
		WIDE_CLOCK, KLEST_WINDOW_ORIGIN_X + 10, true);
	assert(click.action == KLEST_PANEL_ACTION_NONE);
}

static struct panel_window *make_window(uint32_t id, struct panel_window *next)
{
	struct panel_window *window = calloc(1, sizeof(*window));
	assert(window != NULL);
	window->id = id;
	window->next = next;
	return window;
}

/* Removal unlinks (and frees) the right node at the head, middle and tail, and
 * reports whether anything was removed. */
static void test_window_remove(void)
{
	struct panel_window *head =
		make_window(1, make_window(2, make_window(3, NULL)));
	bool removed = true;

	head = klest_panel_window_remove(head, 99, &removed);
	assert(!removed);
	assert(head->id == 1 && head->next->id == 2 && head->next->next->id == 3);

	head = klest_panel_window_remove(head, 2, &removed);
	assert(removed);
	assert(head->id == 1 && head->next->id == 3 && head->next->next == NULL);

	head = klest_panel_window_remove(head, 1, &removed);
	assert(removed && head->id == 3 && head->next == NULL);

	head = klest_panel_window_remove(head, 3, &removed);
	assert(removed && head == NULL);
}

int main(void)
{
	test_visible_workspaces_is_capped();
	test_window_box_color();
	test_truncate_title();
	test_window_by_id();
	test_window_at_hits_and_gaps();
	test_window_at_skips_other_workspaces();
	test_window_at_respects_clipping();
	test_workspace_at();
	test_click_at_workspace();
	test_click_at_window_toggles();
	test_click_at_without_window_support();
	test_click_at_empty();
	test_window_remove();
	return 0;
}
