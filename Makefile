# Pattern rules and $(@D) are GNU make extensions; the upstream .POSIX: Makefile
# was dropped when the tree moved to src/ include/ external/.
.SUFFIXES:
.DELETE_ON_ERROR:

include config.mk

# expand once, not once per command
VERSION  := $(VERSION)
WLR_INCS := $(WLR_INCS)
WLR_LIBS := $(WLR_LIBS)

# make V=1 shows the full commands
Q = $(if $(V),,@)

# Layout
SRCDIR   = src
INCDIR   = include
EXTDIR   = external
BUILDDIR = build
GENDIR   = $(BUILDDIR)/protocols

# MSG ( Messages )
MESS			:= printf
RESET       	:= \033[0m
RED         	:= \033[31m
GREEN       	:= \033[32m
YELLOW      	:= \033[33m
MAGENTA     	:= \033[35m
CYAN        	:= \033[36m

ifneq ($(filter-out dumb,$(TERM)),)
  ifneq (, $(shell command -v tput 2>/dev/null))
    RESET  := $(shell tput sgr0)
    RED    := $(shell tput setaf 1)
    GREEN  := $(shell tput setaf 2)
    YELLOW := $(shell tput setaf 3)
    MAGENTA:= $(shell tput setaf 5)
    CYAN   := $(shell tput setaf 6)
  endif
endif
export MESS RESET RED GREEN YELLOW MAGENTA CYAN

# flags for compiling
G0WMCPPFLAGS = -I$(INCDIR) -I$(INCDIR)/systray -I$(EXTDIR) -I$(GENDIR) \
	-DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND) $(BACKGROUND) -MMD -MP
G0WMDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput pixman-1 fcft dbus-1 gdk-pixbuf-2.0 \
	$(XLIBS)
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS))
G0WMCFLAGS = $(PKG_CFLAGS) $(WLR_INCS) $(G0WMCPPFLAGS) $(G0WMDEVCFLAGS) $(CFLAGS)
LDLIBS    = $(PKG_LIBS) $(WLR_LIBS) -lm $(LIBS)

# Sources. The systray and its dbus glue come from the bar-systray patch.
SRC = $(SRCDIR)/g0wm.c $(SRCDIR)/bar.c $(SRCDIR)/buffer.c $(SRCDIR)/client.c \
	$(SRCDIR)/corner.c $(SRCDIR)/input.c $(SRCDIR)/layout.c $(SRCDIR)/lock.c \
	$(SRCDIR)/monitor.c $(SRCDIR)/opacity.c \
	$(SRCDIR)/util.c $(SRCDIR)/dbus.c $(SRCDIR)/settings.c \
	$(SRCDIR)/notify.c $(SRCDIR)/runner.c $(SRCDIR)/toplevel.c \
	$(SRCDIR)/traypopup.c \
	$(SRCDIR)/systray/watcher.c $(SRCDIR)/systray/tray.c \
	$(SRCDIR)/systray/item.c $(SRCDIR)/systray/icon.c \
	$(SRCDIR)/systray/menu.c $(SRCDIR)/systray/helpers.c
ifneq ($(XWAYLAND),)
SRC += $(SRCDIR)/xwayland.c
endif
OBJ = $(SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Vendored third-party sources, built without the dev warning flags.
EXTSRC = $(EXTDIR)/cJSON.c
EXTOBJ = $(EXTSRC:$(EXTDIR)/%.c=$(BUILDDIR)/external/%.o)

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)

ENUMHDR = cursor-shape-v1 ext-image-copy-capture-v1 \
	pointer-constraints-unstable-v1 wlr-layer-shell-unstable-v1
SERVERHDR = wlr-output-power-management-unstable-v1 xdg-shell
GENHDR = $(patsubst %,$(GENDIR)/%-protocol.h,$(ENUMHDR) $(SERVERHDR))

# local protocols/ first
vpath %.xml protocols \
	$(WAYLAND_PROTOCOLS)/stable/xdg-shell \
	$(WAYLAND_PROTOCOLS)/staging/cursor-shape \
	$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture \
	$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints

.PHONY: all clean dist install uninstall remove format format-check test FORCE

all: g0wm

g0wm: $(OBJ) $(EXTOBJ)
	@$(MESS) '[$(GREEN)LINKER$(RESET)] %s\n' 'Linking $@'
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) $(EXTOBJ) $(LDLIBS) -o $@

# header deps come from the .d files, the generated headers just need to exist
$(BUILDDIR)/%.o: $(SRCDIR)/%.c config.mk | $(GENHDR)
	@$(MESS) '[$(GREEN)COMPILER$(RESET)] %s\n' 'Compiling $@'
	@mkdir -p $(@D)
	$(Q)$(CC) $(CPPFLAGS) $(G0WMCFLAGS) -c $< -o $@

$(BUILDDIR)/external/%.o: $(EXTDIR)/%.c config.mk
	@$(MESS) '[$(GREEN)COMPILER$(RESET)] %s\n' 'Compiling $@'
	@mkdir -p $(@D)
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d) $(EXTOBJ:.o=.d)

# rewritten only when the version changes
$(BUILDDIR)/version: FORCE
	@mkdir -p $(@D)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' >$@

$(BUILDDIR)/g0wm.o $(BUILDDIR)/monitor.o: $(BUILDDIR)/version

$(ENUMHDR:%=$(GENDIR)/%-protocol.h): SCANMODE = enum-header
$(SERVERHDR:%=$(GENDIR)/%-protocol.h): SCANMODE = server-header

$(GENDIR)/%-protocol.h: %.xml
	@$(MESS) '[$(GREEN)SCANNER$(RESET)] %s\n' 'Generating $@'
	@mkdir -p $(@D)
	$(Q)$(WAYLAND_SCANNER) $(SCANMODE) $< $@

# ./configure writes this file; without it the defaults are used as-is.
config.mk:
	cp config.def.mk $@

# Formatting, per .clang-format. external/ is vendored and config*.h are
# alignment-sensitive tables, so neither is reformatted.
FMT_SRC = $(filter-out $(INCDIR)/config.h, \
	$(wildcard $(SRCDIR)/*.c $(SRCDIR)/systray/*.c \
	$(INCDIR)/*.h $(INCDIR)/systray/*.h))

format:
	@$(MESS) '[$(CYAN)FORMAT$(RESET)] %s\n' 'Formatting...'
	clang-format -i $(FMT_SRC)
	@$(MESS) '[$(CYAN)FORMAT$(RESET)] %s\n' 'Done!'

format-check:
	@for f in $(FMT_SRC); do \
		clang-format "$$f" | diff -u - "$$f" || \
			{ echo "Wrong format in $$f, run 'make format'" >&2; exit 1; }; \
	done

# what -c writes must pass the check startup makes, a damaged file must not
TESTCFG = $(BUILDDIR)/test-config

test: g0wm
	@rm -rf $(TESTCFG)
	@XDG_CONFIG_HOME=$(TESTCFG) ./g0wm -c >$(TESTCFG).json 2>/dev/null
	@cmp -s $(TESTCFG).json $(TESTCFG)/g0wm/settings.json || \
		{ echo 'what -c wrote is not what it reads back' >&2; exit 1; }
	@out=`XDG_CONFIG_HOME=$(TESTCFG) ./g0wm -c 2>&1 >/dev/null`; \
	case $$out in *"is missing"*|*"is not a setting"*|*"should be"*) \
		printf '%s\n' "$$out" >&2; \
		echo 'a fresh settings.json did not pass its own check' >&2; exit 1 ;; \
	esac
	@sed 's/"borderpx":\([^0-9]*\)[0-9]*/"borderpx":\17/; s/"showbar"/"shobwar"/' \
		$(TESTCFG)/g0wm/settings.json >$(TESTCFG)/t \
		&& mv $(TESTCFG)/t $(TESTCFG)/g0wm/settings.json
	@out=`XDG_CONFIG_HOME=$(TESTCFG) ./g0wm -c 2>&1 >$(TESTCFG).json`; \
	case $$out in \
	*"bar.showbar is missing"*"bar.shobwar is not a setting"*) ;; \
	*) printf '%s\n' "$$out" >&2; \
		echo 'a damaged settings.json went unreported' >&2; exit 1 ;; \
	esac
	@grep -q '"borderpx":[^0-9]*7' $(TESTCFG).json || \
		{ echo 'a setting read from the file was not applied' >&2; exit 1; }
	@$(MESS) '[$(GREEN)TEST$(RESET)] %s\n' 'settings.json round trip ok'

clean:
	@$(MESS) '[$(RED)CLEANER$(RESET)] %s\n' 'Cleaning...'
	rm -rf g0wm $(BUILDDIR)
	@$(MESS) '[$(RED)CLEANER$(RESET)] %s\n' 'Done!'

# the committed tree, not the working copy
dist:
	git archive --prefix=g0wm-$(VERSION)/ -o g0wm-$(VERSION).tar.gz HEAD

# g0wm -c writes settings.json: only the binary knows which features it has.
# not for a staged install or root, it would land in the wrong home
install: g0wm
	@$(MESS) '[$(YELLOW)INSTALL$(RESET)] %s\n' 'Starting...'
	install -Dm755 g0wm $(DESTDIR)$(BINDIR)/g0wm
	install -Dm644 docs/g0wm.1 $(DESTDIR)$(MANDIR)/man1/g0wm.1
	install -Dm644 share/g0wm.desktop \
		$(DESTDIR)$(DATADIR)/wayland-sessions/g0wm.desktop
	@[ -n "$(DESTDIR)" ] || [ "`id -u`" = 0 ] || ./g0wm -c >/dev/null
	@$(MESS) '[$(YELLOW)INSTALL$(RESET)] %s\n' 'Done!'

uninstall remove:
	@$(MESS) '[$(RED)UNINSTALL$(RESET)] %s\n' 'Removing G0wm'
	rm -f $(DESTDIR)$(BINDIR)/g0wm $(DESTDIR)$(MANDIR)/man1/g0wm.1 \
		$(DESTDIR)$(DATADIR)/wayland-sessions/g0wm.desktop
	@$(MESS) '[$(RED)UNINSTALL$(RESET)] %s\n' 'Done!'
