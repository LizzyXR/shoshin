#include "shoshin.h"

void
update_mode_cursor(void)
{
	if(chord_mode_cur == MODE_KILL)
		swc_set_cursor(SWC_CURSOR_SIGHT);
	else if(chord_mode_cur == MODE_SCROLL)
		swc_set_cursor(scroll_cursor_dir < 0 ? SWC_CURSOR_UP : SWC_CURSOR_DOWN);
	else if(sel_active)
		swc_set_cursor(SWC_CURSOR_CROSS);
	else if(chord_mode_cur == MODE_MOVE || chord_mode_cur == MODE_RESIZE)
		swc_set_cursor(SWC_CURSOR_BOX);
	else
		swc_set_cursor(SWC_CURSOR_DEFAULT);
}

void
stop_select(void)
{
	if(sel_timer) {
		wl_event_source_remove(sel_timer);
		sel_timer = NULL;
	}
	sel_active = false;
	swc_overlay_clear();
	update_mode_cursor();
}

int
select_tick(void *data)
{
	int32_t x, y;
	(void)data;

	if(!sel_active) return 0;

	if(cursor_position(&x, &y)) {
		sel_cur_x = x;
		sel_cur_y = y;
		swc_overlay_set_box(sel_start_x, sel_start_y, x, y, cfg.select_box_color, cfg.select_box_border);
	}

	wl_event_source_timer_update(sel_timer, cfg.timerms);
	return 0;
}

void
spawn_term_select(const struct swc_rectangle *geometry)
{
	pid_t pid;
	spawn_pending = true;
	spawn_geometry = *geometry;
	pid = fork();
	if(pid == 0) {
		execlp(cfg.term, cfg.term, cfg.term_flag, cfg.select_term_app_id, NULL);
		_exit(127);
	}
}
