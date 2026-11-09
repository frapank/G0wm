<div align="center">
  <img src=".github/assets/logo.png" alt="G0wm Logo" width="120"/>

# G0 Window Manager

**The Wayland compositor that wants to be a desktop environment**

[<kbd>Features</kbd>][Features] ·
[<kbd>Install</kbd>][Install] ·
[<kbd>Building</kbd>][Build] ·
[<kbd>Troubleshooting</kbd>][Troubleshooting] ·
[<kbd>Configure</kbd>][Configure] ·
[<kbd>Release</kbd>][Release]

</div>


----


G0wm is a Wayland compositor that tries to be a complete desktop environment.
A lot of features are built in, so you need fewer external programs, and there
are some privacy options too. It started as a fork of [dwl](https://codeberg.org/dwl/dwl),
but most of it has changed since.

**Privacy:** you can hide windows with a keybind. They stay visible to you, but when
you share your screen (on Discord, for example) the others can't see their content
or title. Same goes for notifications.

**Bar:** the built-in bar handles workspaces, status, notifications and the runner.
The status text comes from a script, like in other compositors. The runner opens
with a keybind and launches apps from their `.desktop` files. Prefixes change what
it does: search the web, do calculations or run a binary directly in a terminal.
Notifications support images, urgency and history.

**Title bars & tabbed:** title bars are on by default. Besides looking nice, they're
what the tabbed mode uses, like in sway and i3.

**Looks:** you can configure borders borders, background, style, window colors,
blur and more.

More in [docs/features.md](docs/features.md).

## Install

Grab a package from the [latest release][Release] (x86_64, wlroots and libscfg included):

```sh
sudo apt install ./g0wm_*_amd64.deb                            # Ubuntu 26.04+, Debian testing
sudo dnf install ./g0wm-*.x86_64.rpm                           # Fedora 44+
sudo zypper install --allow-unsigned-rpm ./g0wm-*.x86_64.rpm   # openSUSE Tumbleweed
sudo tar -xzf g0wm-*.tar.gz -C /                               # others, no dependency check
```

If your distro is too old for the packages, see [Building][Build].
To check the download: `sha256sum -c --ignore-missing SHA256SUMS`.

## Build

Needs a C23 compiler and wlroots 0.20. With the dependencies installed:

```sh
./configure && make
make install                                    # installs to ~/.local/bin
```

More on dependencies and build options in [Building][Build].

## Configure & Run

The config lives in `~/.config/g0wm/config` ([all settings](docs/settings.md)).
`make install` creates it, or run `g0wm -c` to write a default one.

Start g0wm from your display manager or a TTY. The status text is read from stdin:

```sh
your-status-script | g0wm
```

If the session doesn't show up or something won't start, see
[Troubleshooting][Troubleshooting].

## Screenshots

<details>
<summary>Show screenshots</summary>

![g0wm in the tiling layout](.github/assets/windows.png)
![g0wm in the tabbed layout](.github/assets/tabbed.png)

</details>

## License

GPL-3.0-or-later, see [`LICENSE`](LICENSE). Code from dwl and other projects keeps
its original license (every file says which, following [REUSE](https://reuse.software)).
Credits and patch authors are in [docs/credits.md](docs/credits.md).

[Features]: docs/features.md
[Install]: #install
[Build]: docs/building.md
[Troubleshooting]: docs/troubleshooting.md
[Configure]: docs/settings.md
[Release]: https://github.com/frapank/G0wm/releases/latest
