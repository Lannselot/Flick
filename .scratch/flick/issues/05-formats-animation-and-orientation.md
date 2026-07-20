# 05 — Supported formats, animation, and orientation

**What to build:** Flick renders the complete MVP format set, plays authored animations, and automatically presents oriented photographs upright.

**Blocked by:** 04 — Asynchronous decoding and bounded prefetch cache.

**Status:** ready-for-agent

- [ ] Valid JPEG, PNG, WebP, GIF, and BMP fixtures render with independently verified pixels and transparency where applicable.
- [ ] EXIF orientation is applied automatically to displayed content.
- [ ] Animated GIF and WebP preserve source frame delays and loop behavior.
- [ ] Space pauses an animation on its current frame and resumes playback.
- [ ] Static images remain unaffected by the animation control.
- [ ] Tests use fixed fixtures rather than reimplementing decoder behavior to derive expectations.
