#include "placement.h"

bool klest_window_initial_position(const struct wlr_box *usable_area,
	const struct wlr_box *geometry, int *scene_x, int *scene_y)
{
	if (usable_area == NULL || geometry == NULL || scene_x == NULL ||
			scene_y == NULL || usable_area->width < 0 ||
			geometry->width < 0) {
		return false;
	}

	int geometry_x = usable_area->x;
	if (geometry->width < usable_area->width) {
		geometry_x += (usable_area->width - geometry->width) / 2;
	}
	*scene_x = geometry_x - geometry->x;
	*scene_y = usable_area->y - geometry->y;
	return true;
}
