.SUFFIXES:
.DELETE_ON_ERROR:
.DEFAULT_GOAL := all

# Build targets need config.mk; maintenance targets do not.
NEEDS_CONFIG := $(filter-out clean distclean dist format format-check shellcheck help,$(or $(MAKECMDGOALS),all))
ifneq ($(NEEDS_CONFIG),)
ifeq ($(wildcard config.mk),)
$(error Run ./configure before building)
endif

config.mk: configure
	@echo 'config.mk is outdated: re-run ./configure (options are at its top)' >&2
	@exit 1
endif
-include config.mk

VERSION := $(strip $(file <VERSION))

# Avoid running pkg-config for every object.
WLR_INCS := $(WLR_INCS)
WLR_LIBS := $(WLR_LIBS)

# V=1 shows the commands.
Q = $(if $(V),,@)

SRCDIR   = src
INCDIR   = include
EXTDIR   = external
BUILDDIR = build
GENDIR   = $(BUILDDIR)/protocols

G0WMCPPFLAGS = -I$(INCDIR) -isystem $(EXTDIR) -I$(GENDIR) \
	-DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND) $(BACKGROUND) -MMD -MP
G0WMDEVCFLAGS = -g -Wpedantic -Wall -Wextra \
	-Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# The bundled build gets libscfg from WLR_LIBS.
PKGS = wayland-server xkbcommon libinput pixman-1 fcft dbus-1 gdk-pixbuf-2.0 $(XLIBS)
ifneq ($(BUNDLE_DEPS),yes)
PKGS += scfg
endif
ifneq ($(wildcard config.mk),)
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS))
endif
G0WMCFLAGS = -std=gnu23 $(PKG_CFLAGS) $(WLR_INCS) $(G0WMCPPFLAGS) $(G0WMDEVCFLAGS) $(CFLAGS)
LDLIBS    = $(PKG_LIBS) $(WLR_LIBS) -lm $(LIBS)

SOURCES = $(wildcard $(SRCDIR)/*/*.c $(SRCDIR)/*/*/*.c)
ifeq ($(XWAYLAND),)
SOURCES := $(filter-out $(SRCDIR)/window/xwayland.c,$(SOURCES))
endif
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

ifneq ($(wildcard config.mk),)
WAYLAND_SCANNER   := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
endif

ENUM_PROTOCOLS = cursor-shape-v1 ext-image-copy-capture-v1 \
	pointer-constraints-unstable-v1 wlr-layer-shell-unstable-v1
SERVER_PROTOCOLS = wlr-output-power-management-unstable-v1 xdg-shell
PROTOCOL_HEADERS = $(patsubst %,$(GENDIR)/%-protocol.h,$(ENUM_PROTOCOLS) $(SERVER_PROTOCOLS))

# Look in protocols/ first.
vpath %.xml protocols \
	$(WAYLAND_PROTOCOLS)/stable/xdg-shell \
	$(WAYLAND_PROTOCOLS)/staging/cursor-shape \
	$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture \
	$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints

.PHONY: all clean distclean dist install uninstall remove format format-check \
	shellcheck test check help FORCE

all: g0wm

g0wm: $(OBJECTS)
	@printf '  LD      %s\n' '$@'
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
ifeq ($(BUNDLE_DEPS),yes)
	$(Q)if readelf -d $@ | grep -E 'NEEDED.*lib(wlroots|scfg)|R(UN)?PATH'; then \
		echo '$@: linked to the libraries above, not bundled' >&2; rm -f $@; exit 1; \
	fi
endif

# Header dependencies come from .d files once the objects exist.
$(BUILDDIR)/%.o: $(SRCDIR)/%.c config.mk | $(PROTOCOL_HEADERS)
	@printf '  CC      %s\n' '$@'
	@mkdir -p $(@D)
	$(Q)$(CC) $(CPPFLAGS) $(G0WMCFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

# Leave the timestamp alone if the version is unchanged.
$(BUILDDIR)/version: FORCE
	@mkdir -p $(@D)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' >$@

$(BUILDDIR)/core/g0wm/main.o $(BUILDDIR)/output/monitor.o: $(BUILDDIR)/version

$(ENUM_PROTOCOLS:%=$(GENDIR)/%-protocol.h): SCAN_MODE = enum-header
$(SERVER_PROTOCOLS:%=$(GENDIR)/%-protocol.h): SCAN_MODE = server-header

$(GENDIR)/%-protocol.h: %.xml
	@printf '  SCAN    %s\n' '$@'
	@mkdir -p $(@D)
	$(Q)$(WAYLAND_SCANNER) $(SCAN_MODE) $< $@

# Keep external/ and the hand-aligned config.h out of clang-format.
FORMAT_FILES = $(filter-out $(SRCDIR)/core/settings/config.h, \
	$(wildcard $(SRCDIR)/*/*.c $(SRCDIR)/*/*/*.c $(SRCDIR)/*/*/*.h \
	$(INCDIR)/*/*.h $(INCDIR)/*/*/*.h))

format:
	@printf '  FORMAT  sources\n'
	$(Q)clang-format -i $(FORMAT_FILES)

format-check:
	@printf '  CHECK   source formatting\n'
	$(Q)for f in $(FORMAT_FILES); do \
		clang-format "$$f" | diff -u - "$$f" || \
			{ echo "Wrong format in $$f, run 'make format'" >&2; exit 1; }; \
	done

test: g0wm
	$(Q)sh tests/config.sh ./g0wm $(BUILDDIR)
	@printf '  TEST    config round trip ok\n'

shellcheck:
	@printf '  CHECK   shell scripts\n'
	$(Q)shellcheck -S warning configure packaging/bundle-deps tests/config.sh

check: test format-check shellcheck

help:
	@printf '%s\n' \
		'Run ./configure before building.' \
		'' \
		'  make                Build g0wm' \
		'  make install        Install g0wm, manual and session file' \
		'  make uninstall      Remove installed files' \
		'  make test           Test configuration handling' \
		'  make check          Run tests, format check and ShellCheck' \
		'  make format         Format project sources' \
		'  make clean          Remove build output' \
		'  make distclean      Also remove configuration and bundled dependencies' \
		'  make dist           Archive committed sources' \
		'  make packages       Build available binary package formats' \
		'' \
		'Use V=1 to show full commands; ./configure --help lists build options.'

clean:
	@printf '  CLEAN   %s\n' 'clean'
	$(Q)rm -rf g0wm $(BUILDDIR)

distclean: clean
	@printf '  CLEAN   %s\n' 'distclean'
	$(Q)rm -rf subprojects config.mk

# Archive HEAD, not the working tree.
dist:
	@printf '  DIST    %s\n' 'g0wm-$(VERSION).tar.gz'
	$(Q)git archive --prefix=g0wm-$(VERSION)/ -o g0wm-$(VERSION).tar.gz HEAD

# Do not write settings into root's home or the package staging directory.
install: g0wm
	@printf '  INSTALL %s\n' 'g0wm'
	$(Q)install -Dm755 g0wm $(DESTDIR)$(BINDIR)/g0wm
	$(Q)install -Dm644 docs/g0wm.1 $(DESTDIR)$(MANDIR)/man1/g0wm.1
	$(Q)install -Dm644 share/g0wm.desktop \
		$(DESTDIR)$(DATADIR)/wayland-sessions/g0wm.desktop
	@[ -n "$(DESTDIR)" ] || [ "`id -u`" = 0 ] || ./g0wm -c >/dev/null

uninstall remove:
	@printf '  REMOVE  %s\n' 'g0wm'
	$(Q)rm -f $(DESTDIR)$(BINDIR)/g0wm $(DESTDIR)$(MANDIR)/man1/g0wm.1 \
		$(DESTDIR)$(DATADIR)/wayland-sessions/g0wm.desktop

include packaging/packages.mk
