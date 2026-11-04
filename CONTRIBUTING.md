# Contributing

Patches are welcome as pull requests on GitHub. Open an issue first for
anything big.

## Code

- Run `make format` before committing, the CI checks it.
- Build with `./configure --debug` and fix the warnings you add.
- Keep comments short. Say why, not what.
- New files start with the SPDX header the others have, `reuse lint`
  checks it.
- A new setting goes in `src/core/settings/config.h`,
  `include/core/settings.h`, `src/core/settings/read.c`,
  `src/core/settings/write.c` and `docs/settings.md`.

## Layout

`src/` and `include/` have the same tree, one directory per area: `core`,
`input`, `output`, `window`, `render` and `desktop`. A module too big for one
file gets its own directory, such as `src/desktop/bar/`. Its files share what
the rest of g0wm does not need through an `internal.h` next to them.

## Naming

- Functions and file scope variables start with their module name, static
  ones too: `bar_draw()`, `runner_buffer`. `src/core/dbus.c` uses `bus_`, as
  `dbus_` is taken by libdbus.
- Listener callbacks are named after the event they handle:
  `monitor_handle_new_output()`.
- `src/core/g0wm/` is the core and its names carry no prefix: `seat`,
  `selected_monitor`, `spawn()`.
- Settings are named after what they hold, `border_width`, while the key in
  the config file keeps its own name, `borderpx`.
- Types are `CamelCase`, constants and macros `UPPER_CASE`.

## Commits

One change per commit. The title is a short imperative sentence, with no
prefix and no trailing period:

    Open files from the runner

Check `git log` for more examples.

## Bugs

Include the output of `g0wm -v`, your wlroots version, your GPU and the log
from `g0wm -d`.
