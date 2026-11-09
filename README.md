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
**[<kbd> <br> Install <br> </kbd>][Install]** 
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

## Install

Each [release][Release] has x86_64 packages with wlroots and libscfg built in:

| System | File | Install it with |
| --- | --- | --- |
| Ubuntu 26.04 and newer, Debian testing | `g0wm_<version>_amd64.deb` | `sudo apt install ./g0wm_*_amd64.deb` |
| Fedora 44 and newer | `g0wm-<version>-1.x86_64.rpm` | `sudo dnf install ./g0wm-*.x86_64.rpm` |
| openSUSE Tumbleweed | `g0wm-<version>-1.x86_64.rpm` | `sudo zypper install --allow-unsigned-rpm ./g0wm-*.x86_64.rpm` |
| Other recent distributions | `g0wm-<version>-linux-x86_64.tar.gz` | `sudo tar -xzf g0wm-*.tar.gz -C /` |

The tarball does not check dependencies. Debian 13, Ubuntu 24.04 and Fedora 43
are too old for wlroots 0.20. Elsewhere, build from source.

To check the download:

```sh
sha256sum -c --ignore-missing SHA256SUMS
```

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

Or let `./configure` download them, build them as static libraries and link
them into the binary, with no rpath. It needs git, meson and ninja:

```sh
./configure --bundle-deps
```

See `./configure --help` for the other options (toolchain, `--debug`, `--native`).

## Configure & Run

- **Config:** `~/.config/g0wm/config`, written by `make install` and read at startup. `g0wm -c` writes it if missing.
- **Status text:** read from standard input, one line at a time. No status script ships with g0wm.
- **Session:** the packages put `g0wm.desktop` in `/usr/share/wayland-sessions/`, where display managers look. `make install` puts it in `~/.local/share/wayland-sessions/`, which most of them ignore: copy `share/g0wm.desktop` to `/usr/share/wayland-sessions/`, or just run `g0wm` from a VT.

## License

**GPL-3.0-or-later**, see [`LICENSE`](LICENSE). Parts taken from dwl and other
projects keep their original licenses. The project follows [REUSE](https://reuse.software):
each file states its license, the texts are in [`LICENSES/`](LICENSES) and
[`REUSE.toml`](REUSE.toml) covers the files that cannot carry a header. Where
each part comes from, and the thanks owed to the patch authors, is in
[docs/credits.md](docs/credits.md).

[Features]: docs/features.md
[Install]: #install
[Build]: #build
[Configure]: docs/settings.md
[Release]: https://github.com/frapank/G0wm/releases/latest
