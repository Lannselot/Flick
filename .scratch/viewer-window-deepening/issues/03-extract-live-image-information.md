# 03: Extract live Image Information

**What to build:** A user retains a compact, non-modal and live Image Information dialog while its
formatting and lifecycle live behind one module fed by a typed current-image snapshot.

**Blocked by:** 01: Establish the ViewerWindow seam.

**Status:** resolved

- [x] Define one typed snapshot containing the current path, file facts, decoded availability,
      dimensions, view state, animation state, and browsing-sequence position required for display.
- [x] The Image Information module owns formatting, the scrollable native dialog, accessibility
      metadata, selection behavior, updates, raising, and close lifecycle.
- [x] The module does not receive `ViewerWindow`, image-loader, browsing-sequence, or viewing-surface
      pointers and does not reconstruct their policies.
- [x] Browsing, zooming, rotation, loading failure, and natural animation completion update an open
      dialog exactly as before without stealing viewing focus.
- [x] Existing application-level information tests pass through the running process.
