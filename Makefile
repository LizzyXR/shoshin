PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

CFLAGS = -O2 -std=c99 -Wall -Wextra
CFLAGS += -Isrc
CFLAGS += -I$(PREFIX)/include
CFLAGS += `pkg-config --cflags swc wayland-server wayland-client libinput pixman-1 xkbcommon libdrm wld`

LDFLAGS = -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib
LDLIBS = `pkg-config --libs swc wayland-server wayland-client libinput pixman-1 xkbcommon libdrm libudev xcb xcb-composite xcb-ewmh xcb-icccm wld`

OBJ = \
	src/shoshin.o    \
	src/config.o     \
	src/input.o      \
	src/scroll.o     \
	src/select.o     \
	src/window.o     \
	src/workspace.o  \
	src/ipc.o        \
	src/zoom.o       \
	src/bar.o

all: shoshin

shoshin: $(OBJ)
	$(CC) $(LDFLAGS) -o shoshin $(OBJ) $(LDLIBS)

src/shoshin.o: src/shoshin.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/config.o: src/config.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/input.o: src/input.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/scroll.o: src/scroll.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/select.o: src/select.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/window.o: src/window.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/workspace.o: src/workspace.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/ipc.o: src/ipc.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/zoom.o: src/zoom.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/bar.o: src/bar.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f shoshin $(OBJ)

install: shoshin
	install -m 755 shoshin $(DESTDIR)$(BINDIR)/shoshin

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/shoshin

.PHONY: all clean install uninstall
