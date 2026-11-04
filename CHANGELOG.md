# Changelog

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
