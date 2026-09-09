# 05: Move navigation and directory reconciliation

**What to build:** Flick preserves navigation and current selection as directories change while the
browsing-sequence module owns movement limits and reconciliation outcomes.

**Blocked by:** 04 — Introduce the browsing-sequence module.

**Status:** ready-for-agent

- [ ] Direct tests cover previous and next movement, both boundaries, preserved selections, removal of the current image, rename, addition, and empty recovery.
- [ ] Directory-backed sequences react to watcher observations while explicit sequences remain unchanged.
- [ ] The nearest remaining image and visible feedback match existing behaviour when the current file disappears.
- [ ] The window translates sequence outcomes into presentation without mutating sequence internals.
- [ ] Existing navigation and live-filesystem process tests pass.
