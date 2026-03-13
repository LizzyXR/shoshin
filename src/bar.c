/*
 * bar.c; the shoshin status bar :sunglasses:
 *
 * IPC (read-only):
 *   /tmp/shoshin-<uid>/workspace      current workspace number
 *   /tmp/shoshin-<uid>/focused_title  focused window title
 *
 * updates instantly via inotify on IPC dir
 * status (cpu/ram/time) updates every 60 seconds via timerfd
*/

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wld/wld.h>

#include "swc-client-protocol.h"
#include "config.h"

extern struct wld_context *wld_pixman_context;

#define WS_COUNT 9
#define PAD_X 6
#define WS_BOX_W 18

struct shm_buf {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	bool busy;
};

static volatile sig_atomic_t running = 1;

static struct {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct swc_panel_manager *panel_manager;
	struct wl_surface *surface;
	struct swc_panel *panel;

	uint32_t width;
	uint32_t height;
	bool docked;

	struct shm_buf bufs[2];
	int cur;

	struct wld_renderer *renderer;
	struct wld_font_context *font_ctx;
	struct wld_font *font;

	int workspace;
	char title[256];
	char cpu[16];
	char ram[16];
	char timestr[32];
	char hostname[64];

	char ipc_dir[256];
} bar;

static void
on_signal(int s) {
	(void)s;
	running = 0;
}

static void
ipc_read(const char *name, char *buf, size_t n)
{
	char path[512];
	FILE *f;
	buf[0] = '\0';
	if(!bar.ipc_dir[0]) return;
	snprintf(path, sizeof(path), "%s/%s", bar.ipc_dir, name);
	f = fopen(path, "r");
	if(!f) return;
	if(fgets(buf, (int)n, f)) {
		size_t l = strlen(buf);
		if(l && buf[l-1] == '\n') buf[l-1] = '\0';
	}
	fclose(f);
}

static void
update_workspace(void)
{
	char b[8];
	int ws;
	ipc_read("workspace", b, sizeof(b));
	ws = atoi(b);
	if(ws >= 1 && ws <= WS_COUNT) bar.workspace = ws;
}

static void
update_title(void)
{
	ipc_read("focused_title", bar.title, sizeof(bar.title));
}

static void
update_cpu(void)
{
	static unsigned long long prev_idle = 0, prev_total = 0;
	unsigned long long u, n, s, idle, iow, irq, sirq, steal;
	unsigned long long total, d_idle, d_total;
	FILE *f = fopen("/proc/stat", "r");
	if(!f) {
		snprintf(bar.cpu, sizeof(bar.cpu), "cpu:?");
		return;
	}
	fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &idle, &iow, &irq, &sirq, &steal);
	fclose(f);
	total = u + n + s + idle + iow + irq + sirq + steal;
	d_idle = idle - prev_idle;
	d_total = total - prev_total;
	prev_idle = idle;
	prev_total = total;
	snprintf(bar.cpu, sizeof(bar.cpu), "cpu:%d%%", d_total > 0 ? (int)(100 - d_idle * 100 / d_total) : 0);
}

static void
update_ram(void)
{
	char line[128];
	unsigned long total = 0, avail = 0;
	FILE *f = fopen("/proc/meminfo", "r");
	if(!f) {
		snprintf(bar.ram, sizeof(bar.ram), "ram:?");
		return;
	}
	while(fgets(line, sizeof(line), f)) {
		sscanf(line, "MemTotal: %lu kB", &total);
		sscanf(line, "MemAvailable: %lu kB", &avail);
	}
	fclose(f);
	snprintf(bar.ram, sizeof(bar.ram), "ram:%d%%", total > 0 ? (int)((total - avail) * 100 / total) : 0);
}

static void
update_time(void)
{
	time_t t = time(NULL);
	strftime(bar.timestr, sizeof(bar.timestr), "%a %d %b  %H:%M", localtime(&t));
}

static void
update_all(void)
{
	update_workspace();
	update_title();
	update_cpu();
	update_ram();
	update_time();
}

static void
buf_release(void *data, struct wl_buffer *wl)
{
	struct shm_buf *b = data;
	(void)wl;
	b->busy = false;
}

static const struct wl_buffer_listener buf_listener = {
	.release = buf_release,
};

static bool
buf_alloc(struct shm_buf *b, uint32_t w, uint32_t h)
{
	int fd;
	char name[] = "/tmp/shoshin-bar-XXXXXX";
	uint32_t stride = w * 4;
	size_t size = stride * h;
	struct wl_shm_pool *pool;

	fd = mkostemp(name, O_CLOEXEC);
	if(fd < 0) return false;
	unlink(name);

	if(ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return false;
	}

	b->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(b->data == MAP_FAILED) {
		close(fd);
		b->data = NULL;
		return false;
	}

	pool = wl_shm_create_pool(bar.shm, fd, (int32_t)size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, (int32_t)w, (int32_t)h, (int32_t)stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	if(!b->wl) {
		munmap(b->data, size);
		b->data = NULL;
		return false;
	}

	b->size = size;
	b->busy = false;
	wl_buffer_add_listener(b->wl, &buf_listener, b);
	return true;
}

static void
buf_free(struct shm_buf *b)
{
	if(b->wl) {
		wl_buffer_destroy(b->wl);
		b->wl = NULL;
	}
	if(b->data) {
		munmap(b->data, b->size);
		b->data = NULL;
	}
	b->busy = false;
}

static int32_t
text_w(const char *text)
{
	struct wld_extents ex;
	if(!bar.font || !text || !*text) return 0;
	wld_font_text_extents(bar.font, text, &ex);
	return (int32_t)ex.advance;
}

static void
render(void)
{
	struct shm_buf *b;
	struct wld_buffer *wbuf;
	union wld_object obj;
	uint32_t stride = bar.width * 4;
	int i;
	int32_t x, text_y;
	char ws_str[3], right[192], title[256];
	int32_t right_w, avail;
	uint32_t bg, fg;

	if(!bar.docked || !bar.shm) return;

	b = &bar.bufs[bar.cur];
	if(b->busy) {
		bar.cur ^= 1;
		b = &bar.bufs[bar.cur];
	}
	if(b->busy) return;

	obj.ptr = b->data;
	wbuf = wld_import_buffer(wld_pixman_context, WLD_OBJECT_DATA, obj, bar.width, bar.height, WLD_FORMAT_ARGB8888, stride);
	if(!wbuf) return;

	if(!wld_set_target_buffer(bar.renderer, wbuf)) {
		wld_buffer_unreference(wbuf);
		return;
	}

	text_y = (int32_t)(bar.height / 2) - (int32_t)(bar.font ? bar.font->height / 2 : 0);

	wld_fill_rectangle(bar.renderer, cfg.bar_bg, 0, 0, bar.width, bar.height);

	x = PAD_X;

	for(i = 1; i <= WS_COUNT; i++) {
		bg = (i == bar.workspace) ? cfg.bar_ws_active_bg : cfg.bar_bg;
		fg = (i == bar.workspace) ? cfg.bar_ws_active_fg : cfg.bar_fg;
		wld_fill_rectangle(bar.renderer, bg, x, 1, WS_BOX_W, (int32_t)bar.height - 2);
		snprintf(ws_str, sizeof(ws_str), "%d", i);
		if(bar.font) {
			int32_t tw = text_w(ws_str);
			wld_draw_text(bar.renderer, bar.font, fg, x + (WS_BOX_W - tw) / 2, text_y, ws_str, (uint32_t)strlen(ws_str), NULL);
		}
		x += WS_BOX_W + 1;
	}

	wld_fill_rectangle(bar.renderer, 0xff333333, x, 0, 1, bar.height);
	x += PAD_X;

	snprintf(right, sizeof(right), "%s  %s  %s  %s", bar.cpu, bar.ram, bar.hostname, bar.timestr);
	right_w = text_w(right) + PAD_X * 2;

	if(bar.title[0] && bar.font) {
		avail = (int32_t)bar.width - x - right_w - PAD_X;
		if(avail > 0) {
			snprintf(title, sizeof(title), "%s", bar.title);
			while(title[0] && text_w(title) > avail) {
				size_t l = strlen(title);
				if(l > 3) {
					title[l-1] = '\0';
					title[l-2] = '.';
					title[l-3] = '.';
					title[l-4] = '.';
				} else {
					title[0] = '\0';
					break;
				}
			}
			if(title[0]) 
				wld_draw_text(bar.renderer, bar.font, cfg.bar_fg, x, text_y, title, (uint32_t)strlen(title), NULL);
		}
	}

	if(bar.font)
		wld_draw_text(bar.renderer, bar.font, cfg.bar_fg, (int32_t)bar.width - text_w(right) - PAD_X, text_y, right, (uint32_t)strlen(right), NULL);

	wld_flush(bar.renderer);
	wld_buffer_unreference(wbuf);

	b->busy = true;
	wl_surface_attach(bar.surface, b->wl, 0, 0);
	wl_surface_damage(bar.surface, 0, 0, (int32_t)bar.width, (int32_t)bar.height);
	wl_surface_commit(bar.surface);
	wl_display_flush(bar.display);

	bar.cur ^= 1;
}

static void
panel_docked(void *data, struct swc_panel *panel, uint32_t length)
{
	(void)data;
	bar.width = length;
	bar.height = (uint32_t)cfg.bar_height;
	bar.docked = true;

	if(!buf_alloc(&bar.bufs[0], bar.width, bar.height) || !buf_alloc(&bar.bufs[1], bar.width, bar.height)) {
		fprintf(stderr, "[bar] shm alloc failed\n");
		running = 0;
		return;
	}

	bar.renderer = wld_create_renderer(wld_pixman_context);
	if(!bar.renderer) {
		fprintf(stderr, "[bar] wld_create_renderer failed\n");
		running = 0;
		return;
	}

	bar.font_ctx = wld_font_create_context();
	if(bar.font_ctx)
		bar.font = wld_font_open_name(bar.font_ctx, cfg.bar_font);
	if(!bar.font)
		fprintf(stderr, "[bar] font '%s' not found, text will be absent\n", cfg.bar_font);

	update_all();
	render();
	wl_display_flush(bar.display);

	swc_panel_set_strut(panel, bar.height, 0, bar.width);
}

static const struct swc_panel_listener panel_listener = {
	.docked = panel_docked,
};

static void
registry_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t version)
{
	(void)data;
	if(!strcmp(iface, wl_compositor_interface.name))
		bar.compositor = wl_registry_bind(reg, name, &wl_compositor_interface, version < 4 ? version : 4);
	else if(!strcmp(iface, wl_shm_interface.name))
		bar.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	else if(!strcmp(iface, swc_panel_manager_interface.name))
		bar.panel_manager = wl_registry_bind(reg, name, &swc_panel_manager_interface, 1);
}

static void
registry_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data;
	(void)reg;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

int
bar_main(void)
{
	char cfgpath[512];
	int tfd, ifd, wfd, maxfd, r;
	fd_set rfds;
	struct itimerspec its;
	uint64_t exp;
	char evbuf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	memset(&bar, 0, sizeof(bar));
	bar.workspace = 1;
	gethostname(bar.hostname, sizeof(bar.hostname));

	snprintf(bar.ipc_dir, sizeof(bar.ipc_dir), "/tmp/shoshin-%d", (int)getuid());

	cfg_find_path(cfgpath, sizeof(cfgpath), NULL);
	if(cfgpath[0])
		cfg_load(cfgpath);

	usleep(200000);

	bar.display = wl_display_connect(NULL);
	if(!bar.display) {
		fprintf(stderr, "[bar] cannot connect to wayland\n");
		return 1;
	}

	bar.registry = wl_display_get_registry(bar.display);
	wl_registry_add_listener(bar.registry, &registry_listener, NULL);
	wl_display_roundtrip(bar.display);

	if(!bar.compositor || !bar.shm || !bar.panel_manager) {
		fprintf(stderr, "[bar] missing wayland globals\n");
		return 1;
	}

	bar.surface = wl_compositor_create_surface(bar.compositor);
	if(!bar.surface) {
		fprintf(stderr, "[bar] no surface\n");
		return 1;
	}

	bar.panel = swc_panel_manager_create_panel(bar.panel_manager, bar.surface);
	if(!bar.panel) {
		fprintf(stderr, "[bar] no panel\n");
		return 1;
	}
	swc_panel_add_listener(bar.panel, &panel_listener, NULL);

	swc_panel_dock(bar.panel, SWC_PANEL_EDGE_TOP, NULL, 0);
	wl_display_roundtrip(bar.display);
	wl_display_roundtrip(bar.display);

	if(!bar.docked) {
		fprintf(stderr, "[bar] docked event not received\n");
		return 1;
	}

	swc_panel_set_y_offset(bar.panel, 0);
	wl_display_roundtrip(bar.display);

	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if(tfd >= 0) {
		its.it_interval.tv_sec = 60;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 60;
		its.it_value.tv_nsec = 0;
		timerfd_settime(tfd, 0, &its, NULL);
	}

	ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if(ifd >= 0) {
		inotify_add_watch(ifd, bar.ipc_dir, IN_CLOSE_WRITE | IN_MOVED_TO);
	}

	wfd = wl_display_get_fd(bar.display);

	update_all();
	render();

	while(running) {
		FD_ZERO(&rfds);
		FD_SET(wfd, &rfds);
		if(tfd >= 0) FD_SET(tfd, &rfds);
		if(ifd >= 0) FD_SET(ifd, &rfds);
		maxfd = (tfd > wfd) ? tfd : wfd;
		if(ifd > maxfd) maxfd = ifd;

		r = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if(r < 0) {
			if(errno == EINTR) continue;
			break;
		}

		if(tfd >= 0 && FD_ISSET(tfd, &rfds)) {
			read(tfd, &exp, sizeof(exp));
			update_cpu();
			update_ram();
			update_time();
			render();
		}

		if(ifd >= 0 && FD_ISSET(ifd, &rfds)) {
			while(read(ifd, evbuf, sizeof(evbuf)) > 0)
				;
			update_workspace();
			update_title();
			render();
		}

		if(FD_ISSET(wfd, &rfds)) {
			if(wl_display_dispatch(bar.display) < 0) break;
		}

		wl_display_flush(bar.display);
	}

	if(tfd >= 0) close(tfd);
	if(ifd >= 0) close(ifd);

	if(bar.font) wld_font_close(bar.font);
	if(bar.font_ctx) wld_font_destroy_context(bar.font_ctx);
	if(bar.renderer) wld_destroy_renderer(bar.renderer);

	buf_free(&bar.bufs[0]);
	buf_free(&bar.bufs[1]);

	if(bar.panel) swc_panel_destroy(bar.panel);
	if(bar.surface) wl_surface_destroy(bar.surface);
	if(bar.panel_manager) swc_panel_manager_destroy(bar.panel_manager);
	if(bar.shm) wl_shm_destroy(bar.shm);
	if(bar.compositor) wl_compositor_destroy(bar.compositor);
	if(bar.registry) wl_registry_destroy(bar.registry);
	wl_display_disconnect(bar.display);

	return 0;
}
