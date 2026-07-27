# 14 — Color-managed rendering

**What to build:** Flick presents deterministic, color-managed output from tagged and untagged images and follows the active display profile when the desktop exposes one.

**Blocked by:** 05 — Supported formats, animation, and orientation.

**Status:** resolved

- [x] Embedded ICC profiles are honored during rendering.
- [x] Images without a profile are interpreted as sRGB.
- [x] When an active display profile is available, displayed colors are transformed for that profile.
- [x] Moving the window between differently profiled displays updates rendering when the platform exposes the change.
- [x] Automated fixtures validate deterministic profile handling, with documented manual checks for visual correctness on profiled displays.
