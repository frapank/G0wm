# Contributing

Patches are welcome as pull requests on GitHub. Open an issue first for
anything big.

## Code

- Run `make format` before committing, the CI checks it.
- Build with `./configure --debug` and fix the warnings you add.
- Keep comments short. Say why, not what.
- A new setting goes in `include/config.h`, `src/settings.c` and
  `docs/settings.md`.

## Commits

One change per commit. The title is a short imperative sentence, with no
prefix and no trailing period:

    Open files from the runner

Check `git log` for more examples.

## Bugs

Include the output of `g0wm -v`, your wlroots version, your GPU and the log
from `g0wm -d`.
