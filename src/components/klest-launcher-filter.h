/*
 * Klest — launcher command model (pure filtering, navigation, selection).
 *
 * The launcher's list building, query filtering and keyboard navigation are
 * kept free of Wayland, rendering and xkb *state* so they can be unit-tested
 * directly. Only the keysym constants are borrowed from xkbcommon.
 */
#ifndef KLEST_LAUNCHER_FILTER_H
#define KLEST_LAUNCHER_FILTER_H

#include <stdbool.h>
#include <stddef.h>

#include <xkbcommon/xkbcommon.h>

#define KLEST_LAUNCHER_QUERY_MAX 128U

struct command_list {
	char **items;
	size_t count;
	size_t capacity;
};

struct launcher_model {
	struct command_list commands;
	const char **matches;
	size_t match_count;
	size_t selected;
	size_t first_visible;
	char query[KLEST_LAUNCHER_QUERY_MAX];
	size_t query_len;
	char *launch; /* command to run after the loop, or NULL */
	bool running;
};

/* --- command list --------------------------------------------------------- */

/* Appends a copy of `name`; silently drops it on allocation failure. */
void klest_commands_push(struct command_list *list, const char *name);

/* Adds every executable regular file directly inside `dir`. */
void klest_commands_scan_dir(struct command_list *list, const char *dir);

/* Scans every directory in $PATH (falling back to /usr/bin:/bin). */
void klest_commands_scan_path(struct command_list *list);

/* Sorts and removes duplicate names in place. */
void klest_commands_finalize(struct command_list *list);

/* Frees the list contents and resets it to empty. */
void klest_commands_free(struct command_list *list);

/* --- filtering and navigation -------------------------------------------- */

/* Case-insensitive substring test; an empty needle matches everything. */
bool klest_contains_fold(const char *haystack, const char *needle);

/* Recomputes `matches` from `commands` against `query`, resetting selection.
 * `matches` must hold at least `commands.count` entries. */
void klest_launcher_filter(struct launcher_model *model);

/* Records the command to launch: the selected match, or the raw query when
 * there are no matches, or nothing when both are empty. */
void klest_launcher_choose(struct launcher_model *model);

/* Appends a printable character to the query (bounded) and re-filters. */
void klest_launcher_append(struct launcher_model *model, char character);

/* Keeps the selected row inside a window of `visible` rows, adjusting the
 * first visible index. Returns the (possibly updated) first visible index. */
size_t klest_launcher_scroll(struct launcher_model *model, size_t visible);

/* Handles a navigation/editing keysym. Returns true if it was consumed. */
bool klest_launcher_navigate(struct launcher_model *model, xkb_keysym_t sym);

#endif /* KLEST_LAUNCHER_FILTER_H */
