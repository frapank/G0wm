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
- libdbus (systray; set `showsystray = 0` to leave it unused, but it is still
  linked in)
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
docs/            man page
scripts/         start-dwl, dwl-status.sh
share/           dwl.desktop
```

`config.def.h` and `config.h` deliberately stay at the root, since they are
the files you actually edit. Note that the Makefile uses GNU make pattern
rules and is no longer `.POSIX:`.

## Configuration

All configuration is done by editing `config.h` and recompiling, in the same
manner as [dwm]. There is no way to separately restart the window manager in
Wayland without restarting the entire display server, so any changes will take
effect the next time dwl is executed.

As in the [dwm] community, we encourage users to share patches they have
created. Check out the [dwl-patches] repository!

### Used patches

This checkout has the following [dwl-patches] applied on top of upstream dwl,
adapted to this tree where needed. See `config.h`/`config.def.h` for the
resulting configuration knobs.

- **[bar]** — internal i3-like status bar (tags, layout symbol, focused
  client title, external status text), rendered with `fcft`/`pixman`.
  Configured for an always-black bar with white text and black window
  borders (`colors[]` in `config.h`).
- **[gaps]** — gaps between tiled clients, toggled at runtime with
  `MODKEY+g`. Configured to a 3px gap (`gappx` in `config.h`).
- **[autostart]** — runs commands listed in the `autostart[]` array in
  `config.h` at startup and terminates them on exit, instead of relying on
  the `-s` flag. Used here to start `swaybg` for the wallpaper.
- **[cursortheme]** — makes the xcursor theme and size configurable via
  `cursor_theme`/`cursor_size` in `config.h` (and exports `XCURSOR_THEME`/
  `XCURSOR_SIZE` so clients match). Naming a theme is required on systems
  with no `default` cursor theme installed: otherwise wlroots falls back to
  a 10x16px built-in cursor that ignores `cursor_size`. Set to `"Adwaita"`
  at size 48 here, which the monitor scale in `monrules` multiplies.
- **[attachbottom]** — newly opened windows are appended to the bottom of
  the stack instead of becoming the new master. This keeps whichever window
  is master in place, so the 2nd window you open lands in the stack (right
  side) rather than displacing the 1st one out of master (left side).
- **[movestack]** (by Nikita Ivanov) — moves the focused client up or down
  the stack, bound to `MODKEY+Shift+h/j/k/l` here.
- **[bar-systray]** (by [janetski]) — a StatusNotifierItem tray at the right
  end of the bar, adding a `libdbus` dependency. Left click activates an item,
  right click opens its menu through `dmenucmd`. Size and spacing come from
  `systrayiconsize`/`systrayspacing`, and `showsystray` turns it off.
  Ported from 0.7 to this tree, with three changes: icons are drawn at a
  configurable size centered in the bar instead of always filling its height;
  `destroytray()` unlinks the tray before freeing it (it was left on the
  watcher's list, so re-creating a monitor's tray left a dangling pointer);
  and the watcher accepts registrations under a well-known bus name, which is
  what KStatusNotifierItem (so every Qt/KDE app) sends and which was
  previously rejected as a bad argument. A missing session bus now logs a
  warning instead of aborting startup.

  Note that it does not read icons from the filesystem, by design: apps that
  publish an icon *name* rather than pixel data show the first letter of
  their name instead of an icon.
- **[hide-cursor-when-typing]** (by [unixchad]) — hides the mouse cursor as
  soon as a key is pressed, restoring it on the next pointer motion or
  button press, like `xbanish`. Toggle with `hide_cursor_when_typing` in
  `config.h`.
- **[warpcursor]** (by [Ben Collerson]) — warps the cursor to the center of
  a newly focused client if the cursor isn't already over it, and to the
  center of the monitor's usable area on `arrange()` when no client is
  focused.

### Local additions

Not from [dwl-patches] — written for this tree:

- **Title bars** — every window gets a title bar showing its title, drawn
  with the same `drwl` code as the bar. Height and colors come from
  `titlebar`/`titlepadding` and the `SchemeTitle`/`SchemeTitleSel` entries
  of `colors[]`; clicking a title bar focuses its window.
- **Tabbed layout** — an i3/sway-style `tabbed` layout toggled with
  `MODKEY+t`. The group is laid out as a single window and the members'
  title bars are packed into its one title row as tabs, so `MODKEY+h/l`
  cycles between them. Pressing `MODKEY+t` again restores the previous
  layout.
- **Bar notifications** (`src/notify.c`) — a minimal
  `org.freedesktop.Notifications` server on the session bus that renders the
  most recent notification (`app: summary - body`) centered in the same bar
  space used for the title/blank area, computed so it never overlaps the
  tags, layout symbol, status text, or systray. Long text is truncated with
  an ellipsis (the same way titles already are); left-clicking it scrolls to
  whatever didn't fit, wrapping back to the start once you reach the end.
  Only one notification is tracked at a time — a new one always replaces
  whatever is showing — and it auto-hides after `notification_timeout`
  seconds. Toggle with `shownotifications` in `config.h`; when off (or if
  another notification daemon like mako/dunst/swaync already owns the bus
  name), dwl doesn't touch the bus name and nothing changes.

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


[wlroots]: https://gitlab.freedesktop.org/wlroots
[dwm]: https://dwm.suckless.org/
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
