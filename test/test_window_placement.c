#include "compositor/placement.h"

#include <assert.h>

static void test_centers_below_reserved_area(void)
{
	const struct wlr_box usable_area = {
		.x = 0,
		.y = 32,
		.width = 1280,
		.height = 688,
	};
	const struct wlr_box geometry = {
		.x = -4,
		.y = -12,
		.width = 800,
		.height = 600,
	};
	int scene_x;
	int scene_y;

	assert(klest_window_initial_position(&usable_area, &geometry,
		&scene_x, &scene_y));
	assert(scene_x + geometry.x == 240);
	assert(scene_y + geometry.y == 32);
}

static void test_left_aligns_oversized_window_below_reserved_area(void)
{
	const struct wlr_box usable_area = {
		.x = 12,
		.y = 48,
		.width = 900,
		.height = 600,
	};
	const struct wlr_box geometry = {
		.x = 6,
		.y = -8,
		.width = 1000,
		.height = 800,
	};
	int scene_x;
	int scene_y;

	assert(klest_window_initial_position(&usable_area, &geometry,
		&scene_x, &scene_y));
	assert(scene_x + geometry.x == usable_area.x);
	assert(scene_y + geometry.y == usable_area.y);
}

int main(void)
{
	test_centers_below_reserved_area();
	test_left_aligns_oversized_window_below_reserved_area();
	return 0;
}
