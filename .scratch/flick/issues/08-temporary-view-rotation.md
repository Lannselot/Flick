# 08 — Safe temporary view rotation

**What to build:** Users can temporarily rotate the displayed image for inspection without creating an edit or affecting other images.

**Blocked by:** 05 — Supported formats, animation, and orientation; 06 — Zoom and pan viewing workflow.

**Status:** ready-for-agent

- [ ] `L` and `R` rotate the current view left and right in 90-degree increments.
- [ ] Rotation composes predictably with automatic orientation, zoom, fit, and pan.
- [ ] Navigating to another image discards the temporary rotation.
- [ ] Returning to a previously viewed image does not restore temporary rotation.
- [ ] The source file and its metadata remain byte-for-byte unchanged.
