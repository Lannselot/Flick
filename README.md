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

Large images initially fit inside the window, while smaller images open at
100%. Press `1` for 100% or `F` to fit the image. The mouse wheel browses the
image sequence by default, while `Ctrl` plus the wheel zooms around the pointer.
The viewport context menu can swap those primary and alternate wheel actions.
Drag with the left mouse button to pan, or pan with `Shift` plus an arrow key.
Plain left and right arrows continue to browse the image sequence at any zoom
level.

## License

Copyright (C) 2026 Flick contributors

Flick is free software licensed under the GNU General Public License version 3
or (at your option) any later version. See [LICENSE](LICENSE).
