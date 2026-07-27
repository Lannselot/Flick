# 17 — Full macOS support

**What to build:** Flick is a native, distributable, color-managed macOS image
viewer with feature parity across the supported viewing workflows.

**Blocked by:** 14 — Color-managed rendering.

**Status:** ready-for-human

**Preparation already available:** `PlatformServices` isolates display-profile
lookup and file-manager reveal; Linux-only DBus/XCB dependencies are conditional;
Open, Zoom, and Preferences use platform-standard `QKeySequence` values.

- [ ] Amend the product specification so macOS is a supported target rather
  than explicitly out of scope, and define the minimum macOS/Qt versions.
- [ ] Decide whether releases are universal (`arm64` + `x86_64`) or separate
  architecture artifacts and record the decision.
- [ ] Build `flick` as a `MACOSX_BUNDLE` with bundle identifier, version, icon,
  usage metadata, supported document types, and `BUNDLE DESTINATION`.
- [ ] Package Qt frameworks plus Cocoa and image-format plugins using Qt's
  deployment tooling; the installed `.app` opens JPEG, PNG, GIF, BMP, and WebP
  on a Mac without a developer Qt installation.
- [ ] Add an Objective-C++ `PlatformServices` adapter that obtains the active
  window/display ICC data through AppKit/ColorSync and preserves the existing
  single-owner color-conversion contract.
- [ ] Moving the window between differently profiled displays refreshes the
  current frame without decoding it again and matches Preview/ColorSync for
  tagged and untagged fixtures.
- [ ] Implement Finder reveal through
  `NSWorkspace.activateFileViewerSelectingURLs`.
- [ ] Handle `QFileOpenEvent` so Finder double-click, Open With, and opening a
  document while Flick is already running all establish the correct sequence.
- [ ] Add a standard macOS application menu with Open, Settings, About, and
  Quit; user documentation names Command-based shortcuts.
- [ ] Make application tests platform-neutral for native shortcut text and
  settings locations while retaining deterministic offscreen coverage.
- [ ] Add Cocoa smoke tests for the native picker, Finder reveal, file-open
  events, clipboard, drag-and-drop, fullscreen/Spaces, Retina scaling, and
  display-profile changes.
- [ ] Add macOS CI that builds, tests, installs, launches, and inspects the
  deployed `.app` on the selected architecture set.
- [ ] Configure Developer ID signing, hardened runtime, secure timestamp,
  notarization, stapling, and Gatekeeper verification. Credentials remain a
  human/release-secret responsibility.
- [ ] Document and execute a real-device matrix covering Apple Silicon,
  supported Intel hardware if applicable, mouse and trackpad, light/dark mode,
  single and dual wide-gamut displays, every supported format, animations,
  settings, directory updates, errors, and large-image confirmation.

## References

- [`docs/research/macos-support-audit.md`](../../../docs/research/macos-support-audit.md)
- [Qt: Deploying an Application on macOS](https://doc.qt.io/qt-6/macos-deployment.html)
- [Apple: Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
