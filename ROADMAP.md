# Roadmap

What is planned next, grouped by area. Each step can be shipped on its own,
in the order listed.

## Display and color

Today everything is treated as sRGB: apps cannot describe their colors and
outputs never leave SDR.

### color-representation-v1

Lets an app say how its YUV video buffers turn into RGB (BT.601/709/2020
coefficients, limited or full range, chroma siting, alpha mode). Without it
the compositor guesses, and video shows grey blacks or tinted colors.

- [ ] Create the manager with
  `wlr_color_representation_manager_v1_create_with_renderer()`. The scene
  graph reads the surface state by itself.

### color-management-v1

Lets an app tag its buffers with a color space (sRGB, Display P3, BT.2020,
PQ…) and learn what the output prefers, so the scene graph converts between
them.

- [ ] Create the manager, advertising only what the renderer supports
  (`wlr_color_manager_v1_*_list_from_renderer()`).
- [ ] Attach it to the scene with `wlr_scene_set_color_manager_v1()`.
- [ ] Document that real conversions need the Vulkan renderer
  (`WLR_RENDERER=vulkan`); with GLES2 only sRGB is advertised.

### HDR per output

Drives an HDR monitor in BT.2020/PQ, so games and video players can show
real HDR.

- [ ] Add an `hdr` option to the monitor rules in the config.
- [ ] Enable it only when the output lists PQ and BT.2020 in
  `supported_transfer_functions` / `supported_primaries`.
- [ ] Set the image description on the output commit
  (`wlr_output_state_set_image_description()`).
- [ ] Handle the conflict with gamma-control: gamma tables (wlsunset,
  gammastep) cannot apply to an HDR output, so refuse them while HDR is on.
- [ ] Add a brightness setting for SDR content shown in HDR, which otherwise
  looks dim or washed out.

### ICC profiles per monitor

Applies a calibration profile to an SDR monitor, for photo and graphics work.

- [ ] Add an `icc` path to the monitor rules.
- [ ] Load it with `wlr_color_transform_init_linear_to_icc()` and set it as the
  output color transform.
- [ ] Reapply it on settings reload, and skip it on outputs running in HDR.

## Input

### On-screen keyboard (works today)

wvkbd only needs layer-shell, virtual-keyboard and touch, which g0wm has.

- [ ] Check in a nested session that touch reaches layer-shell surfaces.
- [ ] Bind showing and hiding wvkbd (`pkill -USR1/-USR2 wvkbd`) to a key, a
  swipe or a clickable status area.

### IPC socket

Lets other programs drive g0wm and follow what it does, so scripts, external
bars and switchers no longer need a key binding or a polling loop.

- [ ] Add `src/core/ipc.c`: a Unix socket in
  `$XDG_RUNTIME_DIR/g0wm.$WAYLAND_DISPLAY.sock` (mode 0600), added to the
  event loop like the D-Bus connection in `src/core/dbus.c`.
- [ ] Run commands through the same action table that the key bindings use in
  `src/core/settings/`, so `view 3` or `spawn foot` work without a second parser.
- [ ] Answer queries (`clients`, `tags`, `outputs`, `focused`) in JSON.
- [ ] Add `subscribe` for events (focus, tag, title, urgent, layout, output),
  emitted where the state already changes. Give each subscriber a bounded
  output buffer and drop it when full, so a slow reader never blocks the
  compositor.
- [ ] Version the event format from the start: it becomes a public API.
- [ ] Add a small `g0wmctl` client.
- [ ] Let programs set named segments of the status bar (with a click
  command) instead of sharing the single stdin line.
- [ ] Create `ext-workspace-v1` (`wlr_ext_workspace_v1`) so external bars and
  switchers can follow the tags.

## Privacy

### Private windows in screen capture

Keeps a window (a password manager, a private chat) out of screenshots and
screen shares, for any app that goes through the portal, such as Discord or
OBS.

- [ ] export-dmabuf hands out the screen's own buffer, so it cannot be masked:
  hide it while a private window is shown, or drop it.
- [ ] Add a share mode (key and IPC command) with an indicator in the bar.
  While it is on, private windows, and notifications and popups marked
  private, are replaced by a placeholder on the output. This also hides them
  on the real screen.
- [ ] Let the placeholder carry a text and a color of your choice, set in the
  config (`private` block) and overridable per rule. The text can use `{app}`
  and `{title}`, is drawn with fcft, and the shape follows the window border
  (rounded or squircle).

### Protocols per client

Every client can bind the powerful protocols today: screen capture, the
clipboard, fake input, the list of open windows. Lets the config give them
only to the programs that need them.

- [ ] Add a `protocols` block to the config: a list of programs for each of
  `screencopy`, `export-dmabuf`, `data-control` (both versions),
  `virtual-keyboard`, `virtual-pointer`, `input-method` and
  `foreign-toplevel`.
- [ ] Stay permissive by default: a protocol that is not in the block is open
  to every client, as it is now. Once a protocol is listed, only the listed
  programs see it, and everyone else gets no such global.
- [ ] Filter the globals with `wl_display_set_global_filter()`. Tell a client
  by its executable (`wl_client_get_credentials()` and `/proc/<pid>/exe`), and
  a sandboxed app by the app id of its security context
  (`security_context_manager` already exists).
- [ ] Log every refusal, such as `denied screencopy to grim`, so a broken tool
  can be traced.
- [ ] Document that the filter applies when a client connects, so a reload
  affects only new clients, and that the portal backend (such as
  `xdg-desktop-portal-wlr`) must be in the `screencopy` list or Discord and OBS
  lose the screen.
- [ ] Limiting `export-dmabuf` to trusted programs also narrows the gap in
  private windows above.

### Private notifications

A message with personal text can show up in the middle of a call or a share.
Notifications are hidden by default, like a private window, and a key shows
them.

- [x] Hide the notifications by default: like a private window, screen
  sharing gets a short placeholder instead of the summary and body, while the
  real screen keeps them. History is hidden the same way.
- [x] Add a key and an action (`toggleprivatenotify`, next to `toggleprivate`)
  that switches between hidden and shown.
- [x] Add a `privatenotify` option, on by default. Set to 0, notifications are
  shown by default and the key hides them.
- [ ] Draw a dot in the bar for the state that differs from the default,
  the same way the dot of private windows works: when hidden is the default
  and notifications are shown, it uses `publiccolor` (light blue). When shown
  is the default and they are hidden, it uses `privatecolor` (purple).
- [ ] Make the position and the two colors of the dot settable in the config,
  with `publiccolor` and `privatecolor` as defaults.

## Quality and security

### Static analysis in CI

Catches use-after-free, leaks and null dereferences on every pull request,
before a build is even run.

- [ ] Add a CodeQL workflow for C, on pushes to main, pull requests and once a
  week. It needs a real build between `init` and `analyze`, so run `make` the
  way `ci.yml` does.
- [ ] Check that it works inside the `archlinux` container: wlroots 0.20 is not
  packaged on the Ubuntu runners.
- [ ] Go through the first results in the Security tab, fix or dismiss them.
- [ ] Make `tray_get_watcher()` and `item_get_watcher()` trust their argument,
  as every caller passes a valid one. Their null checks make `gcc -fanalyzer`
  assume it can be null, and it reports four dereferences.

### Headless tests with sanitizers

Starts the real compositor in CI, the way weston tests itself, so a crash at
startup, on a new client or on exit fails the build.

- [ ] Run g0wm with `WLR_BACKENDS=headless` and `WLR_RENDERER=pixman`, in a
  temporary `XDG_RUNTIME_DIR` and `XDG_CONFIG_HOME`.
- [ ] Add a small client in `tests/` that maps an xdg toplevel, commits a
  buffer and exits, so no terminal or font is needed.
- [ ] Feed a few status lines on stdin, then stop g0wm with SIGTERM. Fail on a
  non-zero exit or on any ASan or UBSan report.
- [ ] Run it from `make test` and in the sanitizers job of `ci.yml`.
- [ ] Add unit tests for the parts that need no display: the runner
  calculator in `src/desktop/runner/calc.c`, `notify_demarkup()` in
  `src/desktop/notify/message.c`, `util_put_codepoint()` and the settings
  parser in `src/core/settings/read.c`.

### File descriptors in child processes

Every program g0wm starts inherits the descriptors that are open at the time.
None leaks today, but nothing prevents it either.

- [ ] Open files with close-on-exec: `"e"` in the `fopen()` modes of
  `src/desktop/runner/` and `src/core/settings/`, `O_CLOEXEC` in any new `open()`. The one in
  `spawn()` stays as it is: it becomes the child's stdin.
- [ ] Call `close_range(3, ~0U, 0)` in the child before `execvp()`, in
  `spawn()` and in the autostart loop of `src/core/g0wm/main.c`, so descriptors opened by
  libraries do not reach launched programs either.

### Settings in one struct

The 128 settings are separate globals, defined in `src/core/settings/config.h`.
A reload overwrites them in place and never frees the old values.
the scfg tree and the `keys`, `rules` and `layouts` arrays stay
allocated, partly because `Monitor.lt` points into `layouts`

- [ ] Group them in one struct, with a part per section such as bar, runner,
  input and opacity, reached through a single pointer, as sway does with its
  config.
- [ ] Move one section per commit, starting with the smallest.
- [ ] On reload, parse into a new struct, point the monitors at its layouts,
  then free the old one.
- [ ] Keep the running settings when the file has errors, instead of applying
  the part that could be read.
