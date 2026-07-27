# macOS support audit

Date: 2026-07-27  
Scope: baseline repository state at `d845c74`  
Method: static source/build/test audit on Linux. No macOS binary was built or
run, so items marked “expected” still need verification on Apple Silicon and
Intel. Claims below use repository sources and first-party Qt/Apple
documentation only.

## Preparation update

The preparation stage following this audit has removed the direct Linux
dependencies from `ViewerWindow`:

- `PlatformServices` is now the seam for display-profile lookup and revealing a
  file in the system file manager.
- Linux X11/colord/D-Bus behavior lives in the default adapter; application
  tests use a controllable test adapter through the same interface.
- Qt DBus and XCB are conditional Linux build dependencies.
- Open, Zoom In, Zoom Out, and Preferences use standard `QKeySequence` values.

The remaining work is tracked in
[`17-full-macos-support.md`](../../.scratch/flick/issues/17-full-macos-support.md).
The baseline findings and evidence below are retained to explain that task's
scope; statements about unconditional DBus and Linux calls inside the window
describe `d845c74`, not the prepared worktree.

## Executive summary

Flick's Qt-based viewer core is largely portable to macOS: image decoding,
animation, navigation, zoom/pan/rotation, dialogs, drag-and-drop, clipboard,
settings, directory watching, asynchronous decode/cache, large-image
confirmation, and fullscreen are implemented with cross-platform Qt APIs.
Qt 6.5 officially supports macOS 11+ on `arm64` and `x86_64`
([Qt 6.5 supported platforms](https://doc.qt.io/qt-6.5/supported-platforms.html)).

The repository is **not release-ready for macOS**:

1. **P0 — no macOS application bundle/deployment path.** `flick` is not marked
   `MACOSX_BUNDLE`; install handles only a Unix-style runtime executable and
   does not deploy Qt frameworks/plugins. Qt says a macOS GUI app should be an
   application bundle and documents `MACOSX_BUNDLE`, `macdeployqt`, and Qt's
   deployment script for this purpose
   ([CMakeLists.txt](../../CMakeLists.txt),
   [Qt macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html),
   [`qt_generate_deploy_app_script`](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)).
2. **P0 — current color-managed rendering does not discover a macOS display
   color space.** The only discovery implementations are X11 `_ICC_PROFILE`
   and colord/D-Bus. Both fail on the Cocoa platform, causing the explicit sRGB
   fallback. Apple exposes each display's color space via `NSScreen.colorSpace`
   and the window's active space via `NSWindow.colorSpace`
   ([source](../../src/main.cpp),
   [Apple `NSScreen`](https://developer.apple.com/documentation/appkit/nsscreen),
   [Apple `NSWindow.colorSpace`](https://developer.apple.com/documentation/appkit/nswindow/colorspace)).
3. **P1 — “Show in File Manager” cannot reveal a file in Finder.** Release
   code calls only `org.freedesktop.FileManager1` over D-Bus. On macOS the
   native API is `NSWorkspace.activateFileViewerSelectingURLs`
   ([source](../../src/main.cpp),
   [Apple API](https://developer.apple.com/documentation/appkit/nsworkspace/1524549-activatefileviewerselectingurls)).
4. **P1 — Finder launch/file association is absent.** There is no bundle,
   `Info.plist` document-type declaration, or handling of Qt file-open events.
   Consequently, the supported path today is command-line invocation or the
   in-app picker/drop, not double-clicking an associated image in Finder.
5. **P1 — there is no signed/notarized distribution artifact or CI evidence.**
   Apple requires Developer ID signing, hardened runtime, secure timestamp, and
   notarization for modern direct distribution
   ([Apple notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)).

## Capability matrix

| Area | macOS status from current code | Evidence / qualification | Priority |
|---|---|---|---|
| Configure and compile | **Likely with a complete Qt installation**, but unverified | C++20, Qt Widgets/Concurrent/DBus and Clang-compatible warnings are portable. Qt DBus is Unix-only, which includes macOS, but making `DBus` required creates an unnecessary dependency for Linux-only integrations ([CMake](../../CMakeLists.txt), [Qt DBus module](https://doc.qt.io/qt-6/qtdbus-module.html)). Increase CMake floor from 3.21 to 3.21.1 because Qt's Apple CMake instructions require 3.21.1+ ([Qt deployment](https://doc.qt.io/qt-6/macos-deployment.html)). | P1 |
| Runnable development executable | **Expected** from the build tree | `qt_add_executable`, `QApplication`, and QWidget UI are supported on macOS. This is not equivalent to a distributable app. | P2 verify |
| Distributable `.app` | **Blocked** | No `MACOSX_BUNDLE`, bundle install destination, deployment script, icon, or bundle metadata ([CMake](../../CMakeLists.txt)). Qt states that macOS GUI apps use bundles and that runtime Qt frameworks/plugins must be deployed ([Qt deployment](https://doc.qt.io/qt-6/macos-deployment.html)). | P0 |
| JPEG/PNG/GIF/BMP/WebP decode | **Expected if image plugins are deployed** | Uses `QImageReader`; suffix allow-list is platform-neutral. Qt's macOS deploy tooling includes image format plugins, but the repository does not invoke it ([source](../../src/main.cpp), [Qt deployment plugin rules](https://doc.qt.io/qt-6/macos-deployment.html)). WebP must be explicitly smoke-tested in the packaged app. | P0 packaging / P2 behavior |
| EXIF orientation | **Expected unchanged** | `QImageReader::setAutoTransform(true)` is platform-neutral ([source](../../src/main.cpp)). | P2 verify |
| Animated GIF/WebP timing, loops, pause | **Expected unchanged** | Container parsing, `QImageReader`, and `QTimer` contain no OS-specific branch ([source](../../src/main.cpp)). | P2 verify |
| Embedded ICC profile and untagged-as-sRGB decode | **Expected unchanged** | Uses `QImage::colorSpace`, `QColorSpace`, and assigns sRGB to untagged frames. Qt documents ICC-backed `QColorSpace` and pixel conversion, while noting that not every ICC profile is supported ([source](../../src/main.cpp), [Qt `QColorSpace`](https://doc.qt.io/qt-6/qcolorspace.html), [Qt `QImage`](https://doc.qt.io/qt-6/qimage.html)). | P2 verify |
| Active-display color management | **Degraded: always sRGB fallback** | Cocoa is neither Qt's `xcb` platform nor a colord desktop. After both discovery calls fail, `applyDisplayColorSpace` selects sRGB and clears the rendered image's profile ([source](../../src/main.cpp)). macOS provides screen/window color spaces through AppKit ([`NSScreen`](https://developer.apple.com/documentation/appkit/nsscreen), [`NSWindow.colorSpace`](https://developer.apple.com/documentation/appkit/nswindow/colorspace)). | P0 |
| Re-render on monitor move | **Signal works; target profile does not** | `QWindow::screenChanged` calls refresh, but refresh has no macOS provider ([source](../../src/main.cpp)). Once a native provider exists, the cache design can re-render without decoding again. | P0 |
| Open dialog | **Expected unchanged/native** | `QFileDialog::getOpenFileName` is cross-platform; supported types are explicitly filtered ([source](../../src/main.cpp)). Verify the test selection hook because native dialogs are not QWidget dialogs. | P2 |
| Command-line path | **Expected unchanged** | First application argument is passed directly to the viewer ([source](../../src/main.cpp)). | P2 |
| Finder double-click / “Open With” / open while running | **Unsupported** | No app bundle document declarations and no `QFileOpenEvent` handling exist. | P1 |
| Drag one or multiple local images | **Expected unchanged** | Uses Qt URL drag/drop APIs and rejects non-local URLs ([source](../../src/main.cpp)). | P2 verify |
| Folder navigation and live refresh | **Expected unchanged** | Uses `QDir`, `QFileInfo`, `QCollator`, and `QFileSystemWatcher`. Qt explicitly supplies a macOS watcher backend ([Qt `QFileSystemWatcher`](https://doc.qt.io/qt-6/qfilesystemwatcher.html)). | P2 verify |
| Async decode, prefetch, LRU/cache limit | **Expected unchanged** | QtConcurrent/futures and in-process containers have no platform branch ([source](../../src/main.cpp)). | P2 verify |
| Large-image guard/retry/errors | **Expected unchanged** | Based on `QImageReader`, arithmetic, and Qt widgets ([source](../../src/main.cpp)). | P2 verify |
| Zoom, pointer-centered wheel zoom, pan, rotate | **Expected**, input semantics need Mac QA | Uses Qt input and paint APIs. Qt maps `Qt::ControlModifier`/`Qt::CTRL` to the Command key on Apple platforms, so documented `Ctrl` shortcuts render/operate as Command shortcuts ([Qt `QKeySequence`](https://doc.qt.io/qt-6/qkeysequence.html)). Trackpad pixel-delta direction and modifier behavior need hardware testing. | P2 |
| Keyboard shortcuts | **Functionally expected as Command shortcuts; docs/tests are Linux-centric** | Hard-coded `Qt::CTRL` becomes Command on macOS; README says `Ctrl`. Prefer standard keys where they exist and document platform-native names ([source](../../src/main.cpp), [Qt `QKeySequence`](https://doc.qt.io/qt-6/qkeysequence.html)). | P2 |
| Fullscreen/status/cursor | **Expected**, native behavior unverified | Uses QWidget/QWindow fullscreen and cursor APIs without OS branch ([source](../../src/main.cpp)). Test transitions on a real Cocoa window and multiple Spaces. | P2 |
| Clipboard image/path | **Expected unchanged** | Uses `QApplication::clipboard`; test harness exercises both operations ([source](../../src/main.cpp), [tests](../../tests/flick_application_test.cpp)). | P2 verify |
| Reveal in Finder | **Broken in production** | Test build bypasses integration and only records a path, so tests mask the problem. Production calls the freedesktop D-Bus service only ([source](../../src/main.cpp), [tests](../../tests/flick_application_test.cpp)). Apple provides a direct Finder reveal API ([Apple API](https://developer.apple.com/documentation/appkit/nsworkspace/1524549-activatefileviewerselectingurls)). | P1 |
| Settings and geometry | **Expected unchanged** | `QSettings` NativeFormat uses CFPreferences on macOS; XDG variables can override locations in tests ([Qt `QSettings`](https://doc.qt.io/qt-6/qsettings.html), [source](../../src/main.cpp)). README's “standard XDG configuration location” is incorrect for normal macOS execution. | P2 docs |
| Accessibility/native application UI | **Partial** | Important labels have accessible names, but the app has only a viewport context menu, no standard macOS menu bar/About/Preferences/Open menu. Qt Widgets use AppKit for macOS look and feel, though individual widgets are not native wrapped controls ([source](../../src/main.cpp), [Qt macOS specifics](https://doc.qt.io/qt-6.5/macos-issues.html)). | P2 |
| Signing/notarization | **Absent** | No bundle, signing identity/options, hardened-runtime configuration, entitlements, notarization, stapling, or validation workflow is present. Apple documents these as release requirements for Developer ID distribution ([Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)). | P1 |
| Universal binary | **Not configured** | No `CMAKE_OSX_ARCHITECTURES` release preset/CI matrix exists. Qt 6.5 supports both `arm64` and `x86_64`; choose separate artifacts or `arm64;x86_64` and verify dependencies match ([Qt supported platforms](https://doc.qt.io/qt-6.5/supported-platforms.html)). | P1 release |

## Build and runtime blockers in detail

### 1. Bundle and deployment

The current target:

```cmake
qt_add_executable(flick src/main.cpp)
install(TARGETS flick RUNTIME DESTINATION bin)
```

produces and installs the Unix-shaped executable expected by the Linux README.
For macOS, make the executable a bundle (either pass `MACOSX_BUNDLE` to
`qt_add_executable` or set the target property), provide bundle identifier,
version, icon and `Info.plist` metadata, install with `BUNDLE DESTINATION .`,
then install the script generated by `qt_generate_deploy_app_script`. Qt's
deployment tooling copies the Cocoa platform plugin and image-format plugins;
without those, a copied standalone binary will not be a self-contained release
([Qt macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html),
[`qt_generate_deploy_app_script`](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)).

### 2. Linux-only integration is unconditional

`Qt6::DBus` and the D-Bus headers are required and linked on every Unix build,
although both consumers are Linux desktop integrations:

- colord display-profile lookup;
- freedesktop file-manager `ShowItems`.

On macOS these calls do not provide the intended services. Guard them behind a
Linux build definition and make Qt DBus conditional. This reduces build and
deployment surface and prevents a Linux IPC dependency from being mistaken for
a macOS implementation. Qt describes D-Bus as a Unix IPC module originally
developed for Linux ([Qt D-Bus overview](https://doc.qt.io/QT-6/qtdbus-index.html)).

### 3. macOS display color management

The decode side is portable, but presentation currently does:

1. query X11;
2. query colord;
3. transform to sRGB if neither returns a valid profile;
4. clear `QImage` color-space metadata before painting.

Thus a P3 or calibrated Mac display never becomes the explicit target. Add a
small macOS Objective-C++ adapter that obtains the active Qt window's
`NSWindow`/`NSScreen`, reads its `NSColorSpace`, exports ICC data into
`QColorSpace`, and feeds the existing `applyDisplayColorSpace` seam. Use the
window's actual screen instead of correlating displays by localized name.
Retain `QWindow::screenChanged` refresh and add refresh coverage for display
configuration/profile changes. Apple documents that `NSScreen` describes a
monitor and exposes `colorSpace`; `NSWindow.colorSpace` exposes the window's
backing color space
([`NSScreen`](https://developer.apple.com/documentation/appkit/nsscreen),
[`NSWindow.colorSpace`](https://developer.apple.com/documentation/appkit/nswindow/colorspace)).

The implementation must explicitly choose one color-management owner. The
present architecture manually converts pixels and then removes their
`QColorSpace`; therefore the macOS adapter should preserve that contract and
must be validated against Preview/ColorSync reference output to exclude double
conversion.

### 4. Finder integration and launch events

Use `NSWorkspace.activateFileViewerSelectingURLs` for reveal on macOS and keep
the D-Bus path for Linux. The test seam should inject the platform operation or
its result rather than compiling out the real branch; the current harness
asserts only that a path was recorded and cannot catch a broken platform
integration.

To act like a Mac image viewer, declare the six supported document types in the
bundle metadata and handle Qt's file-open application event. Otherwise launch
from Finder and “Open With” remain missing even after creating a bundle.

## Test portability gaps

The single CTest target is a valuable end-to-end harness, but it is not proof of
Cocoa support:

- CTest forces `QT_QPA_PLATFORM=offscreen`, so it never loads the Cocoa platform
  plugin and cannot validate native input, windowing, fullscreen, menus,
  display/profile changes, Finder, native dialogs, Retina scaling, or Spaces
  ([tests/CMakeLists.txt](../../tests/CMakeLists.txt)).
- The harness sets XDG directories. Qt allows XDG overrides on macOS, so
  isolation is plausible, but production `QSettings` normally uses
  CFPreferences; add a test of the normal macOS storage path/round trip
  ([Qt `QSettings`](https://doc.qt.io/qt-6/qsettings.html)).
- `copiesPathAndRenderedImageAndExposesContextCommands` expects literal
  `Ctrl+C`, `Ctrl+Shift+C`, and `Ctrl+Shift+R`, while the harness emits
  `QKeySequence::NativeText`. Native text on Apple is platform-specific and
  `Qt::CTRL` maps to Command, so these assertions are expected to fail
  ([test](../../tests/flick_application_test.cpp),
  [Qt `QKeySequence`](https://doc.qt.io/qt-6/qkeysequence.html)).
- File-picker automation looks for an active `QFileDialog` widget. A native
  macOS panel need not be represented by that active QWidget. The offscreen
  test can remain a non-native logic test, but a separate manual/GUI test must
  verify the native picker.
- Reveal tests compile out the real D-Bus/Finder behavior. Display-profile tests
  inject an ICC file and do not test discovery. Both need injectable
  platform-service interfaces plus platform-specific integration smoke tests.
- Screenshot pixel/layout assumptions may vary with fonts, scale factors, and
  color handling. Keep deterministic offscreen tests for viewer logic, and add
  a small Cocoa smoke suite rather than weakening all assertions.
- There is no macOS CI job. Add at least `macos-14`/Apple Silicon where
  available, configure/build/test/install, inspect bundle dependencies, launch
  the installed `.app`, and verify packaged JPEG/PNG/GIF/WebP/BMP decoding.
  Add Intel or a universal-artifact check according to the release target.

## Recommended implementation order

### P0 — establish a truthful, runnable Mac build

1. Add an Apple CMake branch: `MACOSX_BUNDLE`, bundle identifiers/version/icon,
   `BUNDLE DESTINATION .`, and Qt's deployment script. Raise the CMake minimum
   to at least 3.21.1.
2. Make DBus conditional on Linux; retain XCB only on X11-capable Linux.
3. Add a macOS display-color-space provider at the existing
   `refreshDisplayColorSpace` seam and validate tagged P3, untagged sRGB, and
   monitor moves against a trusted macOS color-managed viewer.
4. Add macOS CI that builds both the test executable and installed bundle, then
   launches/tests the deployed artifact.

### P1 — complete desktop integration and release safety

5. Implement Finder reveal with `NSWorkspace`.
6. Add document-type declarations and file-open event handling.
7. Make tests platform-neutral for shortcut text and move real external
   operations behind injectable interfaces.
8. Produce a signed Developer ID build with hardened runtime and timestamp;
   notarize, staple, and verify it. Apple requires these protections for modern
   direct distribution
   ([Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)).
9. Decide Intel + Apple Silicon release policy and build/verify the selected
   architecture set.

### P2 — native quality and documentation

10. Add the standard macOS application menu (Open, Preferences/Settings, About,
    Quit), use platform standard key sequences where applicable, and update the
    README to show Command shortcuts on macOS.
11. Replace Linux-only README language (“minimal ... for Linux”, XDG settings)
    with platform-specific installation and settings notes.
12. Perform a real-device matrix: Retina/non-Retina, trackpad/mouse, single and
    dual display with distinct profiles, light/dark mode, fullscreen/Spaces,
    native dialogs, drag/drop, clipboard, directory mutation, animations, and
    all packaged decoders.

## Acceptance gate for claiming macOS support

- Clean configure/build on supported Xcode with Qt 6.5+ on `arm64`; Intel or
  universal build according to policy.
- Installed, self-contained `.app` launches on a clean Mac without a developer
  Qt installation.
- All deterministic CTest cases pass after platform-neutral fixes.
- Cocoa smoke tests pass for open dialog, Finder reveal, clipboard, fullscreen,
  drag/drop, settings, and file-open events.
- Packaged app opens all six advertised formats, including animated GIF/WebP.
- Tagged/untagged color fixtures match a trusted macOS reference on at least
  one wide-gamut display; colors update correctly between two differently
  profiled displays.
- `codesign` verification, Gatekeeper assessment, notarization, and stapling
  succeed for the final artifact.
