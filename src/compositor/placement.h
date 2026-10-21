/* Initial placement for newly mapped toplevel windows. */
#ifndef KLEST_COMPOSITOR_PLACEMENT_H
#define KLEST_COMPOSITOR_PLACEMENT_H

#include <stdbool.h>

#include <wlr/util/box.h>

/* Return a scene-root position that puts the window geometry at the top of
 * the usable area and centers it horizontally when it fits. */
bool klest_window_initial_position(const struct wlr_box *usable_area,
	const struct wlr_box *geometry, int *scene_x, int *scene_y);

#endif /* KLEST_COMPOSITOR_PLACEMENT_H */
