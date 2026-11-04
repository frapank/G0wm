# Changelog

## 1.2.0 - 2026-11-04

### Added

- Private windows, blacked out in screen captures, with the `private` rule
  and the `alwaysprivate` setting

### Changed

- Building needs a C23 compiler, GCC 14 or Clang 18 and newer
- The built-in defaults moved from `include/config.h` to `src/config.h`
- Every file states its license, following the REUSE specification

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
