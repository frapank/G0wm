# Credits

g0wm is licensed under **GPL-3.0-or-later**; the full text is in
[`LICENSE`](../LICENSE). It is not an independent piece of work: it started as
a fork of [dwl] and still carries code from dwl, from the projects dwl itself
inherited from, and from patches written by other people. This page records
where those parts come from.

## Upstream

g0wm is a fork of **[dwl]**, the dwm-inspired Wayland compositor by Devin J.
Pohly and the dwl contributors, and inherits its GPL-3.0-or-later license.
Everything that is not described in [features.md](features.md) as g0wm's own
behaviour is, in one form or another, still dwl.

Through dwl, the tree also carries:

| File | Project | License | Covers |
| --- | --- | --- | --- |
| [`license/dwm.txt`](../license/dwm.txt) | [dwm] | MIT/X Consortium | `src/util.c`, `include/util.h` and the tags/layout model |
| [`license/sway.txt`](../license/sway.txt) | [sway] | MIT | parts of the wlroots plumbing |
| [`license/tinywl.txt`](../license/tinywl.txt) | tinywl | CC0 | the compositor skeleton dwl was built on |

## Vendored code

| File | Project | License |
| --- | --- | --- |
| [`external/drwl.h`](../external/drwl.h) | [drwl] by sewn and notchoc, including Björn Höhrmann's UTF-8 decoder | MIT ([`license/drwl.txt`](../license/drwl.txt)) |

`drwl` is what draws every piece of text g0wm puts on screen: the bar, the
title bars, the notifications and the runner.

## Patches

Several features arrived as [dwl-patches] and were then adapted, extended or
rewritten in this tree. The originals, and their authors:

| Patch | Author | What it gave g0wm |
| --- | --- | --- |
| [bar] | dwl-patches | the bar itself |
| [gaps] | dwl-patches | gaps between tiled clients |
| [autostart] | dwl-patches | `autostart[]` instead of the `-s` flag |
| [cursortheme] | dwl-patches | configurable xcursor theme and size |
| [attachbottom] | dwl-patches | new windows appended to the stack |
| [movestack] | Nikita Ivanov | moving a client up and down the stack |
| [bar-systray] | [janetski] | the StatusNotifierItem tray |
| [hide-cursor-when-typing] | [unixchad] | hiding the pointer while typing |
| [warpcursor] | [Ben Collerson] | warping the cursor to the focused client |
| [client-opacity] / [client-opacity-focus] | [Hansvon] | per-client and focus-dependent opacity |

The tray was ported from dwl 0.7 with three changes: icons are drawn at a
configurable size centered in the bar instead of always filling its height;
`destroytray()` unlinks the tray before freeing it, where it used to be left on
the watcher's list so that re-creating a monitor's tray left a dangling
pointer; and the watcher accepts registrations under a well-known bus name,
which is what KStatusNotifierItem sends and which was previously rejected as a
bad argument.

The opacity patches were merged with the X11 client initialisation the focus
variant is missing, and with the rule fields treated as an override rather than
an overwrite; the app filter, the global toggle and the frosted glass on top of
them are g0wm's own.

## Thanks

Great thanks to Devin J. Pohly and everyone who has contributed to dwl, to the
suckless.org and [dwm] communities whose design this all still follows, to sewn
and notchoc for [drwl], and to every patch author listed above. None of this
would exist without their work.

[dwl]: https://codeberg.org/dwl/dwl
[dwm]: https://dwm.suckless.org/
[sway]: https://github.com/swaywm/sway
[drwl]: https://codeberg.org/sewn/drwl
[dwl-patches]: https://codeberg.org/dwl/dwl-patches
[bar]: https://codeberg.org/dwl/dwl-patches/wiki/bar
[gaps]: https://codeberg.org/dwl/dwl-patches/wiki/gaps
[autostart]: https://codeberg.org/dwl/dwl-patches/wiki/autostart
[cursortheme]: https://codeberg.org/dwl/dwl-patches/wiki/cursortheme
[attachbottom]: https://codeberg.org/dwl/dwl-patches/wiki/attachbottom
[movestack]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/movestack
[bar-systray]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/bar-systray
[hide-cursor-when-typing]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/hide-cursor-when-typing
[warpcursor]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/warpcursor
[client-opacity]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/client-opacity
[client-opacity-focus]: https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/client-opacity-focus
[Hansvon]: https://codeberg.org/Hansvon
[unixchad]: https://codeberg.org/unixchad
[janetski]: https://codeberg.org/janetski
[Ben Collerson]: https://codeberg.org/bencc
