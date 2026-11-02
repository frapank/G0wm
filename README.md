<div align="center">
  <img src=".github/assets/logo.png" alt="G0wm Logo" width="120"/>

# G0 Window Manager

**The Wayland compositor that wants to be a desktop environment**

[![C](https://img.shields.io/badge/C-99%2B-A8B9CC?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
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

The required dependencies are: wlroots 0.20 (with the libinput backend),
wayland, wayland-protocols, libinput, xkbcommon, pixman, fcft,
libdbus, gdk-pixbuf, libscfg and pkg-config.

X11 support also requires: libxcb, libxcb-icccm and Xwayland.

```sh
./configure && make
make install         # g0wm into ~/.local/bin
```

wlroots 0.20 and [libscfg][scfg] are not packaged by every distribution. If you
installed them in their own prefix, `./configure` cannot find them on its own:
point it there and it also records an rpath in the binary, so `g0wm` finds the
libraries at run time without `LD_LIBRARY_PATH` or `ldconfig`:

```sh
./configure --wlroots-prefix=/path/to/prefix
```

Run `./configure --help` to see the other available options such as
toolchain settings, `--debug` and `--native`.

## Configure & Run

Every setting lives in `~/.config/g0wm/config`, an [scfg] file that
`make install` writes and g0wm reads at startup.

```sh
g0wm -c             # write it if it is not there
```

The status text shown in the bar is read from standard input, one line at a
time. No status script ships with g0wm.

`make install` puts `g0wm.desktop` in `~/.local/share/wayland-sessions/`. Most
display managers only look in `/usr/share/wayland-sessions/`, so copy
`share/g0wm.desktop` there and pick g0wm from the session list. You can also
just run `g0wm` from a VT.

## Roadmap

What is planned next (color management and HDR, input methods and the
on-screen keyboard, the magnifier) is listed step by step in
[`ROADMAP.md`](ROADMAP.md).

[scfg]: https://git.sr.ht/~emersion/scfg
[Features]: docs/FEATURES.md
[Build]: #build
[Configure]: docs/settings.md
[Release]: https://github.com/frapank/G0wm/releases/latest

## License

g0wm is licensed under **GPL-3.0-or-later**.

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
