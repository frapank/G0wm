# dwl - dwm for Wayland

> [!WARNING]
> This repository is a **personal fork** of the [DWL](https://codeberg.org/dwl/dwl) project.
>
> It is **not** intended to replace DWL or present itself as a different project. It is simply my own fork containing a set of personal patches and configuration choices that match my workflow and philosophy.

dwl is a compact, hackable compositor for [Wayland] based on [wlroots]. It is
intended to fill the same space in the Wayland world that [dwm] does in X11,
primarily in terms of functionality, and secondarily in terms of
philosophy. Like [dwm], dwl is:

- Easy to understand, hack on, and extend with patches
- One C source file (or a very small number) configurable via `config.h`
- Tied to as few external dependencies as possible

## Building dwl

This fork only has one branch (`main`) and tracks whatever released
[wlroots] version `config.mk` points at (currently 0.20) — there's no
`wlroots-next` tracking and no release page, unlike upstream dwl.

dwl has the following dependencies:
- libinput
- wayland
- wlroots (compiled with the libinput backend)
- xkbcommon
- fcft (bar and title bar text)
- libdbus (systray and bar notifications; set `showsystray = 0` to leave it
  unused, but it is still linked in. `./configure --no-systray` drops the tray
  sources from the build entirely)
- wayland-protocols (compile-time only)
- pkg-config (compile-time only)

dwl has the following additional dependencies if XWayland support is enabled:
- libxcb
- libxcb-wm
- wlroots (compiled with X11 support)
- Xwayland (runtime only)

Install these (and their `-devel` versions if your distro has separate
development packages) and run `make`.

To enable XWayland, you should uncomment its flags in `config.mk`.

### Repository layout

Upstream dwl keeps everything in one flat directory. This fork uses a
conventional tree instead:

```
config.def.h     user configuration, copied to config.h on first build
src/             dwl.c, util.c, and the systray sources
include/         client.h, util.h, dbus.h, systray/*.h
external/        drwl.h, vendored from the drwl project
protocols/       wlr protocol XML fed to wayland-scanner
build/           objects and generated headers (gitignored, `make clean`)
docs/            man page, features.md
scripts/         start-dwl, dwl-status.sh
share/           dwl.desktop
license/         upstream license notices (see License below)
```

`config.def.h` and `config.h` deliberately stay at the root, since they are
the files you actually edit, next to the `config_gen` and `status_gen`
generators that write them. Note that the Makefile uses GNU make pattern rules
and is no longer `.POSIX:`.

## Configuration

All configuration is done by editing `config.h` and recompiling, in the same
manner as [dwm]. There is no way to separately restart the window manager in
Wayland without restarting the entire display server, so any changes will take
effect the next time dwl is executed.

`./config_gen` writes that `config.h` for you: it walks through the settings
and takes every default from `config.def.h`, so pressing Enter at each prompt
reproduces the shipped configuration. `./config_gen -c` (or the fourth entry
of the menu it shows when a `config.h` is already there) edits the current
`config.h` instead — it becomes both the template and the source of every
default, so the values you already picked, and anything you hand-edited, are
kept. Either way the previous file is backed up next to it.

It asks in both modes for the scale of every monitor — `1` keeps the native
resolution, a higher factor makes everything bigger on a proportionally
smaller desktop — and for the opacity settings below.

### Status text

The text at the right end of the bar comes from `scripts/dwl-status.sh`, which
`start-dwl` pipes into dwl. It prints a line per second made of the modules
listed in `status.conf`, in the order they are listed there: `date`, `time`,
`battery`, `cpu`, `ram`, `netdown` and `netup`. With no config file it prints
the date, the clock and the battery, as it always did.

`./status_gen` writes that file, the way `./config_gen` writes `config.h`, to
`$XDG_CONFIG_HOME/dwl/status.conf` (`~/.config/dwl/status.conf`). It asks
which modules to show and in which order, then the format of each one — a
strftime string for the clock and the date, a template where `%v` is the value
for the others (`ram` also has `%u` for what is in use and `%t` for the total)
— showing the line the bar would draw next to every choice. `./status_gen -c`
edits the existing file, keeping its settings as the defaults, and backs up
the previous one.

It also offers Nerd Font icons in place of the `cpu`/`ram`/`down`/`up`
labels. It looks for a Nerd Font with `fc-list` first and skips them with a
warning when none is installed, since the glyphs would otherwise draw as
boxes; the advanced mode lets you replace each glyph, and the battery takes a
list of them, drawn from empty to full according to the charge.

Every module needs a source of readings the system may not have: the battery
is read from sysfs on Linux, from `sysctl` on FreeBSD and from `apm` on
OpenBSD; the cpu from `/proc/stat` or `kern.cp_time`; memory from
`/proc/meminfo` only; the network counters from sysfs, or from `netstat -ibn`
where it reports bytes. A module whose source is missing is dropped with a
warning on stderr rather than printing an empty field, and `status_gen` marks
it as unavailable while you pick.

As in the [dwm] community, we encourage users to share patches they have
created. Check out the [dwl-patches] repository!

### Used patches

These [dwl-patches] are applied on top of upstream dwl, adapted to this tree
where needed. See [docs/features.md](docs/features.md) for what each one does
here and the knobs it adds.

- **[bar]** — internal i3-like status bar, rendered with `fcft`/`pixman`.
- **[gaps]** — 3px gaps between tiled clients, toggled with `MODKEY+g`.
- **[autostart]** — runs the commands in `autostart[]` at startup and kills
  them on exit, instead of relying on the `-s` flag.
- **[cursortheme]** — configurable xcursor theme and size, also exported to
  clients.
- **[attachbottom]** — new windows go to the bottom of the stack instead of
  becoming the new master.
- **[movestack]** (by Nikita Ivanov) — moves the focused client up or down the
  stack, bound to `MODKEY+Shift+h/j/k/l`.
- **[bar-systray]** (by [janetski]) — a StatusNotifierItem tray at the right
  end of the bar, adding a `libdbus` dependency. Ported from 0.7, with fixes
  for icon sizing, tray destruction, and Qt/KDE registrations.
- **[hide-cursor-when-typing]** (by [unixchad]) — hides the cursor while you
  type, like `xbanish`.
- **[warpcursor]** (by [Ben Collerson]) — warps the cursor to the center of a
  newly focused client.
- **[client-opacity-focus]** (by [Hansvon], on top of [client-opacity]) —
  separate opacity for the focused and unfocused windows, per-client
  overridable through `rules[]` and adjustable at runtime with `MODKEY+o` and
  friends.

### Local additions

Not from [dwl-patches] — written for this tree, and covered in detail in
[docs/features.md](docs/features.md):

- **Title bars** — every window gets a title bar with its title, drawn with
  the same `drwl` code as the bar; clicking one focuses its window.
- **Tabbed layout** — an i3/sway-style `tabbed` layout on `MODKEY+t`: the
  group is laid out as a single window and the members' title bars become tabs
  in its one title row, cycled with `MODKEY+h/l`.
- **Bar notifications** (`src/notify.c`) — a minimal
  `org.freedesktop.Notifications` server that shows the most recent
  notification in the bar, click to scroll or dismiss. Toggled with
  `shownotifications`, and it stays out of the way of any other notification
  daemon.
- **Graphics tablet support** — pen tablets (tested on a Wacom Intuos S) move
  the cursor and click like a pointer. Pressure, tilt, pad buttons and the
  eraser end are not forwarded.
- **Opacity filter and global toggle** — on top of [client-opacity-focus]:
  `opacity_apps[]` limits (or excludes) the apps opacity applies to, and
  `MODKEY+Alt+o` turns it off for every window at once.

## Running dwl

dwl can be run on any of the backends supported by wlroots. This means you can
run it as a separate window inside either an X11 or Wayland session, as well as
directly from a VT console. Depending on your distro's setup, you may need to
add your user to the `video` and `input` groups before you can run dwl on a
VT. If you are using `elogind` or `systemd-logind` you need to install polkit;
otherwise you need to add yourself in the `seat` group and enable/start the
seatd daemon.

When dwl is run with no arguments, it will launch the server and begin handling
any shortcuts configured in `config.h`. There is no status bar or other
decoration initially; these are instead clients that can be run within the
Wayland session. Do note that the default background color is grey. This can be
modified in `config.h`.

If you would like to run a script or command automatically at startup, you can
specify the command using the `-s` option. This command will be executed as a
shell command using `/bin/sh -c`.  It serves a similar function to `.xinitrc`,
but differs in that the display server will not shut down when this process
terminates. Instead, dwl will send this process a SIGTERM at shutdown and wait
for it to terminate (if it hasn't already). This makes it ideal for execing into
a user service manager like [s6], [anopa], [runit], [dinit], or [`systemd
--user`].

Note: The `-s` command is run as a *child process* of dwl, which means that it
does not have the ability to affect the environment of dwl or of any processes
that it spawns. If you need to set environment variables that affect the entire
dwl session, these must be set prior to running dwl. For example, Wayland
requires a valid `XDG_RUNTIME_DIR`, which is usually set up by a session manager
such as `elogind` or `systemd-logind`.  If your system doesn't do this
automatically, you will need to configure it prior to launching `dwl`, e.g.:

    export XDG_RUNTIME_DIR=/tmp/xdg-runtime-$(id -u)
    mkdir -p $XDG_RUNTIME_DIR
    dwl

### Status information

Information about selected layouts, current window title, app-id, and
selected/occupied/urgent tags is written to the stdin of the `-s` command (see
the `STATUS INFORMATION` section in `_dwl_(1)`).  This information can be used to
populate an external status bar with a script that parses the
information. Failing to read this information will cause dwl to block, so if you
do want to run a startup command that does not consume the status information,
you can close standard input with the `<&-` shell redirection, for example:

    dwl -s 'foot --server <&-'

If your startup command is a shell script, you can achieve the same inside the
script with the line

    exec <&-

To get a list of status bars that work with dwl consult our [wiki].

### (Known) Java nonreparenting WM issue
Certain IDEs don't display correctly unless an environmental variable for Java AWT
indicates that the WM is nonreparenting.

For some Java AWT-based IDEs, such as Xilinx Vivado and Microchip MPLAB X, the
following environment variable needs to be set before running the IDE or dwl:

    export _JAVA_AWT_WM_NONREPARENTING=1

## Replacements for X applications

You can find a [list of useful resources on our wiki].

## Background

dwl is not meant to provide every feature under the sun. Instead, like [dwm], it
sticks to features which are necessary, simple, and straightforward to implement
given the base on which it is built. Implemented default features are:

- Any features provided by [dwm]/Xlib: simple window borders, tags, keybindings,
  client rules, mouse move/resize. Providing a built-in status bar is an
  exception to this goal, to avoid dependencies on font rendering and/or drawing
  libraries when an external bar could work well.
- Configurable multi-monitor layout support, including position and rotation
- Configurable HiDPI/multi-DPI support
- Idle-inhibit protocol which lets applications such as mpv disable idle
  monitoring
- Provide information to external status bars via stdout/stdin
- Urgency hints via xdg-activate protocol
- Support screen lockers via ext-session-lock-v1 protocol
- Various Wayland protocols
- XWayland support as provided by wlroots (can be enabled in `config.mk`)
- Zero flickering - Wayland users naturally expect that "every frame is perfect"
- Layer shell popups (used by Waybar)
- Damage tracking provided by scenegraph API

Given the Wayland architecture, dwl has to implement features from [dwm] **and**
the xorg-server. Because of this, it is impossible to maintain the original
project goal of 2000 SLOC and have a reasonably complete compositor with
features comparable to [dwm]. However, this does not mean that the code will grow
indiscriminately. We will try to keep the code as small as possible.

Feature *non-goals*, inherited from upstream, include:

- Client-side decoration (any more than is necessary to tell the clients not to)
- Client-initiated window management, such as move, resize, and close, which can
  be done through the compositor
- Animations and visual effects

## Acknowledgements

dwl began by extending the TinyWL example provided (CC0) by the sway/wlroots
developers. This was made possible in many cases by looking at how sway
accomplished something, then trying to do the same in as suckless a way as
possible.

Many thanks to suckless.org and the [dwm] developers and community for the
inspiration, and to the various contributors to the project, including:

- **Devin J. Pohly for creating and nurturing the fledgling project**
- Alexander Courtis for the XWayland implementation
- Guido Cella for the layer-shell protocol implementation, patch maintenance,
  and for helping to keep the project running
- Stivvo for output management and fullscreen support, and patch maintenance

## License

dwl is licensed under the GNU General Public License v3.0 or later; the full
text is in [`LICENSE`](LICENSE), and it covers this tree as a whole.

It incorporates code from three other projects, whose notices are kept in
`license/`:

| File | Project | License | What it covers |
| --- | --- | --- | --- |
| [`license/dwm.txt`](license/dwm.txt) | [dwm] | MIT/X Consortium | `src/util.c`, `include/util.h`, and the tags/layout model |
| [`license/sway.txt`](license/sway.txt) | [sway] | MIT | portions of the wlroots plumbing |
| [`license/tinywl.txt`](license/tinywl.txt) | tinywl | CC0 | the original compositor skeleton dwl grew from |


[wlroots]: https://gitlab.freedesktop.org/wlroots
[dwm]: https://dwm.suckless.org/
[sway]: https://github.com/swaywm/sway
[`systemd --user`]: https://wiki.archlinux.org/title/Systemd/User
[anopa]: https://jjacky.com/anopa/
[attachbottom]: https://codeberg.org/dwl/dwl-patches/wiki/attachbottom
[bar]: https://codeberg.org/dwl/dwl-patches/wiki/bar
[cursortheme]: https://codeberg.org/dwl/dwl-patches/wiki/cursortheme
[gaps]: https://codeberg.org/dwl/dwl-patches/wiki/gaps
[movestack]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/movestack
[bar-systray]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/bar-systray
[hide-cursor-when-typing]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/hide-cursor-when-typing
[warpcursor]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/warpcursor
[client-opacity-focus]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/client-opacity-focus
[client-opacity]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/client-opacity
[Hansvon]: https://codeberg.org/Hansvon
[unixchad]: https://codeberg.org/unixchad
[janetski]: https://codeberg.org/janetski
[Ben Collerson]: https://codeberg.org/bencc
[autostart]: https://codeberg.org/dwl/dwl-patches/wiki/autostart
[dinit]: https://davmac.org/projects/dinit/
[dwl-patches]: https://codeberg.org/dwl/dwl-patches
[list of useful resources on our wiki]: https://codeberg.org/dwl/dwl/wiki/Home#migrating-from-x
[runit]: http://smarden.org/runit/faq.html#userservices
[s6]: https://skarnet.org/software/s6/
[wiki]: https://codeberg.org/dwl/dwl/wiki/Home#compatible-status-bars
[Wayland]: https://wayland.freedesktop.org/
