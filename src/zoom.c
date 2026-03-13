#include "shoshin.h"

int
zoom_tick(void *data)
{
	float cur = swc_get_zoom();
	float diff = zoom_target - cur;

	(void)data;

	if(diff > -0.01f && diff < 0.01f) {
		swc_set_zoom(zoom_target);
		return 0;
	}

	float step = diff / 4.0f;
	if(step > 0 && step < 0.01f) step = 0.01f;
	if(step < 0 && step > -0.01f) step = -0.01f;

	swc_set_zoom(cur + step);
	wl_event_source_timer_update(zoom_timer, cfg.timerms);
	return 0;
}
