_VERSION = 1.2.0
VERSION  = $(shell git describe --tags --dirty 2>/dev/null || echo $(_VERSION))

PKG_CONFIG = pkg-config

# paths
PREFIX = $(HOME)/.local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

WLR_INCS = $(shell $(PKG_CONFIG) --cflags wlroots-0.20)
WLR_LIBS = $(shell $(PKG_CONFIG) --libs wlroots-0.20)

# for a wlroots outside the system paths, see ./configure --wlroots-prefix

XWAYLAND = -DXWAYLAND
XLIBS = xcb xcb-icccm

BACKGROUND = -DINTEGRATED_BACKGROUND

# cc and not the gmake default c99, the Makefile asks for -std=gnu23
CC = cc
