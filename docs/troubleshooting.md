# Troubleshooting

If g0wm won't start, try it from a TTY with logging on:

```sh
g0wm -d 2>g0wm.log
```

Use `./g0wm` if you haven't installed the build yet. The messages before the
last error often explain what went wrong.

## Build problems

Configure tells you which dependencies it couldn't find. Check that you
installed the development packages too. For wlroots and libscfg, you can see
what pkg-config finds with:

```sh
pkg-config --modversion wlroots-0.20 scfg
```

If you built wlroots outside the system paths, pass
`--wlroots-prefix=/path/to/prefix` to configure. Or use `--bundle-deps` to
build wlroots and libscfg along with g0wm. That still needs the other build
dependencies; see [Building](building.md). If the bundled build fails, its
log is in `subprojects/build.log`.

If make asks you to rerun configure, the old command is at the top of `config.mk`.

## Command or library not found

A default install puts g0wm in `~/.local/bin`. If your shell can't find it,
check your `PATH` or try `~/.local/bin/g0wm` directly. `command -v g0wm` shows
which copy you're running.

For a missing library, run `ldd` on the binary and look for `not found`.
If you used `--wlroots-prefix`, that directory still needs to be there.
The release packages include wlroots and libscfg, but not every library;
install the `.deb` or `.rpm` through your package manager. The tarball doesn't
check dependencies.

## No session in the display manager

Most display managers don't read `~/.local/share/wayland-sessions`, which
is where a default source install puts the session file. From the project
directory, you can copy it to the system location:

```sh
sudo install -Dm644 share/g0wm.desktop /usr/share/wayland-sessions/g0wm.desktop
```

The display manager also needs to find `g0wm` in its `PATH`. A
[system install](building.md#build-and-install) takes care of both.
If it still won't start, try from a TTY to get the log.

`XDG_RUNTIME_DIR must be set` means your login session didn't provide its
runtime directory. Start from a normal user login with a working session
setup. Don't run g0wm with sudo.

## Config or apps not working

Run `g0wm -c` to check your config. It keeps the file you have, reports
problems on stderr and prints the settings it ends up using. If you don't
have a file yet, it creates one. Read the messages even if it exits successfully.

The config is in `~/.config/g0wm/config`, or `$XDG_CONFIG_HOME/g0wm/config`
if you set that variable. [Settings](settings.md) has the names and format.
A build without the integrated background won't accept wallpaper or blur
settings. Super+Shift+R reloads the file; some input changes need a restart.

The default terminal is `foot`. Install it or change the terminal binding
and the runner's terminal setting. X11 apps need Xwayland installed and a
build of g0wm with X11 support enabled.

## Reporting a bug

Open an issue with what happened and how to repeat it. Include the output
of `g0wm -v`, your distro, GPU, wlroots version and the log from `g0wm -d`.
If you built it yourself, add the configure options too. For a bundled build,
the library versions are in `packaging/bundle-deps` from that checkout.

Check the log and config for private information before posting them.
