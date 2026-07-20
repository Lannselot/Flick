# Flick

Flick is a minimal Qt 6 image viewer for Linux.

## Build

Flick requires a C++20 compiler, CMake 3.21 or newer, and Qt 6.5 or newer with
the Widgets component.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Open a JPEG or PNG by passing its path, or launch without a path for the empty
state:

```sh
./build/flick photo.jpg
./build/flick
```

To build and run the application-level tests, include Qt Test and enable CTest:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests launch real, independent Flick processes with controlled image
fixtures. Each process receives private temporary XDG config, data, cache,
state, and runtime directories.

## License

Copyright (C) 2026 Flick contributors

Flick is free software licensed under the GNU General Public License version 3
or (at your option) any later version. See [LICENSE](LICENSE).
