#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cfg cfg;

static void
cfg_defaults(void)
{
	memset(&cfg, 0, sizeof(cfg));

	cfg.background_color = 0xff111111;
	cfg.outer_border_color_inactive = 0xffffffea;
	cfg.inner_border_color_inactive = 0xffddbd8c;
	cfg.outer_border_color_active = 0xffffffea;
	cfg.inner_border_color_active = 0xffc99043;
	cfg.outer_border_width = 4;
	cfg.inner_border_width = 4;
	cfg.select_box_color = 0xffffffff;
	cfg.select_box_border = 2;

	strncpy(cfg.cursor_theme, "nein", MAX_STR - 1);
	strncpy(cfg.term, "st-wl", MAX_STR - 1);
	strncpy(cfg.term_flag, "-w", MAX_STR - 1);
	strncpy(cfg.select_term_app_id, "st-wl-256color", MAX_STR - 1);

	cfg.enable_terminal_spawning = true;
	strncpy(cfg.terminal_app_ids[0], "havoc", MAX_STR - 1);
	strncpy(cfg.terminal_app_ids[1], "st-wl", MAX_STR - 1);
	strncpy(cfg.terminal_app_ids[2], "alacritty", MAX_STR - 1);

	cfg.targethz = 60;
	cfg.timerms = 1000 / cfg.targethz;
	cfg.chord_click_timeout_ms = 250;

	cfg.move_scroll_edge_threshold = 80;
	cfg.move_scroll_speed = 16;
	cfg.move_ease_factor = 0.30f;

	cfg.scrollpx = 64;
	cfg.scrollease = 4;
	cfg.scrollcap = 64;
	cfg.scroll_drag_mode = true;

	cfg.enable_zoom = true;
	cfg.focus_center = true;
	cfg.chord21 = CHORD21_JUMP;

	cfg.bar_height = 20;
	cfg.bar_bg = 0xff1a1a1a;
	cfg.bar_fg = 0xffcccccc;
	cfg.bar_ws_active_bg = 0xff4488cc;
	cfg.bar_ws_active_fg = 0xffffffff;
	strncpy(cfg.bar_font, "monospace:size=9", MAX_STR - 1);
}

static char *
trim(char *s)
{
	char *e;
	while(isspace((unsigned char)*s)) s++;
	e = s + strlen(s);
	while(e > s && isspace((unsigned char)*(e - 1))) e--;
	*e = '\0';
	return s;
}

static bool
parse_bool(const char *v)
{
	return strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "yes") == 0;
}

static uint32_t
parse_hex(const char *v)
{
	return (uint32_t)strtoul(v, NULL, 16);
}

void
cfg_load(const char *path)
{
	FILE *f;
	char line[512];
	char *eq, *key, *val, *comment;
	int term_idx = 0;

	cfg_defaults();

	f = fopen(path, "r");
	if(!f) {
		fprintf(stderr, "[cfg] no config at %s, using defaults\n", path);
		return;
	}

	while(fgets(line, sizeof(line), f)) {
		char *s = trim(line);
		if(*s == '#' || *s == '\0') continue;

		eq = strchr(s, '=');
		if(!eq) continue;
		*eq = '\0';
		key = trim(s);
		val = trim(eq + 1);

		comment = strchr(val, '#');
		if(comment) {
			*comment = '\0';
			val = trim(val);
		}

		if(!strcmp(key, "background_color")) cfg.background_color = parse_hex(val);
		else if(!strcmp(key, "outer_border_color_inactive")) cfg.outer_border_color_inactive = parse_hex(val);
		else if(!strcmp(key, "inner_border_color_inactive")) cfg.inner_border_color_inactive = parse_hex(val);
		else if(!strcmp(key, "outer_border_color_active")) cfg.outer_border_color_active = parse_hex(val);
		else if(!strcmp(key, "inner_border_color_active")) cfg.inner_border_color_active = parse_hex(val);
		else if(!strcmp(key, "select_box_color")) cfg.select_box_color = parse_hex(val);
		else if(!strcmp(key, "select_box_border")) cfg.select_box_border = (uint32_t)atoi(val);
		else if(!strcmp(key, "outer_border_width")) cfg.outer_border_width = (uint32_t)atoi(val);
		else if(!strcmp(key, "inner_border_width")) cfg.inner_border_width = (uint32_t)atoi(val);
		else if(!strcmp(key, "cursor_theme")) strncpy(cfg.cursor_theme, val, MAX_STR - 1);
		else if(!strcmp(key, "term")) strncpy(cfg.term, val, MAX_STR - 1);
		else if(!strcmp(key, "term_flag")) strncpy(cfg.term_flag, val, MAX_STR - 1);
		else if(!strcmp(key, "select_term_app_id")) strncpy(cfg.select_term_app_id, val, MAX_STR - 1);
		else if(!strcmp(key, "enable_terminal_spawning")) cfg.enable_terminal_spawning = parse_bool(val);
		else if(!strcmp(key, "terminal_app_id")) {
			if(term_idx < MAX_TERM_IDS - 1) {
				strncpy(cfg.terminal_app_ids[term_idx++], val, MAX_STR - 1);
				cfg.terminal_app_ids[term_idx][0] = '\0';
			}
		}
		else if(!strcmp(key, "targethz")) {
			cfg.targethz = atoi(val);
			if(cfg.targethz < 1) cfg.targethz = 60;
			cfg.timerms = 1000 / cfg.targethz;
		}
		else if(!strcmp(key, "chord_click_timeout_ms")) cfg.chord_click_timeout_ms = atoi(val);
		else if(!strcmp(key, "move_scroll_edge_threshold")) cfg.move_scroll_edge_threshold  = atoi(val);
		else if(!strcmp(key, "move_scroll_speed")) cfg.move_scroll_speed = atoi(val);
		else if(!strcmp(key, "move_ease_factor")) cfg.move_ease_factor = (float)atof(val);
		else if(!strcmp(key, "scrollpx")) cfg.scrollpx = atoi(val);
		else if(!strcmp(key, "scrollease")) cfg.scrollease = atoi(val);
		else if(!strcmp(key, "scrollcap")) cfg.scrollcap = atoi(val);
		else if(!strcmp(key, "scroll_drag_mode")) cfg.scroll_drag_mode = parse_bool(val);
		else if(!strcmp(key, "enable_zoom")) cfg.enable_zoom = parse_bool(val);
		else if(!strcmp(key, "focus_center")) cfg.focus_center = parse_bool(val);
		else if(!strcmp(key, "chord21")) {
			if(!strcmp(val, "sticky")) cfg.chord21 = CHORD21_STICKY;
			else if(!strcmp(val, "fullscreen")) cfg.chord21 = CHORD21_FULLSCREEN;
			else cfg.chord21 = CHORD21_JUMP;
		}
		else if(!strcmp(key, "bar_height")) cfg.bar_height = atoi(val);
		else if(!strcmp(key, "bar_bg")) cfg.bar_bg = parse_hex(val);
		else if(!strcmp(key, "bar_fg")) cfg.bar_fg = parse_hex(val);
		else if(!strcmp(key, "bar_ws_active_bg")) cfg.bar_ws_active_bg = parse_hex(val);
		else if(!strcmp(key, "bar_ws_active_fg")) cfg.bar_ws_active_fg = parse_hex(val);
		else if(!strcmp(key, "bar_font")) strncpy(cfg.bar_font, val, MAX_STR - 1);
		else {
			fprintf(stderr, "[cfg] unknown key: '%s'\n", key);
		}
	}

	fclose(f);
	fprintf(stderr, "[cfg] loaded %s (%dhz = %dms/tick)\n", path, cfg.targethz, cfg.timerms);
}
