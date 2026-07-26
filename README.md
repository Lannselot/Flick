# Flick

Flick is a minimal Qt 6 image viewer for Linux.

## Build

Flick requires a C++20 compiler, CMake 3.21 or newer, and Qt 6.5 or newer with
the Widgets component.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Open a JPEG, PNG, WebP, GIF, or BMP by passing its path, or launch without a
path for the empty state:

```sh
./build/flick photo.jpg
./build/flick
```

While Flick is running, press `Ctrl+O` to choose an image with the system file
picker. Dropping one image opens its containing directory for navigation;
dropping several files creates a sequence from only the supported images in the
drop.

To build and run the application-level tests, include Qt Test and enable CTest:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests launch real, independent Flick processes with controlled image
fixtures. Each process receives private temporary XDG config, data, cache,
state, and runtime directories.

Image files are decoded on worker threads. Flick prefetches the previous and
next images in the current sequence and keeps decoded images in an automatically
evicted cache with an approximately 512 MB default budget.

Animated GIF and WebP files play with their authored frame timing and loop
behavior. Press `Space` to pause on the current frame and press it again to
resume. Photographs with EXIF orientation metadata are presented upright
automatically.

If an image cannot be read, Flick keeps the browsing sequence available and
shows an in-window explanation with optional technical details. Press `F5` to
retry the current file. Images declaring more than 100 megapixels or an
estimated decoded allocation over 1 GB require confirmation before Flick starts
decoding them.

Large images initially fit inside the window, while smaller images open at
100%. Press `1` for 100%, `F` to fit the image, or `Ctrl++` and `Ctrl+-` to
zoom in and out. The mouse wheel browses the image sequence by default, while
`Ctrl` plus the wheel zooms around the pointer.
The viewport context menu can swap those primary and alternate wheel actions.
Drag with the left mouse button to pan, or pan with `Shift` plus an arrow key.
Plain left and right arrows continue to browse the image sequence at any zoom
level. Press `L` or `R` to rotate only the current view left or right by 90
degrees. This temporary rotation is discarded when another image is selected
and never changes the source file.

Press `F11` or double-click the image to toggle fullscreen mode. Press `Esc` to
return to the normal window. In fullscreen, the status display and mouse
pointer hide after a short period of inactivity and reappear when the mouse
moves.

Press `Ctrl+,` or choose Settings from the viewport context menu to configure
the wheel action, viewport background, transient status display, decoded-image
cache budget, and optional window geometry restoration. Changes take effect
immediately and are saved in the standard XDG configuration location. The open
dialog also returns to the last directory from which an image was selected.

Press `I` to inspect the current file's path, format, dimensions, size,
modification time, zoom, and sequence position. `Ctrl+C` copies the rendered
image, `Ctrl+Shift+C` copies its path, and `Ctrl+Shift+R` shows the file's
directory in the system file manager. These commands and their shortcuts are
also available from the viewport context menu.

## License

Copyright (C) 2026 Flick contributors

Flick is free software licensed under the GNU General Public License version 3
or (at your option) any later version. See [LICENSE](LICENSE).
