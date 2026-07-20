# 06 — Zoom and pan viewing workflow

**What to build:** Users can move fluidly between an overview and pixel inspection with predictable initial scaling, cursor-centered zoom, and mouse or keyboard panning.

**Blocked by:** 04 — Asynchronous decoding and bounded prefetch cache.

**Status:** ready-for-agent

- [ ] Images larger than the viewport initially fit inside it, while smaller images initially display at 100%.
- [ ] The `1` key selects 100% and `F` fits the image to the viewport.
- [ ] Pointer-driven zoom preserves the image point beneath the cursor.
- [ ] A zoomed image pans by left-button drag and by Shift plus arrow keys.
- [ ] Plain arrow keys navigate images even while zoomed.
- [ ] Changing the current image reapplies the initial fit-or-100% policy.
