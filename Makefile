# Pattern rules and $(@D) are GNU make extensions; the upstream .POSIX: Makefile
# was dropped when the tree moved to src/ include/ external/.
.SUFFIXES:

include config.mk

# Layout
SRCDIR   = src
INCDIR   = include
EXTDIR   = external
BUILDDIR = build
GENDIR   = $(BUILDDIR)/protocols

# flags for compiling
DWLCPPFLAGS = -I. -I$(INCDIR) -I$(INCDIR)/systray -I$(EXTDIR) -I$(GENDIR) \
	-DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND) $(BACKGROUND) $(NOTIFY)
DWLDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput pixman-1 fcft dbus-1 $(XLIBS) $(BGLIBS)
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

# Sources. The systray and its dbus glue come from the bar-systray patch.
SRC = $(SRCDIR)/dwl.c $(SRCDIR)/util.c $(SRCDIR)/dbus.c \
	$(SRCDIR)/systray/watcher.c $(SRCDIR)/systray/tray.c \
	$(SRCDIR)/systray/item.c $(SRCDIR)/systray/icon.c \
	$(SRCDIR)/systray/menu.c $(SRCDIR)/systray/helpers.c
HDR = $(INCDIR)/client.h $(INCDIR)/util.h $(INCDIR)/dbus.h \
	$(INCDIR)/systray/watcher.h $(INCDIR)/systray/tray.h \
	$(INCDIR)/systray/item.h $(INCDIR)/systray/icon.h \
	$(INCDIR)/systray/menu.h $(INCDIR)/systray/helpers.h \
	$(EXTDIR)/drwl.h
ifneq ($(NOTIFY),)
SRC += $(SRCDIR)/notify.c
HDR += $(INCDIR)/notify.h
endif
OBJ = $(SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

GENHDR = $(GENDIR)/cursor-shape-v1-protocol.h \
	$(GENDIR)/ext-image-copy-capture-v1-protocol.h \
	$(GENDIR)/pointer-constraints-unstable-v1-protocol.h \
	$(GENDIR)/wlr-layer-shell-unstable-v1-protocol.h \
	$(GENDIR)/wlr-output-power-management-unstable-v1-protocol.h \
	$(GENDIR)/xdg-shell-protocol.h

.PHONY: all clean dist install uninstall remove format format-check test

all: dwl

dwl: $(OBJ)
	$(CC) $(OBJ) $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

# Every object waits on the generated headers: which of them a given source
# needs is not worth tracking, and they are cheap to produce.
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(HDR) $(GENHDR) config.h config.mk
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -c $< -o $@

$(GENDIR)/cursor-shape-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
$(GENDIR)/ext-image-copy-capture-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml $@
$(GENDIR)/pointer-constraints-unstable-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
$(GENDIR)/wlr-layer-shell-unstable-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
$(GENDIR)/wlr-output-power-management-unstable-v1-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
$(GENDIR)/xdg-shell-protocol.h:
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

config.h:
	cp config.def.h $@

# ./configure writes this file; without it the defaults are used as-is.
config.mk:
	cp config.def.mk $@

# Formatting, per .clang-format. external/ is vendored and config*.h are
# alignment-sensitive tables, so neither is reformatted.
FMT_SRC = $(SRCDIR)/dwl.c $(SRCDIR)/util.c $(SRCDIR)/dbus.c $(SRCDIR)/notify.c \
	$(SRCDIR)/systray/watcher.c $(SRCDIR)/systray/tray.c \
	$(SRCDIR)/systray/item.c $(SRCDIR)/systray/icon.c \
	$(SRCDIR)/systray/menu.c $(SRCDIR)/systray/helpers.c \
	$(INCDIR)/client.h $(INCDIR)/util.h $(INCDIR)/dbus.h $(INCDIR)/notify.h \
	$(INCDIR)/systray/watcher.h $(INCDIR)/systray/tray.h \
	$(INCDIR)/systray/item.h $(INCDIR)/systray/icon.h \
	$(INCDIR)/systray/menu.h $(INCDIR)/systray/helpers.h

format:
	clang-format -i $(FMT_SRC)

format-check:
	@for f in $(FMT_SRC); do \
		clang-format "$$f" | diff -u - "$$f" || \
			{ echo "Wrong format in $$f, run 'make format'" >&2; exit 1; }; \
	done

clean:
	rm -rf dwl $(BUILDDIR)

dist: clean
	mkdir -p dwl-$(VERSION)
	cp -R LICENSE license Makefile configure config_gen CHANGELOG.md README.md config.def.h \
		config.def.mk .clang-format src include external protocols docs \
		scripts share dwl-$(VERSION)
	tar -caf dwl-$(VERSION).tar.gz dwl-$(VERSION)
	rm -rf dwl-$(VERSION)

install: dwl
	mkdir -p $(BINDIR)
	cp -f dwl scripts/start-dwl scripts/dwl-status.sh $(BINDIR)
	chmod 755 $(BINDIR)/dwl $(BINDIR)/start-dwl $(BINDIR)/dwl-status.sh
uninstall remove:
	rm -f $(BINDIR)/dwl $(BINDIR)/start-dwl $(BINDIR)/dwl-status.sh
