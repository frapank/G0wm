# Building

You need Bash, GNU make, pkg-config and a C23 compiler (GCC 14+ or Clang 18+).
The libraries are wlroots 0.20 with the libinput backend, wayland,
wayland-protocols, libinput, xkbcommon, pixman, fcft, libdbus, gdk-pixbuf and
[libscfg](https://git.sr.ht/~emersion/scfg). On distros that split them up,
you need the development packages too.

For X11 apps you also need libxcb, xcb-icccm and Xwayland. If you don't use
those, add `--disable-xwayland` when configuring.

## Build and install

From the project directory:

```sh
./configure
make -j"$(nproc)"
make install
```

This installs to `~/.local`, with the binary in `~/.local/bin`. Make sure
that's in your `PATH`. To install system-wide instead:

```sh
./configure --prefix=/usr
make -j"$(nproc)"
sudo make install
```

The system install also puts the session file where display managers look
for it. `make uninstall` removes the installed files and keeps your config;
use the same paths and permissions you installed with.

Rerun configure when you change options or update the script. The command
you used is saved at the top of `config.mk`. `make V=1` shows the full commands.

## If wlroots or libscfg are missing

Configure can download and build them for you:

```sh
./configure --bundle-deps
make -j"$(nproc)"
make install
```

This needs git, Meson, Ninja and wlroots' build dependencies. The Ubuntu
package list is in [the CI action](../.github/actions/ubuntu-deps/action.yml).
Only wlroots and libscfg go inside the binary; the other libraries still
come from your system. The build log is in `subprojects/build.log`.

If you've already built wlroots somewhere else, point configure there:

```sh
./configure --wlroots-prefix=/path/to/prefix
```

It looks for the pkg-config files under that prefix and records the library
path in the binary. Keep the libraries there while you're using it.
Libscfg can be in the same prefix or installed system-wide.

## Options and checks

The default build uses `-O2`. `--debug` turns optimization off and adds debug
information. For a crash, `--debug --enable-sanitizers` is usually more useful.

`--native` adds `-O3`, CPU tuning and LTO for your machine. Hardening is
separate: add `--enable-hardening`, with or without `--native`.
`./configure --help` has the rest, including compiler and linker options.

`make test` checks the config without starting a graphical session.
`make check` also runs clang-format and ShellCheck, so you'll need those
installed. CI uses clang-format 21.1.7. `make help` lists the targets.

`make tarball`, `make deb` and `make rpm` put packages in `build/pkg/`.
The last two need dpkg-dev and rpmbuild respectively. `make packages` builds
the formats you have tools for.

`make dist` archives HEAD, so it only includes committed files. The version
comes from `VERSION`. `make clean` removes the binary and build output;
`make distclean` also removes `config.mk` and `subprojects/`.
