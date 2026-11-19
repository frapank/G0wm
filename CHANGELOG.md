# Changelog

## 1.3.2 - 2026-11-19

### Changed

- The export-dmabuf protocol is removed, since it handed out frames without
  the privacy mask. Recorders should use screencopy instead
- Opacities are clamped to 0 to 1, mfact is bounded, and monitor scales and
  refresh rates out of range are rejected
- Settings left empty keep their default
- An `xkb_rules` that does not compile falls back to the default keymap
- Outputs without a matching monitor rule get the default settings
- Desktop entry `Exec` lines are split as the spec says instead of being
  passed to a shell
- The runner history is created readable by the user only
- The README recommends starting g0wm with `exec` from a TTY, so a crash logs
  you out instead of leaving an unlocked shell

### Fixed

- Popups, subsurfaces, input method popups and drag icons of private windows
  showed in screen captures
- Menus and tooltips of private X11 apps showed in screen captures
- Dialogs of a private window stayed public
- Private rules were not applied when a window changed its title or app_id,
  and reloads could reset window privacy
- Captures were masked after reaching the client's buffer, damage revealed
  activity inside private windows, and unmasked frames could still be sent
- Notification action labels and private bar titles could show in screen
  sharing
- The keyboard could stay with a popup grab, the runner or an X11 menu after
  locking
- Clicks, touches, pointer locks and the client cursor reached windows under
  the lock screen
- The input method could read and type into the lock screen password field
- Key repeat kept running under the lock screen
- Locked was reported before every output showed the lock, and slow clients
  could keep the lock screen from being drawn
- Idle inhibitors on surfaces outside the scene blocked blanking and locking
- Malformed tray object paths, notification hints and property requests
  crashed the compositor
- Tray registrations were unlimited and not tied to their client, and icon
  refreshes were not coalesced
- Tray icon files were read blocking and with any gdk-pixbuf loader
- Tray menu labels were not sanitized
- Use after free in the tray on reload, item destroy and watcher stop
- Notifications were not rate limited, and the activation token was broadcast
  to every client on the bus
- Focus and coordinates came from the old pointer position after a jump
- Every pointer constraint was activated on motion, not only the one under the
  pointer
- Virtual keyboards did not work without a physical keyboard
- Unmanaged X11 windows kept the keyboard while mapped
- Reloading while a binding repeated read freed memory
- Huge floating windows could exhaust memory and exit the compositor
- A broken status pipe ended the session
- The runner app scan looped forever on directory links to a parent
- Crashes from destroyed popups, freed clients and decorations, wrong size
  pooled buffers, failed allocations and D-Bus setup errors

## 1.3.1 - 2026-11-12

### Changed

- Window titles are redrawn only when they change, and title updates are
  coalesced with output frames
- Urgent tag indicators animate separately, so pulsing no longer redraws
  titles or unrelated parts of the bar
- Status updates are skipped when the content is unchanged
- Decoration buffers are kept between frames when nothing changed, and
  rounded corner masks are reused
- Corner coverage is computed with fewer calculations, and duplicate title
  background fills are gone
- The runner reads directory entry types directly instead of querying each
  file
- Loaded settings are grouped into structs, with their lifetime managed in one
  place
- Clearer type, field and variable names across the sources
- The refactoring commits are ignored in `git blame`

### Fixed

- Title redraws were triggered during urgency pulses

## 1.3.0 - 2026-11-10

### Added

- A `private` config section for the placeholder of private windows in screen
  captures, with text, images, sizing and colors
- Images and text centered together, with adjustable text scaling and
  preserved image proportions
- Per-window `privatetext`, `privatecolor` and `privatetextcolor` rules
- `{app}` in the placeholder text. `{title}` stays literal so private titles
  are not exposed
- Placeholder corners follow the window style
- Configuration tests, CodeQL analysis and Dependabot updates for GitHub
  Actions

### Changed

- Simpler build system, with `--enable-hardening` separate from `--native`
- Rewritten README and reorganized documentation, with building and
  troubleshooting guides

### Fixed

- Memory leaks in configuration handling found by sanitizers

## 1.2.2 - 2026-11-09

### Added

- Packages with wlroots and libscfg built in: .deb for Ubuntu 26.04+ and
  Debian testing, .rpm for Fedora 44+ and openSUSE Tumbleweed, a tarball for
  other recent distributions, and SHA256SUMS
- `./configure --bundle-deps` links wlroots and libscfg into the binary

## 1.2.1 - 2026-11-09

### Added

- Notifications are private by default: screen sharing shows a placeholder
  instead of the text and the icon. The notification history is hidden the
  same way
- `toggleprivatenotify` (Super+Ctrl+P) to show or hide them
- `privatenotify` to have them shown by default
- `notification_privatetext` for the placeholder, with `{app}` replaced by the
  app name
- `notification_privatehide` to leave hidden notifications out of screen
  sharing altogether
- A bar dot showing when notifications differ from the default, with
  configurable side and colors

### Changed

- The tree is formatted with clang-format, with a column limit of 100
- Descriptive variable names, declared where they are used
- Activation tokens have their own module

## 1.2.0 - 2026-11-04

### Added

- Private windows, blacked out in screen captures, with the `private` rule,
  the `toggleprivate` action (Super+Shift+P) and the `alwaysprivate` setting
- Settings for the private windows: `privatetitle`, `privatecolor`,
  `privateround` and `publiccolor`

### Changed

- Building needs a C23 compiler, GCC 14 or Clang 18 and newer
- The sources are split into one directory per area, each module with its own
  header
- The built-in defaults moved from `include/config.h` to
  `src/core/settings/config.h`
- Every file states its license, following the REUSE specification
- Licenses moved from `license/` to `LICENSES/`
- Shorter README, updated man page, `FEATURES.md` renamed to `features.md`
- Added the changelog, contributing guide, issue templates and editorconfig

### Fixed

- A tray icon claiming a huge size could overflow the size check

## 1.1.0 - 2026-11-02

### Added

- Runner modes picked by prefix: calculator, web search, open windows, tags,
  desktop applications and files
- Runner history, fuzzy matching and frecency ranking
- Configurable runner shell and extra command directories
- Pulsing dot for urgent tags, with configurable size, gap and roundness
- Setting to choose the monitors that show the status
- Settings documentation

### Fixed

- Formatting of the tag dot and the runner entries

## 1.0.0 - 2026-10-30

Initial release.
