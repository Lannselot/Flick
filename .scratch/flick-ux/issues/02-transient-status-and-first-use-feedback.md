# 02: Transient status and first-use feedback

**What to build:** Flick communicates image context and brief feedback through the accepted quiet
pill overlay without introducing permanent chrome or a second toast system.

**Blocked by:** 01: Selected viewing surface and presentation states.

**Status:** resolved

- [x] The bottom-centered pill uses the accepted Variant C treatment and normally reports filename,
      sequence position, and zoom.
- [x] Open, navigation, zoom, rotation, and pointer movement reveal the overlay, which fades without
      flashing and remains legible without blur.
- [x] Long filenames truncate safely at 480×320 instead of expanding or clipping the viewing
      surface.
- [x] Sequence-boundary, copy-success, and similar brief feedback temporarily reuse the same overlay
      slot before normal status returns.
- [x] The first displayed image teaches browsing and the context menu once, and successful use marks
      that teaching complete.
- [x] First fullscreen entry teaches F11 or Escape in the same feedback system without adding a
      control bar.
- [x] Overlay behavior and optional transitions respect status-visibility and reduced-motion
      settings.
