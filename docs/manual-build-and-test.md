# Manual build and verification

This guide describes how to build the current Flick source tree and perform a
manual pre-release check on Linux. It complements the automated test suite:
CTest checks repeatable application behavior, while the manual pass checks the
real window system, desktop integration, interaction, and packaged output.

## 1. Install prerequisites

Required tools and libraries:

- a C++20 compiler;
- CMake 3.21.1 or newer;
- Ninja or another CMake-supported build tool;
- Qt 6.5 or newer with Concurrent, Widgets, DBus, and image-format plugins;
- pkg-config and XCB development files for X11 color-profile support.

For Arch Linux and derivatives, the usual package set is:

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-imageformats libxcb
```

Package names differ between distributions. In particular, ensure the Qt image
format plugins are installed: Flick may build without every runtime image
plugin but then fail to open formats such as WebP.

Confirm the source revision and check that local changes are intentional:

```sh
git status --short --branch
git rev-parse HEAD
```

Record the commit hash with the manual test results.

## 2. Build the production application

Use a dedicated build directory so test instrumentation cannot accidentally
enter a release artifact:

```sh
cmake -S . -B build/manual-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build/manual-release
```

The production executable is:

```text
build/manual-release/flick
```

Run it both with and without an input file:

```sh
./build/manual-release/flick
./build/manual-release/flick /absolute/path/to/image.webp
```

Running from the build directory is the fastest developer check, but it does
not prove that installation or packaging is complete.

## 3. Build and run the automated tests

Configure a separate instrumented build:

```sh
cmake -S . -B build/manual-test -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON
cmake --build build/manual-test
ctest --test-dir build/manual-test --output-on-failure
```

`BUILD_TESTING=ON` creates `flick_test_driver`, which exposes the private test
harness. Never package that executable as the production application. The
release builder rejects an instrumented `flick` binary as an additional guard.

All tests must pass before beginning the manual checklist. If a test fails,
rerun only that test verbosely with:

```sh
ctest --test-dir build/manual-test -R TEST_NAME --output-on-failure -V
```

Replace `TEST_NAME` with the name shown in the failed CTest result.

## 4. Check the install tree

Install into a staging directory rather than into the operating system:

```sh
cmake --install build/manual-release \
  --prefix "$PWD/build/manual-install"
find build/manual-install -type f -print
```

The staging tree must contain:

```text
bin/flick
share/applications/org.flick.Flick.desktop
share/metainfo/org.flick.Flick.metainfo.xml
share/icons/hicolor/scalable/apps/org.flick.Flick.svg
share/licenses/Flick/LICENSE
```

Launch the staged executable:

```sh
./build/manual-install/bin/flick /absolute/path/to/image.png
```

This verifies installation paths, but the staged tree still relies on the
system Qt installation. Use the AppImage check below to verify a bundled build.

## 5. Prepare test images

Use known-good samples containing:

- JPEG, PNG, WebP, GIF, and BMP files;
- an animated GIF and an animated WebP;
- a JPEG with EXIF orientation;
- an image with an embedded ICC color profile;
- a corrupt or truncated file with a supported extension;
- multiple supported images in one directory;
- at least one unsupported file among supported files;
- a very large image that triggers the decode confirmation guard.

Keep these fixtures outside the source tree unless they are intended to become
test assets. Never use irreplaceable originals for manual testing.

## 6. Run the interactive smoke checklist

Perform the following checks using `build/manual-release/flick`:

- launch without a path and open a file with `Ctrl+O`;
- launch with a file path and drop one file onto the window;
- drop several mixed supported and unsupported files;
- open JPEG, PNG, WebP, GIF, and BMP images;
- confirm that animated GIF and WebP playback can be paused and resumed with
  `Space`;
- browse forward and backward with the arrow keys and mouse wheel;
- verify 100% zoom, fit-to-window, zoom around the pointer, and panning;
- rotate left and right and confirm the source file is unchanged;
- enter and leave fullscreen with `F11`, double-click, and `Esc`;
- inspect image information, copy the rendered image, copy its path, and reveal
  it in the file manager;
- change settings, restart Flick, and verify the expected settings persist;
- open the corrupt file, inspect the error details, replace or repair it, and
  retry with `F5`;
- confirm the large-image warning appears before decoding the oversized image;
- close the application normally and confirm that no Flick process remains.

Also verify keyboard focus, readable text, icons, menus, and window resizing at
the desktop's normal and high-DPI scale factors. The broader desktop matrix is
documented in [`linux-integration.md`](linux-integration.md).

## 7. Check Wayland and X11

Identify the current session:

```sh
printf '%s\n' "$XDG_SESSION_TYPE"
```

Run the interactive checklist in real Wayland and X11 sessions when preparing a
release. Confirm on both sessions that file dialogs, clipboard operations,
fullscreen, drag and drop, file-manager reveal, scaling, and shutdown work.
Forcing `QT_QPA_PLATFORM` inside one session is useful for diagnosis but is not
a substitute for logging into the other display-server session.

For a release candidate, cover Ubuntu, Fedora, and Arch under both Wayland and
X11 as required by [`releasing.md`](releasing.md).

## 8. Build a development archive

The archive-only path needs no linuxdeploy installation:

```sh
packaging/linux/build-release.sh \
  --archive-only \
  build/manual-release \
  build/manual-artifacts
```

Inspect the result:

```sh
tar -tzf build/manual-artifacts/Flick-*-linux-x86_64.tar.gz
```

The archive is a staged development artifact and still depends on a compatible
system Qt. Its filename version is read from `CMakeLists.txt`.

## 9. Build and verify an AppImage

AppImage creation is supported on x86_64 Linux and additionally requires:

- linuxdeploy;
- the linuxdeploy Qt plugin discoverable by linuxdeploy;
- `strace`, used by the offline verification script.

Point `LINUXDEPLOY` at the reviewed linuxdeploy executable when it is not on
`PATH`:

```sh
LINUXDEPLOY=/absolute/path/to/linuxdeploy \
  packaging/linux/build-release.sh \
  build/manual-release \
  build/manual-artifacts
```

The builder creates the archive and AppImage, then automatically runs
`packaging/linux/verify-release.sh`. Run the resulting AppImage directly for
the real desktop checklist:

```sh
chmod +x build/manual-artifacts/Flick-*-linux-x86_64.AppImage
build/manual-artifacts/Flick-*-linux-x86_64.AppImage
```

Prefer testing the AppImage on a machine without a developer Qt installation.
That catches missing bundled libraries and plugins which are hidden on the
build machine. The pinned CI tool versions and complete artifact gates are
documented in
[`performance-and-release.md`](performance-and-release.md).

## 10. Record the result

For each manual pass, retain:

- commit hash and Flick version;
- compiler, CMake, Qt, kernel, distribution, and desktop versions;
- display server (`wayland` or `x11`) and scale factor;
- whether the build, CTest, staged install, archive, and AppImage checks passed;
- the test-image formats and notable metadata used;
- failed checklist items, logs, screenshots, and reproduction steps;
- SHA-256 checksums of any candidate artifacts that were distributed.

Do not treat a failed manual check as an informal exception. Fix it through a
pull request or document an explicit release decision before creating the tag.
