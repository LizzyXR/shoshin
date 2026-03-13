#include "shoshin.h"

static void
ipc_path(char *buf, size_t n, const char *name)
{
	const char *home = getenv("HOME");
	if(home) snprintf(buf, n, "%s/.config/shoshin/%s", home, name);
	else buf[0] = '\0';
}

void
ipc_init(void)
{
	const char *home = getenv("HOME");
	char dir[512];
	if(home) {
		snprintf(dir, sizeof(dir), "%s/.config/shoshin", home);
		mkdir(dir, 0755);
	}
}

void
ipc_write_workspace(int ws)
{
	char path[512];
	FILE *f;
	ipc_path(path, sizeof(path), "workspace");
	if(!path[0]) return;
	f = fopen(path, "w");
	if(!f) return;
	fprintf(f, "%d\n", ws);
	fclose(f);
}

void
ipc_write_title(const char *title)
{
	char path[512];
	FILE *f;
	ipc_path(path, sizeof(path), "focused_title");
	if(!path[0]) return;
	f = fopen(path, "w");
	if(!f) return;
	fprintf(f, "%s\n", title ? title : "");
	fclose(f);
}
