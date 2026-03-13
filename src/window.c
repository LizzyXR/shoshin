#include "shoshin.h"

struct screen *
primary_screen(void)
{
	struct screen *s;
	if(!wl_list_empty(&screens))
		return wl_container_of(screens.next, s, link);
	return NULL;
}

bool
is_visible(struct swc_window *w, struct screen *screen)
{
	struct swc_rectangle *geom = &screen->swc->geometry;
	struct swc_rectangle  wg;
	if(!swc_window_get_geometry(w, &wg)) return false;
	return wg.x + (int32_t)wg.width > geom->x &&
	       wg.x < geom->x + (int32_t)geom->width &&
	       wg.y + (int32_t)wg.height > geom->y &&
	       wg.y < geom->y + (int32_t)geom->height;
}

bool
is_on_screen(struct swc_rectangle *w, struct screen *screen)
{
	struct swc_rectangle *geom = &screen->swc->geometry;
	return w->x + (int32_t)w->width > geom->x && w->x < geom->x + (int32_t)geom->width;
}

bool
is_acme(const struct swc_window *swc)
{
	return swc && swc->app_id && strcmp(swc->app_id, "acme") == 0;
}

void
focus_window(struct swc_window *swc, const char *reason)
{
	const char *from = focused && focused->title ? focused->title : "";
	const char *to = swc && swc->title ? swc->title : "";

	if(focused == swc) return;

	printf("focus '%s' -> '%s' (%s)\n", from, to, reason);

	if(focused)
		swc_window_set_border(focused, cfg.inner_border_color_inactive, cfg.inner_border_width, cfg.outer_border_color_inactive, cfg.outer_border_width);

	swc_window_focus(swc);

	if(cfg.enable_zoom && swc && swc_get_zoom() != 1.0f) {
		zoom_target = 1.0f;
		if(!zoom_timer)
			zoom_timer = wl_event_loop_add_timer(evloop, zoom_tick, NULL);
		if(zoom_timer)
			wl_event_source_timer_update(zoom_timer, 1);
	}

	if(swc)
		swc_window_set_border(swc, cfg.inner_border_color_active, cfg.inner_border_width, cfg.outer_border_color_active, cfg.outer_border_width);

	focused = swc;
	ipc_write_title(swc ? swc->title : NULL);

	/* auto-scroll to center the focused window */
	if(cfg.focus_center && swc && (chord_mode_cur == MODE_JUMP || current_screen)) {
		struct swc_rectangle wg;
		struct screen *target_screen = NULL;

		if(swc_window_get_geometry(swc, &wg)) {
			if(wg.width == 0 || wg.height == 0) return;

			/* find which screen geometrically contains this window */
			struct screen *s;
			wl_list_for_each(s, &screens, link) {
				struct swc_rectangle *sg = &s->swc->geometry;
				if(wg.x + (int32_t)wg.width > sg->x &&
				   wg.x < sg->x + (int32_t)sg->width &&
				   wg.y + (int32_t)wg.height > sg->y &&
				   wg.y < sg->y + (int32_t)sg->height) {
					target_screen = s;
					break;
				}
			}

			/* fallback to current_screen if window isn't clearly on any screen */
			if(!target_screen) target_screen = current_screen;
			if(!target_screen) return;

			int32_t wcx = wg.x + (int32_t)wg.width / 2;
			int32_t wcy = wg.y + (int32_t)wg.height / 2;
			int32_t scx = target_screen->swc->geometry.x + (int32_t)target_screen->swc->geometry.width / 2;
			int32_t scy = target_screen->swc->geometry.y + (int32_t)target_screen->swc->geometry.height / 2;

			int32_t dx = cfg.scroll_drag_mode ? (scx - wcx) : 0;
			int32_t dy = scy - wcy;

			if(dx != 0 || dy != 0) {
				scroll_stop();
				scroll_px = dy;
				scroll_px_x = dx;
				scroll_auto = true;

				if(!scroll_timer) scroll_timer = wl_event_loop_add_timer(evloop, scroll_tick, NULL);
				wl_event_source_timer_update(scroll_timer, cfg.timerms);
			}
		}
	}
}

/* pid helpers (linux /proc) */
static pid_t
get_parent_pid(pid_t pid)
{
	char path[64];
	FILE *f;
	pid_t ppid = 0;
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	f = fopen(path, "r");
	if(!f) return 0;
	fscanf(f, "%*d %*s %*c %d", &ppid);
	fclose(f);
	return ppid;
}

static struct window *
find_window_by_pid(pid_t pid)
{
	struct window *w;
	wl_list_for_each(w, &windows, link) {
		if(w->pid == pid) return w;
	}
	return NULL;
}

static bool
is_terminal_window(struct window *w)
{
	int i;
	if(!w || !w->swc) return false;
	for(i = 0; i < MAX_TERM_IDS && cfg.terminal_app_ids[i][0]; i++) {
		if(w->swc->app_id && strstr(w->swc->app_id, cfg.terminal_app_ids[i])) return true;
		if(w->swc->title  && strstr(w->swc->title, cfg.terminal_app_ids[i])) return true;
	}
	return false;
}

static void
mk_spawn_link(struct window *terminal, struct window *child)
{
	child->spawn_parent = terminal;
	wl_list_insert(&terminal->spawn_children, &child->spawn_link);
	if(swc_window_get_geometry(terminal->swc, &terminal->saved_geometry)) {
		terminal->hidden_for_spawn = true;
		swc_window_hide(terminal->swc);
		swc_window_set_geometry(child->swc, &terminal->saved_geometry);
	}
}

/* window event handlers */
static void
windowdestroy(void *data)
{
	struct window *w = data;

	if(w->spawn_parent) {
		struct window *terminal = w->spawn_parent;
		wl_list_remove(&w->spawn_link);
		if(wl_list_empty(&terminal->spawn_children) && terminal->hidden_for_spawn) {
			swc_window_show(terminal->swc);
			swc_window_set_geometry(terminal->swc, &terminal->saved_geometry);
			terminal->hidden_for_spawn = false;
			focus_window(terminal->swc, "spawn_child_destroyed");
		}
	}

	if(!wl_list_empty(&w->spawn_children)) {
		struct window *child, *tmp;
		wl_list_for_each_safe(child, tmp, &w->spawn_children, spawn_link) {
			child->spawn_parent = NULL;
			wl_list_remove(&child->spawn_link);
			wl_list_init(&child->spawn_link);
		}
	}

	if(focused == w->swc) focus_window(NULL, "destroy");
	wl_list_remove(&w->link);
	free(w);
}

static void
windowappidchanged(void *data)
{
	struct window *w = data;
	struct swc_rectangle g;
	bool is_sel = spawn_pending && w->swc->app_id && strcmp(w->swc->app_id, cfg.select_term_app_id) == 0;
	if(!is_sel) return;
	g = spawn_geometry;
	if(g.width < 50) g.width = 50;
	if(g.height < 50) g.height = 50;
	swc_window_set_geometry(w->swc, &g);
	spawn_pending = false;
}

static void
windowtitlechanged(void *data)
{
	struct window *w = data;
	if(focused && w->swc == focused)
		ipc_write_title(w->swc->title);
}

static const struct swc_window_handler windowhandler = {
	.destroy = windowdestroy,
	.app_id_changed = windowappidchanged,
	.title_changed = windowtitlechanged,
};

void
newwindow(struct swc_window *swc)
{
	struct window *w;
	struct swc_rectangle g;
	bool is_sel = spawn_pending && swc->app_id && strcmp(swc->app_id, cfg.select_term_app_id) == 0;

	w = malloc(sizeof(*w));
	if(!w) return;

	w->swc = swc;
	w->pid = 0;
	w->spawn_parent = NULL;
	w->hidden_for_spawn = false;
	w->sticky = false;
	w->workspace = current_workspace;
	wl_list_init(&w->spawn_children);
	wl_list_init(&w->spawn_link);

	wl_list_insert(&windows, &w->link);
	swc_window_set_handler(swc, &windowhandler, w);
	swc_window_set_stacked(swc);
	swc_window_set_border(swc, cfg.inner_border_color_inactive, cfg.inner_border_width, cfg.outer_border_color_inactive, cfg.outer_border_width);

	if(cfg.enable_terminal_spawning) {
		w->pid = swc_window_get_pid(swc);
		if(w->pid > 0) {
			pid_t cur = w->pid;
			struct window *terminal = NULL;
			int depth = 0;
			while(depth < 10 && cur > 1) {
				pid_t parent = get_parent_pid(cur);
				if(parent <= 1) break;
				{
					struct window *candidate = find_window_by_pid(parent);
					if(candidate && is_terminal_window(candidate)) {
						terminal = candidate;
						break;
					}
				}
				cur = parent;
				depth++;
			}
			if(terminal) mk_spawn_link(terminal, w);
		}
	}

	if(is_sel) {
		g = spawn_geometry;
		if(g.width < 50) g.width = 50;
		if(g.height < 50) g.height = 50;
		swc_window_set_geometry(swc, &g);
		spawn_pending = false;
	}

	swc_window_show(swc);
	printf("window '%s'\n", swc->title ? swc->title : "");
	focus_window(swc, "new_window");
}

/* screen event handlers */
static void
screendestroy(void *data)
{
	struct screen *s = data;
	if(current_screen == s) current_screen = primary_screen();
	wl_list_remove(&s->link);
	free(s);
}

static const struct swc_screen_handler screenhandler = {
	.destroy = screendestroy,
};

void
newscreen(struct swc_screen *swc)
{
	struct screen *s;

	s = malloc(sizeof(*s));
	if(!s) return;

	s->swc = swc;
	wl_list_insert(&screens, &s->link);
	swc_screen_set_handler(swc, &screenhandler, s);

	/* first screen registered becomes current; no name-based selection */
	if(!current_screen) current_screen = s;

	printf("screen %dx%d+%d+%d\n", swc->geometry.width, swc->geometry.height, swc->geometry.x, swc->geometry.y);

	if(!cursor_timer)
		cursor_timer = wl_event_loop_add_timer(evloop, cursor_tick, NULL);
	if(cursor_timer)
		wl_event_source_timer_update(cursor_timer, cfg.timerms);
}

void
newdevice(struct libinput_device *dev)
{
	(void)dev;
}
