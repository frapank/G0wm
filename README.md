<div align="center">
  <img src=".github/assets/logo.png" alt="G0wm Logo" width="120"/>

# G0 Window Manager

**The Wayland compositor that wants to be a desktop environment**

[![C](https://img.shields.io/badge/C-23-A8B9CC?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![wlroots](https://img.shields.io/badge/wlroots-0.20-blue?style=flat-square)](https://gitlab.freedesktop.org/wlroots/wlroots)
![Platform](https://img.shields.io/badge/platform-linux-lightgrey?style=flat-square&logo=linux&logoColor=white)
![Status](https://img.shields.io/badge/status-stable-green?style=flat-square)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)

**[<kbd> <br> Features <br> </kbd>][Features]** 
**[<kbd> <br> Build <br> </kbd>][Build]** 
**[<kbd> <br> Configure <br> </kbd>][Configure]** 
**[<kbd> <br> Release <br> </kbd>][Release]**

</div>

## What this is

g0wm is a minimal Wayland compositor built on dwl and reworked from the ground
up. It is designed to feel more like a complete desktop environment.
Its status bar includes features such as notifications, a runner, a calculator,
and a customizable status bar. It also includes the tabbed mode from i3,
with window titles and close buttons that appear on the right side of the bar
when hovered.

<p align="center">
  <img src=".github/assets/windows.png" alt="g0wm in the tiling layout" width="49%">
  <img src=".github/assets/tabbed.png" alt="g0wm in the tabbed layout" width="49%">
</p>

## Build

**Dependencies:** wlroots 0.20 (with the libinput backend), wayland,
wayland-protocols, libinput, xkbcommon, pixman, fcft, libdbus, gdk-pixbuf,
[libscfg](https://git.sr.ht/~emersion/scfg), pkg-config, a C23 compiler
(GCC 14 or Clang 18 and newer)

**X11 support:** libxcb, libxcb-icccm, Xwayland

```sh
./configure && make
make install         # g0wm into ~/.local/bin
```

wlroots 0.20 and libscfg are not packaged everywhere. If they live in their own
prefix, point `./configure` there. It also records an rpath, so no
`LD_LIBRARY_PATH` or `ldconfig` is needed:

```sh
./configure --wlroots-prefix=/path/to/prefix
```

See `./configure --help` for the other options (toolchain, `--debug`, `--native`).

## Configure & Run

- **Config:** `~/.config/g0wm/config`, written by `make install` and read at startup. `g0wm -c` writes it if missing.
- **Status text:** read from standard input, one line at a time. No status script ships with g0wm.
- **Session:** `make install` puts `g0wm.desktop` in `~/.local/share/wayland-sessions/`. Most display managers only look in `/usr/share/wayland-sessions/`, so copy `share/g0wm.desktop` there, or just run `g0wm` from a VT.

## License

**GPL-3.0-or-later**, see [`LICENSE`](LICENSE). Parts taken from dwl and other
projects keep their original licenses. The project follows [REUSE](https://reuse.software):
each file states its license, the texts are in [`LICENSES/`](LICENSES) and
[`REUSE.toml`](REUSE.toml) covers the files that cannot carry a header. Where
each part comes from, and the thanks owed to the patch authors, is in
[docs/credits.md](docs/credits.md).

[Features]: docs/features.md
[Build]: #build
[Configure]: docs/settings.md
[Release]: https://github.com/frapank/G0wm/releases/latest
