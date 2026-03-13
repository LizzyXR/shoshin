#include "shoshin.h"

void
workspace_apply(void)
{
	struct window *w;
	wl_list_for_each(w, &windows, link) {
		if(!w->swc)   continue;
		if(w->sticky) continue;
		if(w->workspace == 0 || w->workspace == current_workspace)
			swc_window_show(w->swc);
		else
			swc_window_hide(w->swc);
	}
	ipc_write_workspace(current_workspace);
}

void
workspace_switch(void *data, uint32_t time, uint32_t key, uint32_t state)
{
	struct window *w;
	struct swc_window *first = NULL;
	int ws = (int)(intptr_t)data;

	(void)time;
	(void)key;

	if(state) return;
	if(ws < 1 || ws > 9 || ws == current_workspace) return;

	current_workspace = ws;
	workspace_apply();

	wl_list_for_each(w, &windows, link) {
		if(!w->swc) continue;
		if(w->workspace == 0 || w->workspace == current_workspace) {
			first = w->swc;
			break;
		}
	}
	focus_window(first, "workspace_switch");
}

void
workspace_move_window(void *data, uint32_t time, uint32_t key, uint32_t state)
{
	struct window *w;
	int ws = (int)(intptr_t)data;

	(void)time;
	(void)key;

	if(state) return;
	if(ws < 1 || ws > 9 || !focused) return;

	wl_list_for_each(w, &windows, link) {
		if(w->swc == focused) {
			w->workspace = ws;
			break;
		}
	}
	workspace_apply();
}
