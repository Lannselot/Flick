# 17 — Full macOS support

**What to build:** Flick is a native, distributable, color-managed macOS image
viewer with feature parity across the supported viewing workflows.

**Status:** ready-for-human

**Preparation already available:** `PlatformServices` isolates display-profile
lookup and file-manager reveal; Linux-only DBus/XCB dependencies are conditional;
Open, Zoom, and Preferences use platform-standard `QKeySequence` values.

- [x] Amend the product specification so macOS is a supported target rather
  than explicitly out of scope, and define the minimum macOS/Qt versions.
- [x] Decide whether releases are universal (`arm64` + `x86_64`) or separate
  architecture artifacts and record the decision.
- [x] Build `flick` as a `MACOSX_BUNDLE` with bundle identifier, version, icon,
  usage metadata, supported document types, and `BUNDLE DESTINATION`.
- [x] Package Qt frameworks plus Cocoa and image-format plugins using Qt's
  deployment tooling; the installed `.app` opens JPEG, PNG, GIF, BMP, and WebP
  on a Mac without a developer Qt installation.
- [x] Add an Objective-C++ `PlatformServices` adapter that obtains the active
  window/display ICC data through AppKit/ColorSync and preserves the existing
  single-owner color-conversion contract.
- [ ] Moving the window between differently profiled displays refreshes the
  current frame without decoding it again and matches Preview/ColorSync for
  tagged and untagged fixtures.
- [x] Implement Finder reveal through
  `NSWorkspace.activateFileViewerSelectingURLs`.
- [x] Handle `QFileOpenEvent` so Finder double-click, Open With, and opening a
  document while Flick is already running all establish the correct sequence.
- [x] Add a standard macOS application menu with Open, Settings, About, and
  Quit; user documentation names Command-based shortcuts.
- [x] Make application tests platform-neutral for native shortcut text and
  settings locations while retaining deterministic offscreen coverage.
- [ ] Add Cocoa smoke tests for the native picker, Finder reveal, file-open
  events, clipboard, drag-and-drop, fullscreen/Spaces, Retina scaling, and
  display-profile changes.
- [x] Add macOS CI that builds, tests, installs, launches, and inspects the
  deployed `.app` on the selected architecture set.
- [x] Configure Developer ID signing, hardened runtime, secure timestamp,
  notarization, stapling, and Gatekeeper verification. Credentials remain a
  human/release-secret responsibility.
- [ ] Document and execute a real-device matrix covering Apple Silicon,
  supported Intel hardware if applicable, mouse and trackpad, light/dark mode,
  single and dual wide-gamut displays, every supported format, animations,
  settings, directory updates, errors, and large-image confirmation.

## Comments

- 2026-08-13: Human decisions confirmed: minimum macOS 13, minimum Qt 6.5,
  and one universal `arm64` + `x86_64` release artifact.
- 2026-08-13: Implementation is ready for macOS validation. Remaining gates
  require Apple hardware and release credentials: Preview/ColorSync comparison
  on differently profiled displays, the full Cocoa/real-device smoke matrix,
  and a successful Developer ID signing/notarization/stapling/Gatekeeper run.

## References

- [`docs/research/macos-support-audit.md`](../../../docs/research/macos-support-audit.md)
- [Qt: Deploying an Application on macOS](https://doc.qt.io/qt-6/macos-deployment.html)
- [Apple: Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
