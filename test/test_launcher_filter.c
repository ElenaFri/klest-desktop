/*
 * Klest — launcher model tests.
 *
 * These pin the behaviour a user actually feels: typing narrows the list and
 * resets the cursor, arrows/Tab wrap within bounds, Enter launches the
 * highlighted command (or a literal typed command when nothing matches),
 * Escape leaves without launching, and the visible window follows the cursor.
 * The $PATH scan is checked against a real temp directory so the executable
 * filter (skip non-executables and dotfiles) is exercised for real.
 */

#define _POSIX_C_SOURCE 200809L

#include "components/klest-launcher-filter.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void model_init(struct launcher_model *m, const char *const *names,
	size_t count)
{
	memset(m, 0, sizeof(*m));
	for (size_t i = 0; i < count; i++) {
		klest_commands_push(&m->commands, names[i]);
	}
	size_t slots = m->commands.count > 0 ? m->commands.count : 1;
	m->matches = malloc(slots * sizeof(*m->matches));
	assert(m->matches != NULL);
	m->running = true;
	klest_launcher_filter(m);
}

static void model_free(struct launcher_model *m)
{
	free(m->launch);
	klest_commands_free(&m->commands);
	free(m->matches);
}

static void test_contains_fold(void)
{
	assert(klest_contains_fold("firefox", ""));          /* empty matches all */
	assert(klest_contains_fold("Firefox", "fire"));      /* fold, prefix */
	assert(klest_contains_fold("firefox", "FOX"));       /* fold, suffix */
	assert(klest_contains_fold("gnome-terminal", "term")); /* interior */
	assert(!klest_contains_fold("firefox", "chrome"));
	assert(!klest_contains_fold("ab", "abc"));           /* needle too long */
	assert(!klest_contains_fold("", "x"));               /* empty haystack */
}

static void test_filter_narrows_and_resets(void)
{
	const char *names[] = {"alpha", "beta", "alphabet", "gamma"};
	struct launcher_model m;
	model_init(&m, names, 4);
	assert(m.match_count == 4); /* empty query matches everything */

	/* A stale cursor must snap back to the top when the results change. */
	m.selected = 2;
	m.first_visible = 1;
	strcpy(m.query, "alph");
	m.query_len = 4;
	klest_launcher_filter(&m);
	assert(m.match_count == 2);
	assert(strcmp(m.matches[0], "alpha") == 0);
	assert(strcmp(m.matches[1], "alphabet") == 0);
	assert(m.selected == 0);
	assert(m.first_visible == 0);
	model_free(&m);
}

static void test_append_filters_and_is_bounded(void)
{
	const char *names[] = {"foo", "foobar", "baz"};
	struct launcher_model m;
	model_init(&m, names, 3);

	klest_launcher_append(&m, 'f');
	klest_launcher_append(&m, 'o');
	assert(m.query_len == 2 && strcmp(m.query, "fo") == 0);
	assert(m.match_count == 2); /* foo, foobar */

	/* Overflowing the query buffer drops extra characters and stays valid. */
	for (size_t i = 0; i < KLEST_LAUNCHER_QUERY_MAX + 10; i++) {
		klest_launcher_append(&m, 'x');
	}
	assert(m.query_len == KLEST_LAUNCHER_QUERY_MAX - 1);
	assert(m.query[m.query_len] == '\0');
	model_free(&m);
}

static void test_choose_prefers_match_then_query(void)
{
	const char *names[] = {"vim", "vimdiff"};
	struct launcher_model m;
	model_init(&m, names, 2);

	/* A highlighted match is what runs. */
	m.selected = 1;
	klest_launcher_choose(&m);
	assert(m.launch != NULL && strcmp(m.launch, "vimdiff") == 0);
	free(m.launch);
	m.launch = NULL;

	/* No match, but a typed command: run it verbatim (e.g. with arguments). */
	strcpy(m.query, "htop");
	m.query_len = 4;
	m.match_count = 0;
	klest_launcher_choose(&m);
	assert(m.launch != NULL && strcmp(m.launch, "htop") == 0);
	free(m.launch);
	m.launch = NULL;

	/* Nothing matched and nothing typed: launch nothing. */
	m.query[0] = '\0';
	m.query_len = 0;
	m.match_count = 0;
	klest_launcher_choose(&m);
	assert(m.launch == NULL);
	model_free(&m);
}

static void test_navigate_cursor_and_editing(void)
{
	const char *names[] = {"a", "b", "c"};
	struct launcher_model m;
	model_init(&m, names, 3);

	/* Down/Tab advance and clamp at the last match. */
	assert(klest_launcher_navigate(&m, XKB_KEY_Down) && m.selected == 1);
	assert(klest_launcher_navigate(&m, XKB_KEY_Tab) && m.selected == 2);
	assert(klest_launcher_navigate(&m, XKB_KEY_Down) && m.selected == 2);

	/* Up walks back and clamps at zero. */
	assert(klest_launcher_navigate(&m, XKB_KEY_Up) && m.selected == 1);
	m.selected = 0;
	assert(klest_launcher_navigate(&m, XKB_KEY_Up) && m.selected == 0);

	/* BackSpace trims and re-filters; on an empty query it is a safe no-op. */
	strcpy(m.query, "ab");
	m.query_len = 2;
	klest_launcher_filter(&m);
	assert(klest_launcher_navigate(&m, XKB_KEY_BackSpace));
	assert(m.query_len == 1 && strcmp(m.query, "a") == 0);
	m.query[0] = '\0';
	m.query_len = 0;
	assert(klest_launcher_navigate(&m, XKB_KEY_BackSpace) && m.query_len == 0);

	/* Escape leaves the loop without launching anything. */
	assert(klest_launcher_navigate(&m, XKB_KEY_Escape));
	assert(!m.running && m.launch == NULL);

	/* Return launches the highlighted command and leaves the loop. */
	m.running = true;
	klest_launcher_filter(&m);
	m.selected = 1;
	assert(klest_launcher_navigate(&m, XKB_KEY_Return));
	assert(!m.running);
	assert(m.launch != NULL && strcmp(m.launch, "b") == 0);

	/* Printable keys are not navigation: the caller handles them. */
	assert(!klest_launcher_navigate(&m, XKB_KEY_x));
	model_free(&m);
}

static void test_scroll_keeps_selection_visible(void)
{
	struct launcher_model m;
	memset(&m, 0, sizeof(m));
	m.match_count = 100;

	/* Cursor past the window scrolls so it lands on the last visible row. */
	m.selected = 20;
	assert(klest_launcher_scroll(&m, 10) == 11);

	/* Cursor above the window scrolls up onto it. */
	m.selected = 5;
	assert(klest_launcher_scroll(&m, 10) == 5);

	/* Cursor already visible: the window does not move. */
	m.selected = 8;
	m.first_visible = 5;
	assert(klest_launcher_scroll(&m, 10) == 5);
}

static void make_file(const char *dir, const char *name, mode_t mode)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	int fd = open(path, O_CREAT | O_WRONLY, mode);
	assert(fd >= 0);
	close(fd);
	assert(chmod(path, mode) == 0);
}

static void remove_file(const char *dir, const char *name)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	unlink(path);
}

static void test_scan_dir_picks_only_visible_executables(void)
{
	char dir[] = "/tmp/klest-scan-XXXXXX";
	assert(mkdtemp(dir) != NULL);
	make_file(dir, "runme", 0755);   /* executable: included */
	make_file(dir, "data", 0644);    /* not executable: skipped */
	make_file(dir, ".hidden", 0755); /* dotfile: skipped */

	struct command_list list = {0};
	klest_commands_scan_dir(&list, dir);
	assert(list.count == 1);
	assert(strcmp(list.items[0], "runme") == 0);
	klest_commands_free(&list);

	remove_file(dir, "runme");
	remove_file(dir, "data");
	remove_file(dir, ".hidden");
	rmdir(dir);

	/* A missing directory is ignored rather than failing. */
	struct command_list missing = {0};
	klest_commands_scan_dir(&missing, "/klest/no/such/dir");
	assert(missing.count == 0);
	klest_commands_free(&missing);
}

static void test_finalize_sorts_and_dedupes(void)
{
	struct command_list list = {0};
	klest_commands_push(&list, "gamma");
	klest_commands_push(&list, "alpha");
	klest_commands_push(&list, "gamma");
	klest_commands_push(&list, "beta");
	klest_commands_push(&list, "alpha");
	klest_commands_finalize(&list);
	assert(list.count == 3);
	assert(strcmp(list.items[0], "alpha") == 0);
	assert(strcmp(list.items[1], "beta") == 0);
	assert(strcmp(list.items[2], "gamma") == 0);
	klest_commands_free(&list);

	struct command_list empty = {0};
	klest_commands_finalize(&empty); /* no-op, must not crash */
	assert(empty.count == 0);
	klest_commands_free(&empty);
}

static void test_scan_path_reads_env(void)
{
	char d1[] = "/tmp/klest-path1-XXXXXX";
	char d2[] = "/tmp/klest-path2-XXXXXX";
	assert(mkdtemp(d1) != NULL);
	assert(mkdtemp(d2) != NULL);
	make_file(d1, "one", 0755);
	make_file(d2, "two", 0755);

	char combined[8300];
	/* A trailing empty entry must be tolerated (":"-separated). */
	snprintf(combined, sizeof(combined), "%s:%s:", d1, d2);
	setenv("PATH", combined, 1);

	struct command_list list = {0};
	klest_commands_scan_path(&list);
	klest_commands_finalize(&list);
	assert(list.count == 2);
	assert(strcmp(list.items[0], "one") == 0);
	assert(strcmp(list.items[1], "two") == 0);
	klest_commands_free(&list);

	remove_file(d1, "one");
	remove_file(d2, "two");
	rmdir(d1);
	rmdir(d2);
}

int main(void)
{
	test_contains_fold();
	test_filter_narrows_and_resets();
	test_append_filters_and_is_bounded();
	test_choose_prefers_match_then_query();
	test_navigate_cursor_and_editing();
	test_scroll_keeps_selection_visible();
	test_scan_dir_picks_only_visible_executables();
	test_finalize_sorts_and_dedupes();
	test_scan_path_reads_env();
	return 0;
}
