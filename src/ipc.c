#include "shoshin.h"

static char ipc_dir[256];

static void
ipc_path(char *buf, size_t n, const char *name)
{
	if(ipc_dir[0]) snprintf(buf, n, "%s/%s", ipc_dir, name);
	else buf[0] = '\0';
}

void
ipc_init(void)
{
	snprintf(ipc_dir, sizeof(ipc_dir), "/tmp/shoshin-%d", (int)getuid());
	mkdir(ipc_dir, 0755);
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
