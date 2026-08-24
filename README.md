<div align="center">

# G0wn

**A personal desktop environment designed to my liking**

[![C](https://img.shields.io/badge/C-99%2B-A8B9CC?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Status](https://img.shields.io/badge/status-early%20development-orange?style=flat-square)](#status)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)

</div>

## What this is

This is my own build of [dwl], kept here so I can pull it onto whatever machine
I happen to be using. It started as a fork and has changed quite a bit since
then. It is not meant to replace dwl or to pass itself off as a separate
project.

The original dwl is a minimal Wayland compositor that relies on external
programs for things like the bar, notifications and the application launcher.
g0wn handles all of these directly, which makes it closer to a small desktop
environment than to a plain compositor.

It currently includes:

- **Bar** with tags, layout symbol, status text and a [systray]
- **Title bars** for every window and an i3/sway style tabbed layout
- **Notifications** through an `org.freedesktop.Notifications` server shown in the bar
- **Runner** opened with `MODKEY+r`, with `$PATH` completion and basic arithmetic
- **Window opacity** that can be set per client or globally and changed at
  runtime, optionally as frosted glass over the wallpaper
- **Graphics tablet support** and the usual [dwl-patches]

A tour of what all of this looks like in use is in
[docs/features.md](docs/features.md), the settings are documented in the
comments of [`config.def.h`](config.def.h), and the man page is at
[`docs/g0wn.1`](docs/g0wn.1).

Screen locking, idle handling, portals and a polkit agent are not included.
If you need them, you can start programs such as `swayidle` and `swaylock`
from `autostart[]`.

## Status

g0wn is still in early development and things are likely to change.

There is currently one branch called `main` and there are no releases yet.
The project follows the [wlroots] version selected in `config.mk`, which is
currently 0.20.

The defaults are the ones that suit me rather than the most sensible ones, and
names and features move around whenever something starts to annoy me.

## Build

The required dependencies are `wlroots` 0.20 with the libinput backend,
`wayland`, `wayland-protocols`, `libinput`, `xkbcommon`, `pixman`, `fcft`,
`libdbus` and `pkg-config`.

X11 support also requires `libxcb`, `libxcb-icccm` and `Xwayland`.

`gdk-pixbuf` is only needed if you want to use the built in wallpaper support.

```sh
./configure && make
make install      # g0wn, start-g0wn and g0wn-status.sh into ~/.local/bin
```

You can also just run `make`. It will use `config.def.mk` and `config.def.h`
if no custom configuration exists yet.

Some features can be disabled during the build:

| Flag | Disables |
| --- | --- |
| `--disable-xwayland` | X11 support (`libxcb`) |
| `--no-systray` | the tray in the bar |
| `--no-notify` | the notification server |
| `--no-runner` | the bar runner, `MODKEY+r` starts `menucmd` instead |
| `--no-integrated-background` | the wallpaper renderer (`gdk-pixbuf`) |

Run `./configure --help` to see the other available options such as
toolchain settings, `--debug` and `--native`.

## Configure

Most settings are stored in `config.h` and require a rebuild after changes,
similar to [dwm].

The configuration file is divided into numbered sections. It is a good idea
to read the header at the top of [`config.def.h`](config.def.h) before changing
anything.

You can generate a new configuration with:

```sh
./config_gen        # create config.h by answering prompts
./config_gen -c     # edit the current configuration
```

The second command keeps your current values as the default answers.

The status text shown in the bar comes from `scripts/g0wn-status.sh`. Its
configuration is stored in `~/.config/g0wn/status.conf`.

You can generate it with:

```sh
./status_gen        # choose the modules, order and format
```

Both generators create a backup before replacing an existing file.

The available modules are listed in the status text section of
[docs/features.md](docs/features.md), and `status.conf` documents its own
formats in the comments `./status_gen` writes into it.

## Run

Run `start-g0wn` from a VT.

It sets up the session environment, starts PipeWire, runs the status script
and stores logs in `~/.local/state/g0wn/`.

The file `share/g0wn.desktop` can be used as a session entry for display
managers. It is not installed automatically by `make install`.

The desktop entry starts g0wn directly instead of using `start-g0wn`. If you
want to use it, copy it to `/usr/share/wayland-sessions/` and set `Exec` to
whichever startup method you prefer.

## Layout

```text
config.def.h    settings, copied to config.h on first build
configure       writes config.mk
config_gen      writes config.h
status_gen      writes status.conf

src/            g0wn.c, util.c, notify.c, dbus.c, systray/
include/        headers
external/       drwl.h, vendored from the drwl project
protocols/      wlr protocol XML for wayland-scanner
scripts/        start-g0wn, g0wn-status.sh
docs/           man page, features.md and credits.md
```

## License

g0wn is licensed under **GPL-3.0-or-later**.

The full license text is available in [`LICENSE`](LICENSE).

Some parts of the project come from dwl and from other projects and keep their
original licenses. Their license files are stored in the `license/` directory.

Where each part comes from, the patch authors and the great thanks they are
owed are collected in [docs/credits.md](docs/credits.md).

[dwl]: https://codeberg.org/dwl/dwl
[dwl-patches]: https://codeberg.org/dwl/dwl-patches
[systray]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/bar-systray
[dwm]: https://dwm.suckless.org/
[sway]: https://github.com/swaywm/sway
[wlroots]: https://gitlab.freedesktop.org/wlroots
