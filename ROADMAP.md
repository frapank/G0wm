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

- [ ] Add an `hdr` option to the monitor rules in `settings.json`.
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

### text-input-v3 and input-method-v2

Lets the compositor bridge apps (text-input) and input methods
(input-method): fcitx5 and ibus for CJK and other scripts, emoji and Unicode
pickers, keyboards that open by themselves. Nothing changes until an input
method connects.

- [ ] Add `src/textinput.c` with the two managers.
- [ ] Make the active text-input follow the keyboard focus (enter/leave).
- [ ] Relay state between them: surrounding text, cursor rectangle and content
  type from the app, preedit and commit strings from the input method.
- [ ] Hand the keys to the input method while it holds a keyboard grab
  (`keypress` in `input.c`).
- [ ] Place the candidate popup next to the text cursor, clamped to the
  monitor.
- [ ] Test with fcitx5 and with an input-method keyboard such as squeekboard,
  which then opens on its own when a text field is touched. Chromium and
  Electron need `--enable-wayland-ime`.

## Accessibility

### Magnifier

Zooms the screen around the pointer, for low vision.

- [x] Add a zoom action and a per-monitor zoom level (e.g. MODKEY+scroll),
  with a key to reset it.
- [x] While zoomed, render the scene to an offscreen buffer and draw the
  region around the pointer scaled onto the output.
- [x] Move that region with the pointer: placed proportionally, the pointer
  stays where it really is, so input needs no remapping.
- [ ] Use the software cursor while zoomed so it gets magnified too, and turn
  off direct scanout and tearing while zoomed.
