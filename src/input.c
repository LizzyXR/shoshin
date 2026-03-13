#include "shoshin.h"

bool
cursor_position_raw(int32_t *x, int32_t *y)
{
	int32_t fx, fy;
	if(!swc_cursor_position(&fx, &fy)) return false;
	*x = wl_fixed_to_int(fx);
	*y = wl_fixed_to_int(fy);
	return true;
}

bool
cursor_position(int32_t *x, int32_t *y)
{
	if(!cursor_position_raw(x, y)) return false;
	if(cfg.enable_zoom) {
		float z = swc_get_zoom();
		if(z != 1.0f && current_screen) {
			int32_t cx = current_screen->swc->geometry.x + (int32_t)current_screen->swc->geometry.width  / 2;
			int32_t cy = current_screen->swc->geometry.y + (int32_t)current_screen->swc->geometry.height / 2;
			*x = (int32_t)((*x - cx) / z) + cx;
			*y = (int32_t)((*y - cy) / z) + cy;
		}
	}
	return true;
}

int
cursor_tick(void *data)
{
	int32_t x, y;
	struct screen *s;
	(void)data;

	if(cursor_position_raw(&x, &y)) {
		wl_list_for_each(s, &screens, link) {
			struct swc_rectangle *g = &s->swc->geometry;
			if(x >= g->x && x < g->x + (int32_t)g->width &&
			   y >= g->y && y < g->y + (int32_t)g->height) {
				current_screen = s;
				break;
			}
		}
	}

	wl_event_source_timer_update(cursor_timer, cfg.timerms);
	return 0;
}

static int
click_timeout(void *data)
{
	(void)data;
	if(!chord_pending) return 0;
	if(chord_mode_cur == MODE_MOVE) {
		click_cancel();
		return 0;
	}
	if(!chord_forwarded) {
		swc_pointer_send_button(chord_time, chord_btn, WL_POINTER_BUTTON_STATE_PRESSED);
		chord_forwarded = true;
	}
	return 0;
}

void
click_cancel(void)
{
	if(chord_timer) {
		wl_event_source_remove(chord_timer);
		chord_timer = NULL;
	}
	chord_pending = false;
	chord_forwarded = false;
}

void
axis(void *data, uint32_t time, uint32_t ax, int32_t value120)
{
	(void)data;

	if(chord_mode_cur == MODE_MOVE) return;

	if(cfg.scroll_drag_mode) {
		if(cfg.enable_zoom && chord_mode_cur == MODE_SCROLL && ax == 0 && value120 != 0) {
			if(zoom_target == 0.0f) zoom_target = swc_get_zoom();
			zoom_target += (value120 < 0) ? 0.15f : -0.15f;
			if(zoom_target < 0.25f) zoom_target = 0.25f;
			if(zoom_target > 4.0f) zoom_target = 4.0f;
			if(!zoom_timer) zoom_timer = wl_event_loop_add_timer(evloop, zoom_tick, NULL);
			if(zoom_timer) wl_event_source_timer_update(zoom_timer, 1);
			return;
		}
		swc_pointer_send_axis(time, ax, value120);
		return;
	}

	if(chord_mode_cur != MODE_SCROLL) {
		swc_pointer_send_axis(time, ax, value120);
		return;
	}
	if(ax != 0 || value120 == 0) {
		swc_pointer_send_axis(time, ax, value120);
		return;
	}

	scroll_cursor_dir = value120 < 0 ? -1 : 1;
	update_mode_cursor();
	scroll_px += value120 * cfg.scrollpx / 120;
	if(!scroll_timer) scroll_timer = wl_event_loop_add_timer(evloop, scroll_tick, NULL);
	if(scroll_timer) wl_event_source_timer_update(scroll_timer, 1);
}

void
button(void *data, uint32_t time, uint32_t b, uint32_t state)
{
	bool pressed, was_left, was_right, is_lr, is_chord_button, acme_pass;
	int32_t x = 0, y = 0;
	struct swc_rectangle g;

	(void)data;

	pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
	was_left = chord_left;
	was_right = chord_right;

	switch(b) {
		case BTN_LEFT: chord_left = pressed; break;
		case BTN_RIGHT: chord_right = pressed; break;
		case BTN_MIDDLE: chord_middle = pressed; break;
		default: break;
	}

	is_lr = (b == BTN_LEFT || b == BTN_RIGHT);
	is_chord_button = (is_lr || b == BTN_MIDDLE);
	acme_pass = false;

	if(cursor_position(&x, &y)) {
		struct swc_window *t = swc_window_at(x, y);
		if(is_acme(t) && t == focused) acme_pass = true;
	}

	/* 1-3 chord passthrough to acme */
	if(acme_pass && is_lr && pressed) {
		bool other = (b == BTN_LEFT) ? was_right : was_left;
		if(other) {
			swc_pointer_send_button(time, b, state);
			return;
		}
	}

	/* KILL: right held, then left press */
	if(b == BTN_LEFT && !pressed && chord_mode_cur == MODE_KILL) {
		if(cursor_position(&x, &y)) {
			struct swc_window *t = swc_window_at(x, y);
			if(t) swc_window_close(t);
		}
		chord_mode_cur = MODE_NONE;
		update_mode_cursor();
		if(!chord_left && !chord_middle && !chord_right) chord_active = false;
		return;
	}

	if(b == BTN_LEFT && pressed && was_right && !chord_active && !acme_pass) {
		click_cancel();
		stop_select();
		chord_active = true;
		chord_mode_cur = MODE_KILL;
		update_mode_cursor();
		return;
	}

	/* SCROLL: right held, then middle press */
	if(b == BTN_MIDDLE && pressed && was_right && !chord_active) {
		click_cancel(); stop_select();
		chord_active = true;
		chord_mode_cur = MODE_SCROLL;
		scroll_cursor_dir = -1;
		update_mode_cursor();
		scroll_stop();

		if(cfg.scroll_drag_mode && cursor_position(&x, &y)) {
			scroll_drag_x = x;
			scroll_drag_y = y;
			if(!scroll_drag_timer) scroll_drag_timer = wl_event_loop_add_timer(evloop, scroll_drag_tick, NULL);
			if(scroll_drag_timer) wl_event_source_timer_update(scroll_drag_timer, cfg.timerms);
		}
		return;
	}

	/* MOVE: left held, then middle release */
	if(b == BTN_MIDDLE && !pressed && was_left && !chord_active && !sel_active && !acme_pass) {
		click_cancel();
		stop_select();
		chord_active = true;
		chord_mode_cur = MODE_MOVE;
		update_mode_cursor();

		if(focused && cursor_position(&x, &y)) {
			struct swc_rectangle gg;
			if(swc_window_get_geometry(focused, &gg)) {
				move_start_win_x = gg.x;
				move_start_win_y = gg.y;
				move_start_cursor_x = x;
				move_start_cursor_y = y;
			}
		}

		if(!move_scroll_timer) move_scroll_timer = wl_event_loop_add_timer(evloop, move_scroll_tick, NULL);
		if(move_scroll_timer) wl_event_source_timer_update(move_scroll_timer, cfg.timerms);

		swc_pointer_send_button(time, b, state);
		return;
	}

	if(b == BTN_LEFT && !pressed && chord_mode_cur == MODE_MOVE) {
		chord_mode_cur = MODE_NONE;
		update_mode_cursor();
		if(move_scroll_timer) {
			wl_event_source_remove(move_scroll_timer);
			move_scroll_timer = NULL;
		}
		if(!chord_left && !chord_middle && !chord_right) chord_active = false;
		swc_pointer_send_button(time, b, state);
		return;
	}

	/* RESIZE: right held, then middle release */
	if(b == BTN_MIDDLE && !pressed && was_right && !chord_active && !sel_active) {
		click_cancel();
		stop_select();
		chord_active = true;
		chord_mode_cur = MODE_RESIZE;
		update_mode_cursor();
		if(focused) swc_window_begin_resize(focused, SWC_WINDOW_EDGE_RIGHT | SWC_WINDOW_EDGE_BOTTOM);
		swc_pointer_send_button(time, b, state);
		return;
	}

	if(b == BTN_RIGHT && !pressed && chord_mode_cur == MODE_RESIZE) {
		chord_mode_cur = MODE_NONE;
		update_mode_cursor();
		if(focused) swc_window_end_resize(focused);
		if(!chord_left && !chord_middle && !chord_right) chord_active = false;
		swc_pointer_send_button(time, b, state);
		return;
	}

	/* 2-1 chord: left held, then middle press */
	if(b == BTN_MIDDLE && pressed && was_left && !chord_active) {
		click_cancel();
		stop_select();

		if(focused) {
			struct window *w;
			wl_list_for_each(w, &windows, link) {
				if(w->swc != focused) continue;

				switch(cfg.chord21) {
				case CHORD21_STICKY:
					w->sticky = !w->sticky;
					break;
				case CHORD21_FULLSCREEN:
					w->sticky = !w->sticky;
					if(current_screen) swc_window_set_fullscreen(focused, current_screen->swc);
					break;
				case CHORD21_JUMP: {
					bool old_fc = cfg.focus_center;
					cfg.focus_center = true;
					chord_mode_cur   = MODE_JUMP;

					struct window *closest = NULL, *n;
					struct swc_rectangle ng;
					int32_t cx = 0, cy = 0;
					int64_t mindist = INT64_MAX;

					cursor_position_raw(&cx, &cy);
					wl_list_for_each(n, &windows, link) {
						if(!n->swc || n->swc == focused) continue;
						if(!swc_window_get_geometry(n->swc, &ng)) continue;
						int64_t dx = (int64_t)cx - ng.x;
						int64_t dy = (int64_t)cy - ng.y;
						int64_t d  = dx*dx + dy*dy;
						if(d < mindist) {
							mindist = d;
							closest = n;
						}
					}
					if(closest) focus_window(closest->swc, "jump");

					chord_mode_cur = MODE_NONE;
					cfg.focus_center = old_fc;
					break;
				}
				}
				break;
			}
		}

		chord_active = true;
		swc_pointer_send_button(time, b, state);
		return;
	}

	/* swallow middle release while scrolling */
	if(b == BTN_MIDDLE && !pressed && chord_mode_cur == MODE_SCROLL) return;

	/* focus on left-press */
	if(pressed && is_lr && !sel_active) {
		bool other = (b == BTN_LEFT) ? was_right : was_left;
		if(scroll_auto) {
			scroll_auto = false;
			scroll_stop();
		}
		if(b == BTN_LEFT && !other && cursor_position(&x, &y)) {
			struct swc_window *t = swc_window_at(x, y);
			if(t) focus_window(t, "click");
		}
	}

	/* left+right -> selection box */
	if(chord_left && chord_right && !chord_active && !acme_pass) {
		click_cancel();
		chord_active = true;
		if(cursor_position(&x, &y)) {
			sel_active = true;
			sel_start_x = x; sel_start_y = y;
			sel_cur_x = x; sel_cur_y = y;
			update_mode_cursor();
			swc_overlay_set_box(x, y, x, y, cfg.select_box_color, cfg.select_box_border);
			if(!sel_timer) sel_timer = wl_event_loop_add_timer(evloop, select_tick, NULL);
			if(sel_timer) wl_event_source_timer_update(sel_timer, cfg.timerms);
		}
	}

	/* swallow while chord active */
	if(is_chord_button && chord_active && !sel_active) {
		bool was_scroll = (chord_mode_cur == MODE_SCROLL);
		if(!chord_right && chord_mode_cur == MODE_SCROLL) chord_mode_cur = MODE_NONE;
		if(was_scroll && chord_mode_cur != MODE_SCROLL) {
			update_mode_cursor();
			scroll_stop();
		}
		if(!chord_left && !chord_middle && !chord_right) chord_active = false;
		return;
	}

	if(b == BTN_MIDDLE) {
		if(chord_mode_cur == MODE_MOVE) return;
		swc_pointer_send_button(time, b, state);
		return;
	}

	/* delayed-click passthrough */
	if(is_lr && pressed && !sel_active) {
		bool other = (b == BTN_LEFT) ? was_right : was_left;
		if(!other && !chord_pending) {
			chord_pending = true;
			chord_forwarded = false;
			chord_btn = b;
			chord_time = time;
			if(!chord_timer) chord_timer = wl_event_loop_add_timer(evloop, click_timeout, NULL);
			if(chord_timer) wl_event_source_timer_update(chord_timer, cfg.chord_click_timeout_ms);
			return;
		}
	}

	if(is_lr && !pressed && !sel_active) {
		if(chord_pending && chord_btn == b) {
			if(!chord_forwarded) swc_pointer_send_button(chord_time, chord_btn, WL_POINTER_BUTTON_STATE_PRESSED);
			swc_pointer_send_button(time, b, WL_POINTER_BUTTON_STATE_RELEASED);
			click_cancel();
			return;
		}
		swc_pointer_send_button(time, b, WL_POINTER_BUTTON_STATE_RELEASED);
		return;
	}

	/* right release ends selection, spawn terminal */
	if(b == BTN_RIGHT && !pressed && sel_active) {
		uint32_t bw = cfg.outer_border_width + cfg.inner_border_width;
		int32_t  x1, y1, x2, y2;
		uint32_t ow, oh;

		if(!cursor_position(&x, &y)) {
			x = sel_cur_x;
			y = sel_cur_y;
		}
		stop_select();

		x1 = sel_start_x < x ? sel_start_x : x;
		y1 = sel_start_y < y ? sel_start_y : y;
		x2 = sel_start_x < x ? x : sel_start_x;
		y2 = sel_start_y < y ? y : sel_start_y;
		ow = (uint32_t)(x2 - x1);
		oh = (uint32_t)(y2 - y1);
		if(ow < 50 + 2*bw) ow = 50 + 2*bw;
		if(oh < 50 + 2*bw) oh = 50 + 2*bw;

		g.x = x1 + (int32_t)bw;
		g.y = y1 + (int32_t)bw;
		g.width  = ow > 2*bw ? ow - 2*bw : 1;
		g.height = oh > 2*bw ? oh - 2*bw : 1;

		spawn_term_select(&g);
		printf("spawned terminal at %d,%d %ux%u\n", g.x, g.y, g.width, g.height);
	}

	if(!is_lr) swc_pointer_send_button(time, b, state);
	if(!chord_left && !chord_middle && !chord_right) chord_active = false;
}
