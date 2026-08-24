<div align="center">

# **What g0wn does**

[README](../README.md) · [config.def.h](../config.def.h) · [man page](g0wn.1) · [credits](credits.md)

</div>

## The bar

Tags on the left, layout icon next to them, right: status text and tray, middle:
a box that changes according to context.

This middle box is the most interesting thing about the panel. Normally it
displays the name of the window that you have active focus on. However, once the
notification comes in, it takes over the box and draws it in its own colours, to
differentiate the two. And when you press the launcher button it turns into a
prompt. One element, three roles.

The bar can be placed either at the top or the bottom, it can be slightly
taller or shorter than the font suggests and it can be transparent according to
your settings. In a multi-monitor configuration you can choose between having a
bar for each monitor or a single bar following the monitor where you work.

#### **Tray** 
A StatusNotifierItem tray positioned at the right edge. 
Left click sends a signal to the app, right click opens its menu. 
It does not search for the icons on disk, therefore applications, 
which publish only a name of an icon, have their first letter displayed. 
Most applications provide real pixel-based icon.

#### **Notifications** 
A small `org.freedesktop.Notifications` server. One
notification at a time, a new one replaces the old one, and it goes away by
itself after a few seconds. Click it to scroll through text that did not fit,
right click to make it go away sooner. If mako or dunst is already running,
g0wn stays out of the way.

#### **Launcher** 
Hit the key and the bar turns into a prompt. Type a few letters
and it completes against everything executable in your `$PATH`, Tab takes the
suggestion, Enter runs it. If what you typed looks like arithmetic instead, it
does the math and shows you the answer, so `2+2` gives you `= 4` without
opening a calculator. While the prompt is open it owns the keyboard, so none of
your bindings fire and nothing leaks into the window underneath.

#### **Status text** 
Whatever you pipe into g0wn ends up on the right side of the
bar. The shipped script builds that line out of small modules, date, clock,
battery, cpu, ram and network speed, in whatever order you list them. Run
`./status_gen` and it walks you through picking them, with Nerd Font icons if
you have a Nerd Font. A module the machine cannot feed, like a battery on a
desktop, quietly drops out instead of printing an empty box.

## Windows

Tags, not workspaces, like in dwm. Each window has its own tags, each monitor has
its own tags, and you may have several at once.

Four layouts. Tiling layout with master area, floating, monocle, and tabbed that
takes ideas from i3 and sway. In the tabbed layout all members of the group are
drawn as one window and they become tabs in the titlebar, so you can have six
terminals in one corner. Toggle the layout off and get your previous layout
back.

Each window has its own titlebar, drawn with the same code as the bar. Click it
and focus that window.

Newly opened windows go to the bottom of the stack, not the master area, so
your second application lands next to the first one, not in front of it. You can
have gaps, you can have borders, and you can define the rules table for apps
that must be always floating and for those that must always be on tag 4.

## Glass

Windows are capable of transparency, with the active one being more opaque
than others so that one may easily determine their position visually.
Opacity can be controlled for everything, per app, and per window via some
key shortcuts. A particular key combination disables all transparency when
one needs to actually read anything.

A fancier implementation applies a blurred copy of the wallpaper under all
translucent windows, similarly to how it is done in macOS. It is a cheap
hack and it admits that fact. The wallpaper is only blurred once upon loading
and each window is getting a cropped version of that blurred wallpaper, so
nothing has to be blurred per frame, making its overhead virtually non-existent.
The cost is depth. A translucent window placed over another one still displays
wallpaper rather than the window below it.

## Wallpaper

g0wn loads and draws the wallpaper itself. Point it at an image and you are done. 
Build without it and you get a flat color plus whatever you start from autostart.

## Monitors

A different set of scaling, positioning, modality, rotation, and layout can be
assigned to each output, corresponding by name. The windows switch between displays, 
the focus changes, and using the single bar modality, the bar remains stationary but 
only communicates regarding the current focus.

## Credits

Most of this started as dwl, and a good part of it started as patches other
people wrote. They are all listed in [credits.md](credits.md).
