# Features

## It's not DWL anymore

G0wm is based on DWL, but the goal of the project is different.
The codebase has been rewritten with a modern file structure, and the code itself is now formatted with clang-format.
The suckless style has been dropped in exchange for a fully functional desktop environment.

## Features

Here is a list of the main additions in g0wm:

- `config`: g0wm now has a fully dynamic config file, a JSON file at `~/.config/g0wm/settings.json`
- `opacity`: g0wm fully supports window opacity
- `borders`: window borders can be rounded
- `window titles`: g0wm shows a title bar on each window by default
- `tabbed mode`: g0wm has the tabbed mode from sway and i3
- `protocols`: many Wayland protocols have been implemented; touchscreens, drawing tablets and other features work out of the box
- `blur`: g0wm also supports blurred opacity, like frosted glass
- `squircle`: window borders can also be squircles
- `close button`: g0wm shows a close button on the title bar when hovered
- `move windows`: windows can be moved by dragging the title bar, with edge snapping
- `magnifier`: Super+scroll (by default) zooms the monitor, also by pinching on the touchpad
- `gestures`: a three-finger swipe on the touchpad switches tag
- `wallpaper`: g0wm draws the wallpaper by itself
- `xwayland`: g0wm supports X11 apps through Xwayland

- `statusbar`: the status bar from dwl is kept, and it reads the status text from a script (as in dwl)
- `notify`: notifications are displayed in the status bar with icons, priority levels and actions
- `runner`: Super+R (by default) opens the runner, which is displayed in the status bar
- `calculator`: the runner has a built-in calculator
- `systray`: the systray is displayed in the status bar with its menus
- `clickable status`: the status text can run commands when clicked
- `scroll`: scrolling on the tags switches tag, and scrolling on a title switches window

- `reload`: Super+Shift+R (by default) reloads the config file
- `inhibit`: Super+Esc (by default) turns the g0wm shortcuts off, for apps that need them
- `hide cursor`: the cursor is hidden while typing
- `autostart`: g0wm runs the autostart commands from the config file at startup

This is an overview of the main features, but there is much more in the config file.
