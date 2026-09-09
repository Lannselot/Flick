# 05: Move navigation and directory reconciliation

**What to build:** Flick preserves navigation and current selection as directories change while the
browsing-sequence module owns movement limits and reconciliation outcomes.

**Blocked by:** 04 — Introduce the browsing-sequence module.

**Status:** resolved

- [x] Direct tests cover previous and next movement, both boundaries, preserved selections, removal of the current image, rename, addition, and empty recovery.
- [x] Directory-backed sequences react to watcher observations while explicit sequences remain unchanged.
- [x] The nearest remaining image and visible feedback match existing behaviour when the current file disappears.
- [x] The window translates sequence outcomes into presentation without mutating sequence internals.
- [x] Existing navigation and live-filesystem process tests pass.

## Comments

Moved navigation limits and directory reconciliation into `BrowsingSequence`, including defined
movement and reconciliation outcomes. The window now translates those outcomes into decoding,
status, boundary, empty-state, and removal feedback; direct and process-level coverage passes.
