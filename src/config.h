#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
	CHORD21_STICKY = 0,
	CHORD21_FULLSCREEN = 1,
	CHORD21_JUMP = 2,
} chord21_mode;

#define MAX_TERM_IDS 16
#define MAX_STR 256

struct cfg {
	/* colors (ARGB) */
	uint32_t background_color;
	uint32_t outer_border_color_inactive;
	uint32_t inner_border_color_inactive;
	uint32_t outer_border_color_active;
	uint32_t inner_border_color_active;
	uint32_t select_box_color;
	uint32_t select_box_border;

	/* borders */
	uint32_t outer_border_width;
	uint32_t inner_border_width;

	/* cursor */
	char cursor_theme[MAX_STR];

	/* terminal */
	char term[MAX_STR];
	char term_flag[MAX_STR];
	char select_term_app_id[MAX_STR];
	bool enable_terminal_spawning;
	char terminal_app_ids[MAX_TERM_IDS][MAX_STR];

	/* timing */
	int targethz;
	int timerms;

	/* input */
	int chord_click_timeout_ms;

	/* movement */
	int32_t move_scroll_edge_threshold;
	int32_t move_scroll_speed;
	float move_ease_factor;

	/* scroll */
	int scrollpx;
	int scrollease;
	int scrollcap;
	bool scroll_drag_mode;

	/* features */
	bool enable_zoom;
	bool focus_center;

	/* 2-1 chord */
	chord21_mode chord21;

	/* bar */
	int bar_height;
	uint32_t bar_bg;
	uint32_t bar_fg;
	uint32_t bar_ws_active_bg;
	uint32_t bar_ws_active_fg;
	char bar_font[MAX_STR];
};

extern struct cfg cfg;

void cfg_load(const char *path);
void cfg_find_path(char *out, size_t n, const char *binary_path);

#endif
