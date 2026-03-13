#include "shoshin.h"

void
scroll_stop(void)
{
	scroll_px = 0;
	scroll_px_x = 0;
	scroll_auto = false;

	if(scroll_drag_timer) {
		wl_event_source_remove(scroll_drag_timer);
		scroll_drag_timer = NULL;
	}
}

int
scroll_tick(void *data)
{
	struct window *w, *tmp;
	struct swc_rectangle g;
	int32_t step, step_x;

	(void)data;

	if(!scroll_timer) return 0;

	if((chord_mode_cur != MODE_SCROLL && !scroll_auto && chord_mode_cur != MODE_MOVE) || (scroll_px == 0 && scroll_px_x == 0)) {
		scroll_stop();
		return 0;
	}

	step = scroll_px / cfg.scrollease;
	if(step == 0 && scroll_px != 0) step = scroll_px > 0 ? 1 : -1;
	if(step > cfg.scrollcap) step = cfg.scrollcap;
	if(step < -cfg.scrollcap) step = -cfg.scrollcap;

	step_x = scroll_px_x / cfg.scrollease;
	if(step_x == 0 && scroll_px_x != 0) step_x = scroll_px_x > 0 ? 1 : -1;
	if(step_x > cfg.scrollcap) step_x = cfg.scrollcap;
	if(step_x < -cfg.scrollcap) step_x = -cfg.scrollcap;

	wl_list_for_each_safe(w, tmp, &windows, link) {
		if(!w->swc) continue;
		if(w->sticky) continue;
		/* don't drag the window being moved; it causes jitters */
		if(chord_mode_cur == MODE_MOVE && w->swc == focused) continue;
		if(!swc_window_get_geometry(w->swc, &g)) continue;
		if(!cfg.scroll_drag_mode && !is_on_screen(&g, current_screen)) continue;

		swc_window_set_position(w->swc, g.x + step_x, g.y + step);
	}

	scroll_px -= step;
	scroll_px_x -= step_x;
	wl_event_source_timer_update(scroll_timer, cfg.timerms);
	return 0;
}

int
scroll_drag_tick(void *data)
{
	int32_t x, y, dx, dy;
	(void)data;

	if(chord_mode_cur != MODE_SCROLL) return 0;

	if(!cursor_position(&x, &y)) {
		wl_event_source_timer_update(scroll_drag_timer, cfg.timerms);
		return 0;
	}

	dx = x - scroll_drag_x;
	dy = y - scroll_drag_y;
	scroll_drag_x = x;
	scroll_drag_y = y;

	if(dx == 0 && dy == 0) {
		wl_event_source_timer_update(scroll_drag_timer, cfg.timerms);
		return 0;
	}

	/* inverted: drag up scrolls content down */
	scroll_px -= dy;
	scroll_px_x -= dx;

	if(dy != 0) {
		scroll_cursor_dir = dy > 0 ? 1 : -1;
		update_mode_cursor();
	}

	if(!scroll_timer)
		scroll_timer = wl_event_loop_add_timer(evloop, scroll_tick, NULL);
	if(scroll_timer)
		wl_event_source_timer_update(scroll_timer, 1);

	wl_event_source_timer_update(scroll_drag_timer, cfg.timerms);
	return 0;
}

int
move_scroll_tick(void *data)
{
	int32_t x, y;
	struct swc_rectangle g;
	int32_t screen_h = 0;
	(void)data;

	if(chord_mode_cur != MODE_MOVE) return 0;

	if(current_screen)
		screen_h = current_screen->swc->geometry.height;

	if(screen_h == 0) {
		wl_event_source_timer_update(move_scroll_timer, cfg.timerms);
		return 0;
	}

	if(!cursor_position(&x, &y)) {
		wl_event_source_timer_update(move_scroll_timer, cfg.timerms);
		return 0;
	}

	if(focused && swc_window_get_geometry(focused, &g)) {
		int32_t tx = move_start_win_x + (x - move_start_cursor_x);
		int32_t ty = move_start_win_y + (y - move_start_cursor_y);
		int32_t nx = g.x + (int32_t)((tx - g.x) * cfg.move_ease_factor);
		int32_t ny = g.y + (int32_t)((ty - g.y) * cfg.move_ease_factor);
		swc_window_set_position(focused, nx, ny);
	}

	if(y < cfg.move_scroll_edge_threshold) {
		scroll_px += cfg.move_scroll_speed;
		if(!scroll_timer)
			scroll_timer = wl_event_loop_add_timer(evloop, scroll_tick, NULL);
		if(scroll_timer) wl_event_source_timer_update(scroll_timer, 1);
	} else if(y > screen_h - cfg.move_scroll_edge_threshold) {
		scroll_px -= cfg.move_scroll_speed;
		if(!scroll_timer)
			scroll_timer = wl_event_loop_add_timer(evloop, scroll_tick, NULL);
		if(scroll_timer) wl_event_source_timer_update(scroll_timer, 1);
	}

	wl_event_source_timer_update(move_scroll_timer, cfg.timerms);
	return 0;
}
