.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
DWLDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput pixman-1 fcft dbus-1 $(XLIBS)
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

.PHONY: all clean dist install uninstall remove format format-check

TRAYOBJS = systray/watcher.o systray/tray.o systray/item.o systray/icon.o \
	systray/menu.o systray/helpers.o
TRAYDEPS = systray/watcher.h systray/tray.h systray/item.h systray/icon.h \
	systray/menu.h systray/helpers.h

all: dwl
dwl: dwl.o util.o dbus.o $(TRAYOBJS)
	$(CC) dwl.o util.o dbus.o $(TRAYOBJS) $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
dwl.o: dwl.c client.h dbus.h config.h config.mk cursor-shape-v1-protocol.h \
	ext-image-copy-capture-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	$(TRAYDEPS)
util.o: util.c util.h
dbus.o: dbus.c dbus.h
systray/watcher.o: systray/watcher.c $(TRAYDEPS)
systray/tray.o: systray/tray.c $(TRAYDEPS)
systray/item.o: systray/item.c $(TRAYDEPS)
systray/icon.o: systray/icon.c $(TRAYDEPS)
systray/menu.o: systray/menu.c $(TRAYDEPS)
systray/helpers.o: systray/helpers.c $(TRAYDEPS)

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
ext-image-copy-capture-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

config.h:
	cp config.def.h $@

# Formatting, per .clang-format. drwl.h is vendored and config*.h are
# alignment-sensitive tables, so neither is reformatted.
FMT_SRC = dwl.c client.h util.c util.h

format:
	clang-format -i $(FMT_SRC)

format-check:
	@for f in $(FMT_SRC); do \
		clang-format "$$f" | diff -u - "$$f" || \
			{ echo "Wrong format in $$f, run 'make format'" >&2; exit 1; }; \
	done

clean:
	rm -f dwl *.o *-protocol.h systray/*.o

dist: clean
	mkdir -p dwl-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md client.h config.def.h \
		config.mk protocols dwl.1 dwl.c drwl.h util.c util.h dwl.desktop \
		dbus.c dbus.h systray \
		start-dwl dwl-status.sh \
		dwl-$(VERSION)
	tar -caf dwl-$(VERSION).tar.gz dwl-$(VERSION)
	rm -rf dwl-$(VERSION)

BINDIR = $(HOME)/.local/bin

install: dwl
	mkdir -p $(BINDIR)
	cp -f dwl start-dwl dwl-status.sh $(BINDIR)
	chmod 755 $(BINDIR)/dwl $(BINDIR)/start-dwl $(BINDIR)/dwl-status.sh
uninstall remove:
	rm -f $(BINDIR)/dwl $(BINDIR)/start-dwl $(BINDIR)/dwl-status.sh

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $<
