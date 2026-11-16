/* Klest — launcher command model (see klest-launcher-filter.h). */

#define _POSIX_C_SOURCE 200809L

#include "klest-launcher-filter.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* --- command list --------------------------------------------------------- */

static int compare_names(const void *a, const void *b)
{
	const char *const *lhs = a;
	const char *const *rhs = b;
	return strcmp(*lhs, *rhs);
}

void klest_commands_push(struct command_list *list, const char *name)
{
	if (list->count == list->capacity) {
		size_t capacity = list->capacity == 0 ? 256 : list->capacity * 2;
		char **items = realloc(list->items, capacity * sizeof(*items));
		if (items == NULL) {
			return;
		}
		list->items = items;
		list->capacity = capacity;
	}
	char *copy = strdup(name);
	if (copy != NULL) {
		list->items[list->count++] = copy;
	}
}

void klest_commands_scan_dir(struct command_list *list, const char *dir)
{
	DIR *handle = opendir(dir);
	if (handle == NULL) {
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(handle)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		char path[4096];
		int n = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
		if (n < 0 || (size_t)n >= sizeof(path)) {
			continue;
		}
		struct stat st;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
				(st.st_mode & S_IXUSR) != 0) {
			klest_commands_push(list, entry->d_name);
		}
	}
	closedir(handle);
}

void klest_commands_scan_path(struct command_list *list)
{
	const char *path_env = getenv("PATH");
	if (path_env == NULL || path_env[0] == '\0') {
		path_env = "/usr/bin:/bin";
	}
	char *copy = strdup(path_env);
	if (copy == NULL) {
		return;
	}
	char *save = NULL;
	for (char *token = strtok_r(copy, ":", &save); token != NULL;
			token = strtok_r(NULL, ":", &save)) {
		const char *dir = token;
		if (dir[0] != '\0') {
			klest_commands_scan_dir(list, dir);
		}
	}
	free(copy);
}

void klest_commands_finalize(struct command_list *list)
{
	if (list->count == 0) {
		return;
	}
	qsort(list->items, list->count, sizeof(*list->items), compare_names);
	size_t write = 1;
	for (size_t read = 1; read < list->count; read++) {
		if (strcmp(list->items[read], list->items[write - 1]) == 0) {
			free(list->items[read]);
		} else {
			list->items[write++] = list->items[read];
		}
	}
	list->count = write;
}

void klest_commands_free(struct command_list *list)
{
	for (size_t i = 0; i < list->count; i++) {
		free(list->items[i]);
	}
	free(list->items);
	list->items = NULL;
	list->count = 0;
	list->capacity = 0;
}

/* --- filtering and navigation -------------------------------------------- */

bool klest_contains_fold(const char *haystack, const char *needle)
{
	size_t needle_len = strlen(needle);
	if (needle_len == 0) {
		return true;
	}
	for (const char *h = haystack; *h != '\0'; h++) {
		size_t i = 0;
		while (i < needle_len && h[i] != '\0' &&
				tolower((unsigned char)h[i]) ==
				tolower((unsigned char)needle[i])) {
			i++;
		}
		if (i == needle_len) {
			return true;
		}
	}
	return false;
}

void klest_launcher_filter(struct launcher_model *model)
{
	model->match_count = 0;
	for (size_t i = 0; i < model->commands.count; i++) {
		if (klest_contains_fold(model->commands.items[i], model->query)) {
			model->matches[model->match_count++] = model->commands.items[i];
		}
	}
	model->selected = 0;
	model->first_visible = 0;
}

void klest_launcher_choose(struct launcher_model *model)
{
	const char *command = NULL;
	if (model->match_count > 0) {
		command = model->matches[model->selected];
	} else if (model->query_len > 0) {
		command = model->query;
	}
	if (command != NULL) {
		model->launch = strdup(command);
	}
}

void klest_launcher_append(struct launcher_model *model, char character)
{
	if (model->query_len + 1 >= sizeof(model->query)) {
		return;
	}
	model->query[model->query_len++] = character;
	model->query[model->query_len] = '\0';
	klest_launcher_filter(model);
}

size_t klest_launcher_scroll(struct launcher_model *model, size_t visible)
{
	if (model->selected < model->first_visible) {
		model->first_visible = model->selected;
	} else if (model->selected >= model->first_visible + visible) {
		model->first_visible = model->selected - visible + 1;
	}
	return model->first_visible;
}

bool klest_launcher_navigate(struct launcher_model *model, xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_Escape:
		model->running = false;
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		klest_launcher_choose(model);
		model->running = false;
		return true;
	case XKB_KEY_BackSpace:
		if (model->query_len > 0) {
			model->query[--model->query_len] = '\0';
			klest_launcher_filter(model);
		}
		return true;
	case XKB_KEY_Up:
		if (model->selected > 0) {
			model->selected--;
		}
		return true;
	case XKB_KEY_Down:
	case XKB_KEY_Tab:
		if (model->selected + 1 < model->match_count) {
			model->selected++;
		}
		return true;
	default:
		return false;
	}
}
