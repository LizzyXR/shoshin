/* every .c file in src/ includes this; it pulls in everything needed so individual files don't have to manage their own includes */

#ifndef SHOSHIN_H
#define SHOSHIN_H

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server.h>

#ifdef __linux__
#include <linux/input-event-codes.h>
#else
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#endif

#include <swc.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "config.h"
#include "nein_cursor.h"

/* types */
typedef enum {
	MODE_NONE,
	MODE_KILL,
	MODE_SCROLL,
	MODE_MOVE,
	MODE_RESIZE,
	MODE_JUMP,
} chord_mode;

struct window {
	struct swc_window *swc;
	struct wl_list link;
	pid_t pid;
	struct window *spawn_parent;
	struct wl_list spawn_children;
	struct wl_list spawn_link;
	bool hidden_for_spawn;
	struct swc_rectangle saved_geometry;
	bool sticky;
	int workspace; /* 1-9, 0 = visible on all */
};

struct screen {
	struct swc_screen *swc;
	struct wl_list link;
	/*
	 * note: swc_screen has no name field; screens are identified by geometry only current_screen is set to whichever screen the cursor is on, tracked by cursor_tick()
	*/
};

/* globals */

/* compositor */
extern struct wl_display *display;
extern struct wl_event_loop *evloop;
extern struct wl_list windows;
extern struct wl_list screens;
extern struct screen *current_screen;
extern struct swc_window *focused;

/* chord/input */
extern chord_mode chord_mode_cur;
extern bool chord_left, chord_right, chord_middle;
extern bool chord_active;
extern bool chord_pending, chord_forwarded;
extern uint32_t chord_btn, chord_time;
extern struct wl_event_source *chord_timer;

/* move */
extern int32_t move_start_win_x, move_start_win_y;
extern int32_t move_start_cursor_x, move_start_cursor_y;
extern struct wl_event_source *move_scroll_timer;

/* scroll */
extern int32_t scroll_px, scroll_px_x;
extern int8_t scroll_cursor_dir;
extern bool scroll_auto;
extern struct wl_event_source *scroll_timer;

/* scroll-drag */
extern int32_t scroll_drag_x, scroll_drag_y;
extern struct wl_event_source *scroll_drag_timer;

/* zoom */
extern float zoom_target;
extern struct wl_event_source *zoom_timer;

/* selection box */
extern bool sel_active;
extern int32_t sel_start_x, sel_start_y;
extern int32_t sel_cur_x, sel_cur_y;
extern struct wl_event_source *sel_timer;

/* spawn */
extern bool spawn_pending;
extern struct swc_rectangle spawn_geometry;

/* cursor-screen tracking */
extern struct wl_event_source *cursor_timer;

/* workspace */
extern int current_workspace;

/* bar child pid */
extern pid_t bar_pid;

/* function declarations */

/* window.c */
void focus_window(struct swc_window *swc, const char *reason);
bool is_visible(struct swc_window *w, struct screen *screen);
bool is_on_screen(struct swc_rectangle *w, struct screen *screen);
bool is_acme(const struct swc_window *swc);
struct screen *primary_screen(void);
void newwindow(struct swc_window *swc);
void newscreen(struct swc_screen *swc);
void newdevice(struct libinput_device *dev);

/* input.c */
void button(void *data, uint32_t time, uint32_t b, uint32_t state);
void axis(void *data, uint32_t time, uint32_t ax, int32_t value120);
bool cursor_position(int32_t *x, int32_t *y);
bool cursor_position_raw(int32_t *x, int32_t *y);
int  cursor_tick(void *data);
void click_cancel(void);

/* scroll.c */
void scroll_stop(void);
int  scroll_tick(void *data);
int  scroll_drag_tick(void *data);
int  move_scroll_tick(void *data);

/* select.c */
void update_mode_cursor(void);
void stop_select(void);
int  select_tick(void *data);
void spawn_term_select(const struct swc_rectangle *geometry);

/* zoom.c */
int zoom_tick(void *data);

/* workspace.c */
void workspace_switch(void *data, uint32_t time, uint32_t key, uint32_t state);
void workspace_move_window(void *data, uint32_t time, uint32_t key, uint32_t state);
void workspace_apply(void);

/* ipc.c */
void ipc_init(void);
void ipc_write_workspace(int ws);
void ipc_write_title(const char *title);

/* bar.c */
int bar_main(void);

#endif /* SHOSHIN_H */
