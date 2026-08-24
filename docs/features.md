# Features of this fork

Detail behind the overview in the [README](../README.md): the [dwl-patches]
applied on top of upstream dwl, the features written for this tree, and the
fixes carried on top of upstream. Every configuration knob named here lives in
`config.h`/`config.def.h` and is prompted for by `./config_gen`.

## Applied patches

- **[bar]** — internal i3-like status bar (tags, layout symbol, focused client
  title, external status text), rendered with `fcft`/`pixman`. Configured for
  an always-black bar with white text and black window borders (`colors[]`).
- **[gaps]** — gaps between tiled clients, toggled with `MODKEY+g`. Set to 3px
  (`gappx`).
- **[autostart]** — runs the commands in `autostart[]` at startup and
  terminates them on exit, instead of relying on the `-s` flag. Used here to
  start `swaybg` for the wallpaper.
- **[cursortheme]** — makes the xcursor theme and size configurable via
  `cursor_theme`/`cursor_size` (and exports `XCURSOR_THEME`/`XCURSOR_SIZE` so
  clients match). Naming a theme is required on systems with no `default`
  cursor theme installed: otherwise wlroots falls back to a 10x16px built-in
  cursor that ignores `cursor_size`. Set to `"Adwaita"` at size 48, which the
  monitor scale in `monrules` multiplies.
- **[attachbottom]** — new windows are appended to the bottom of the stack
  instead of becoming the new master, so the 2nd window you open lands in the
  stack (right side) rather than displacing the 1st one out of master.
- **[movestack]** (by Nikita Ivanov) — moves the focused client up or down the
  stack, bound to `MODKEY+Shift+h/j/k/l`.
- **[bar-systray]** (by [janetski]) — a StatusNotifierItem tray at the right
  end of the bar, adding a `libdbus` dependency. Left click activates an item,
  right click opens its menu through `traymenucmd`. Size and spacing come from
  `systrayiconsize`/`systrayspacing`, and `showsystray` turns it off.

  Disable at build time with `./configure --no-systray` (`--no-tray` is
  accepted too), which leaves `src/systray/` out of the build and drops the
  tray settings and the `ClkTray` bindings from `config.h`.

  Ported from 0.7 to this tree, with three changes: icons are drawn at a
  configurable size centered in the bar instead of always filling its height;
  `destroytray()` unlinks the tray before freeing it (it was left on the
  watcher's list, so re-creating a monitor's tray left a dangling pointer); and
  the watcher accepts registrations under a well-known bus name, which is what
  KStatusNotifierItem (so every Qt/KDE app) sends and which was previously
  rejected as a bad argument. A missing session bus now logs a warning instead
  of aborting startup.

  It does not read icons from the filesystem, by design: apps that publish an
  icon *name* rather than pixel data show the first letter of their name
  instead of an icon.
- **[hide-cursor-when-typing]** (by [unixchad]) — hides the mouse cursor as
  soon as a key is pressed, restoring it on the next pointer motion or button
  press, like `xbanish`. Toggle with `hide_cursor_when_typing`.
- **[warpcursor]** (by [Ben Collerson]) — warps the cursor to the center of a
  newly focused client if it isn't already over it, and to the center of the
  monitor's usable area on `arrange()` when no client is focused.
- **[client-opacity-focus]** (by [Hansvon], on top of [client-opacity]) — one
  opacity for the focused window and another for every unfocused one, set by
  `opacity_focus`/`opacity_unfocus` and overridable per client through the two
  opacity columns of `rules[]` (a `0` there keeps the default). `MODKEY+o` and
  `MODKEY+Shift+O` step the focused window's opacity up and down,
  `MODKEY+Ctrl+o`/`MODKEY+Ctrl+Shift+O` do the same for what it fades to once
  it loses focus, both clamped to 10-100%. Both defaults are `1.00f`, so
  nothing is transparent until you ask for it. Fullscreen clients are always
  drawn opaque, and the opacity applies to the client's own buffers only, so
  borders, title bars and popups keep their own colors.

  Merged with the X11 client initialisation the focus variant is missing, and
  with the rule fields treated as an override rather than an overwrite. The app
  filter and global toggle below are not part of it.

## Local additions

- **Title bars** — every window gets a title bar showing its title, drawn with
  the same `drwl` code as the bar. Height and colors come from
  `titlebar`/`titlepadding` and the `SchemeTitle`/`SchemeTitleSel` entries of
  `colors[]`; clicking a title bar focuses its window.
- **Tabbed layout** — an i3/sway-style `tabbed` layout toggled with `MODKEY+t`.
  The group is laid out as a single window and the members' title bars are
  packed into its one title row as tabs, so `MODKEY+h/l` cycles between them.
  Pressing `MODKEY+t` again restores the previous layout.
- **Bar notifications** (`src/notify.c`) — a minimal
  `org.freedesktop.Notifications` server on the session bus that renders the
  most recent notification (`app: summary - body`) centered in the same bar
  space used for the title/blank area, computed so it never overlaps the tags,
  layout symbol, status text, or systray. Long text is truncated with an
  ellipsis (the same way titles already are); left-clicking it scrolls to
  whatever didn't fit, wrapping back to the start once you reach the end.
  Right-clicking dismisses it early.

  Only one notification is tracked at a time — a new one always replaces
  whatever is showing — and it auto-hides after `notification_timeout` seconds,
  or after the `expire_timeout` the client asked for when that is shorter (a
  request to never expire is not honoured: the bar has a single slot, capped at
  60s either way). Clients are told what happened through the usual
  `NotificationClosed` signal, and `replaces_id` updates a notification in
  place.

  That bar space is shared with `barwintitle`: while a notification is up it
  takes the box over, drawn in `SchemeNotify` so it doesn't read as a window
  title, and the title comes back as soon as it expires or is dismissed.

  Toggle with `shownotifications`; when off (or if another notification daemon
  like mako/dunst/swaync already owns the bus name), g0wn doesn't touch the bus
  name and nothing changes.
- **Graphics tablet support** — pen tablets (tested on a Wacom Intuos S) move
  the cursor and click like a pointer. `inputdevice()` attaches
  `WLR_INPUT_DEVICE_TABLET` devices to the cursor the same way pointers are,
  but wlroots reports tablet tool motion and clicks on separate
  `tablet_tool_axis`/`tablet_tool_tip` signals instead of the generic pointer
  ones, so two listeners translate them: `tabletaxis()` turns an axis event
  into the same absolute-to-relative motion `motionabsolute()` does for
  pointers (skipping events that update neither X nor Y, e.g. pressure-only
  samples), and `tablettip()` maps the tool tip touching down/up to a synthetic
  `BTN_LEFT` press/release through the existing `buttonpress()`. Both send a
  `wlr_seat_pointer_notify_frame()` afterwards, since tablets don't emit a
  frame event of their own and clients hold pointer events back until one
  arrives. Pressure, tilt, the pad buttons, and the eraser end are not
  forwarded — only cursor motion and the tip-as-click.
- **Opacity filter and global toggle** — written on top of
  [client-opacity-focus]. `opacity_apps[]` lists the app ids opacity applies
  to, matched against the app id the way the window rules are;
  `opacity_exclusion_type` flips what the list means — `0`, the default, makes
  it an include list, `1` a skip list — and an empty list covers every app. The
  filter is evaluated once per client in `mapnotify()` rather than in
  `applyrules()`, so the clients that have a parent, which skip the rules
  entirely, are covered too. `MODKEY+Alt+o` (`toggleopacity()`) turns opacity
  off for every window at once and back on, and `opacity_enabled` picks the
  state it starts in, so a config can carry its opacity settings switched off.
  Since neither changing the opacity nor toggling it damages anything on its
  own, and the values are applied while rendering, both ask every enabled
  output for a frame.
- **Bar runner** (`src/g0wn.c`, `RUNNER`) — `MODKEY+r` turns the same shared
  box the title/notification use into a prompt: a caret marks the insertion
  point (it is what tells an empty prompt apart from an empty title area, and
  its size is derived from the font height so it holds up on every monitor
  scale), type a prefix and the first match among every executable name in
  `$PATH` is drawn after it in `SchemeRunnerSuggest`, `Tab` accepts it,
  `Return` runs the suggestion (or
  whatever was typed if there is none) through `/bin/sh -c`, `Ctrl+C` empties
  the prompt without closing it, `Escape` cancels. While it's open every key
  belongs to it — no keybinding fires and nothing reaches the focused client —
  and held keys repeat into it at the keyboard's own rate, so backspace clears
  by holding it. Colors come from `SchemeRunner`/`SchemeRunnerSuggest`.

  When there's no `$PATH` match, a buffer made only of digits, `+ - * / % ( )`,
  a decimal point and whitespace (with at least one digit and one operator)
  is instead evaluated as arithmetic by a small hand-rolled recursive-descent
  parser and its result drawn the same way, e.g. `2+2` shows `= 4`. It never
  shells out or touches the filesystem, so a malformed expression just fails
  to parse; division by zero, unmatched parens and non-finite results are all
  rejected rather than shown. `Tab` replaces the buffer with the bare result
  (so it can feed into a further expression), and `Return` on a bare
  expression is a no-op instead of handing `2+2` to `/bin/sh` as a command.

  The `$PATH` scan is cached, and each open first compares the mtime of the
  `$PATH` directories against the ones the cache was built from: a directory's
  mtime moves whenever an entry is added or removed, so one `stat()` per entry
  (~0.04ms in total, no `readdir`) is enough to tell whether a rescan would
  turn anything up. A program installed while g0wn runs therefore shows up at
  the next prompt, and only that prompt pays for the rescan. Making an
  *existing* file executable is the one change this misses, since it leaves
  the directory's mtime alone.

  Disable at build time with `./configure --no-runner`, which reverts
  `MODKEY+r` to spawning `menucmd` (`wmenu-run`) instead.
- **Status text modules** (`scripts/g0wn-status.sh`, `./status_gen`) — the
  script `start-g0wn` pipes into g0wn builds its line out of modules named in
  `$XDG_CONFIG_HOME/g0wn/status.conf`: `date`, `time`, `battery`, `cpu`, `ram`,
  `netdown`, `netup`, printed in the order they are listed there. Each one has
  a format of its own — a strftime string for `date` and `time`, a template
  with `%v` for the value, `%i` for the icon, and `%u`/`%t` for the memory in
  use and the total — and there is a `prefix`, a `separator` and a `suffix`
  around them. Without a config file the output is the date, clock and battery
  the script printed before it was made configurable, byte for byte.

  `./status_gen` writes that file the way `config_gen` writes `config.h`, with
  the same prompts, the same `-c` to edit the current one and the same backup.
  Each format is picked from a menu that shows the line the bar would draw,
  not the template, and the last thing it does is run `g0wn-status.sh --once`
  on what it wrote.

  Icons are Nerd Font glyphs substituted into `%i`, offered only after
  `fc-list` reports a Nerd Font family: without one they would draw as boxes,
  so they are skipped with a warning unless asked for anyway. `icon_battery`
  holds a list, drawn from empty to full according to the charge.

  A module whose readings the system cannot provide — no battery, no
  `/proc/meminfo`, `netstat` without byte counters — is dropped at startup
  with a warning on stderr instead of printing an empty field. The loop runs
  once a second, so it reads `/proc` and `/sys` with the shell's own `read`
  and does its arithmetic in the shell: `date` stays the only command it forks
  per tick.

[dwl-patches]: https://codeberg.org/dwl/dwl-patches
[attachbottom]: https://codeberg.org/dwl/dwl-patches/wiki/attachbottom
[autostart]: https://codeberg.org/dwl/dwl-patches/wiki/autostart
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
