#define _POSIX_C_SOURCE 200809L

#include "shoshin.h"

struct wl_display *display;
struct wl_event_loop *evloop;
struct wl_list windows;
struct wl_list screens;
struct screen *current_screen;
struct swc_window *focused;

chord_mode chord_mode_cur;
bool chord_left, chord_right, chord_middle;
bool chord_active;
bool chord_pending, chord_forwarded;
uint32_t chord_btn, chord_time;
struct wl_event_source *chord_timer;

int32_t move_start_win_x, move_start_win_y;
int32_t move_start_cursor_x, move_start_cursor_y;
struct wl_event_source *move_scroll_timer;

int32_t scroll_px, scroll_px_x;
int8_t scroll_cursor_dir;
bool scroll_auto;
struct wl_event_source *scroll_timer;

int32_t scroll_drag_x, scroll_drag_y;
struct wl_event_source *scroll_drag_timer;

float zoom_target;
struct wl_event_source *zoom_timer;

bool sel_active;
int32_t sel_start_x, sel_start_y;
int32_t sel_cur_x, sel_cur_y;
struct wl_event_source *sel_timer;

bool spawn_pending;
struct swc_rectangle spawn_geometry;

struct wl_event_source *cursor_timer;

int current_workspace = 1;
pid_t bar_pid = -1;
pid_t neumenu_pid = -1;

static void
setup_nein_cursor(void)
{
	const struct nein_cursor_meta *arrow = &nein_cursor_metadata[NEIN_CURSOR_WHITEARROW];
	const struct nein_cursor_meta *box = &nein_cursor_metadata[NEIN_CURSOR_BOXCURSOR];
	const struct nein_cursor_meta *cross = &nein_cursor_metadata[NEIN_CURSOR_CROSSCURSOR];
	const struct nein_cursor_meta *sight = &nein_cursor_metadata[NEIN_CURSOR_SIGHTCURSOR];
	const struct nein_cursor_meta *up = &nein_cursor_metadata[NEIN_CURSOR_T];
	const struct nein_cursor_meta *down = &nein_cursor_metadata[NEIN_CURSOR_B];

	if(!cfg.cursor_theme[0] || strcmp(cfg.cursor_theme, "nein") != 0) return;

	swc_set_cursor_mode(SWC_CURSOR_MODE_COMPOSITOR);
	swc_set_cursor_image(SWC_CURSOR_DEFAULT, &nein_cursor_data[arrow->offset], arrow->width, arrow->height, arrow->hotspot_x, arrow->hotspot_y);
	swc_set_cursor_image(SWC_CURSOR_BOX, &nein_cursor_data[box->offset], box->width, box->height, box->hotspot_x, box->hotspot_y);
	swc_set_cursor_image(SWC_CURSOR_CROSS, &nein_cursor_data[cross->offset], cross->width, cross->height, cross->hotspot_x, cross->hotspot_y);
	swc_set_cursor_image(SWC_CURSOR_SIGHT, &nein_cursor_data[sight->offset], sight->width, sight->height, sight->hotspot_x, sight->hotspot_y);
	swc_set_cursor_image(SWC_CURSOR_UP, &nein_cursor_data[up->offset], up->width, up->height, up->hotspot_x, up->hotspot_y);
	swc_set_cursor_image(SWC_CURSOR_DOWN, &nein_cursor_data[down->offset], down->width, down->height, down->hotspot_x, down->hotspot_y);

	update_mode_cursor();
}

static void
applyconfig(void)
{
	struct window *w;
	wl_list_for_each(w, &windows, link) {
		if(!w->swc) continue;
		swc_window_set_border(w->swc, cfg.inner_border_color_inactive, cfg.inner_border_width, cfg.outer_border_color_inactive, cfg.outer_border_width);
	}
	swc_wallpaper_color_set(cfg.background_color);
}

static void
reloadconfig(void *data, uint32_t time, uint32_t key, uint32_t state)
{
	char path[512];
	(void)data;
	(void)time;
	(void)key;

	if(state) return;

	cfg_find_path(path, sizeof(path), NULL);
	if(path[0]) {
		cfg_load(path);
		applyconfig();
		printf("config reloaded from %s\n", path);
	} else {
		printf("no config found, keeping current settings\n");
	}
}

static void
killwindow(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	(void)data;
	(void)time;
	(void)value;
	if(state) return;
	if(focused) swc_window_close(focused);
}

static void
quit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	(void)data;
	(void)time;
	(void)value;
	(void)state;
	wl_display_terminate(display);
}

static void
sig(int s)
{
	(void)s;
	wl_display_terminate(display);
}

static void
spawn_bar(void)
{
	bar_pid = fork();
	if(bar_pid == 0) {
		execl("/proc/self/exe", "shoshin", "--bar", NULL);
		_exit(1);
	}
}

static void
neumenu_runner(void *data, uint32_t time, uint32_t key, uint32_t state)
{
	(void)data;
	(void)time;
	(void)key;

	if(state) return;

	neumenu_pid = fork();
	if(neumenu_pid == 0) {
		execlp("neumenu", "neumenu", NULL);
		_exit(127);
	}
}

static void
on_activate(void) {}

static void
on_deactivate(void) {}

static const struct swc_manager manager = {
	.new_screen = newscreen,
	.new_window = newwindow,
	.new_device = newdevice,
	.activate = on_activate,
	.deactivate = on_deactivate,
};

int
main(int argc, char **argv)
{
	const char *sock;
	char cfgpath[512];
	int i;

	static const uint32_t ws_keys[9] = {XKB_KEY_1, XKB_KEY_2, XKB_KEY_3, XKB_KEY_4, XKB_KEY_5, XKB_KEY_6, XKB_KEY_7, XKB_KEY_8, XKB_KEY_9};

	if(argc >= 2 && strcmp(argv[1], "--bar") == 0) return bar_main();

	wl_list_init(&windows);
	wl_list_init(&screens);
	memset(&spawn_geometry, 0, sizeof(spawn_geometry));

	ipc_init();

	cfg_find_path(cfgpath, sizeof(cfgpath), argv[0]);
	if(cfgpath[0]) {
		cfg_load(cfgpath);
	} else {
		fprintf(stderr, "[config] no config found, using defaults\n");
		cfg_load("/dev/null");
	}

	display = wl_display_create();
	if(!display) {
		fprintf(stderr, "cannot create display\n");
		return 1;
	}

	evloop = wl_display_get_event_loop(display);

	if(!swc_initialize(display, evloop, &manager)) {
		fprintf(stderr, "cannot initialize swc\n");
		return 1;
	}

	swc_wallpaper_color_set(cfg.background_color);
	setup_nein_cursor();

	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT | SWC_MOD_CTRL, XKB_KEY_q, quit, NULL);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_q, killwindow, NULL);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_d, neumenu_runner, NULL);
	swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT, XKB_KEY_r, reloadconfig, NULL);

	for(i = 0; i < 9; i++) {
		swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, ws_keys[i], workspace_switch, (void*)(intptr_t)(i + 1));
		swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT, ws_keys[i], workspace_move_window, (void*)(intptr_t)(i + 1));
	}

	swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_LEFT, button, NULL);
	swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_MIDDLE, button, NULL);
	swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_RIGHT, button, NULL);
	if(swc_add_axis_binding(SWC_MOD_ANY, 0, axis, NULL) < 0)
		fprintf(stderr, "cannot bind vertical scroll axis\n");
	if(swc_add_axis_binding(SWC_MOD_ANY, 1, axis, NULL) < 0)
		fprintf(stderr, "cannot bind horizontal scroll axis\n");

	sock = wl_display_add_socket_auto(display);
	if(!sock) {
		fprintf(stderr, "cannot add socket\n");
		return 1;
	}

	printf("%s\n", sock);
	setenv("WAYLAND_DISPLAY", sock, 1);

	ipc_write_workspace(current_workspace);
	ipc_write_title(NULL);

	spawn_bar();

	signal(SIGTERM, sig);
	signal(SIGINT, sig);
	signal(SIGCHLD, SIG_IGN);

	wl_display_run(display);

	if(bar_pid > 0) {
		kill(bar_pid, SIGTERM);
		waitpid(bar_pid, NULL, 0);
	}

	swc_finalize();
	wl_display_destroy(display);
	return 0;
}
