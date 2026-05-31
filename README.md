# hyprland-modmove

[Hyprland](https://hypr.land) plugin that moves or resizes the floating
window under the cursor (without clicking). This is a hyprland version
of [modmove](https://github.com/keith/modmove) for macOS.

## Usage

There are 2 things this plugin can do:

1. To move the window under the cursor, hold Control (⌃) and Alt (⎇)
   (Option (⌥) on an Apple keyboard), then move the mouse
2. To resize the window under the cursor, hold Control (⌃) and Alt (⎇)
   (Option (⌥) on an Apple keyboard) and Shift (⇧), then move the mouse.

## Installation

```sh
hyprpm update
hyprpm add https://github.com/keith/hyprland-modmove
hyprpm enable hyprbars
hyprpm reload -n
```

### Development

#### Build

```sh
make
```

#### Load

```sh
hyprctl plugin load "$PWD/modmove.so"
```
