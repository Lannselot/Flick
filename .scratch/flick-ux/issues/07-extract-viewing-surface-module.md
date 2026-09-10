# 07: Extract the viewing-surface module

**What to build:** Maintainers can evolve Flick's mutually exclusive presentation states, transient
feedback, and drop feedback behind one deep viewing-surface module while `ViewerWindow` remains a
small coordinator of user events, the browsing sequence, and image-loading outcomes.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] Empty, Loading, Displayed, Load error, Large-image confirmation, and Drop target preserve all
      currently observable behavior through the running-application test boundary.
- [x] The module's interface expresses state transitions without exposing its widget inventory,
      timers, layout bookkeeping, or animation implementation to `ViewerWindow`.
- [x] Beginning a current-image load has one implementation shared by selection, retry, and approved
      large-image flows, including filename, delayed indicator, information refresh, and decoding.
- [x] Status overlay content, first-use teaching, short feedback, and drag feedback remain local to
      the viewing surface rather than becoming parallel state in the coordinator.
- [x] Image loading and browsing-sequence ownership remain in their existing deep modules; this
      prefactor does not change product behavior or introduce a hypothetical adapter.
- [x] Existing application and performance tests remain green at every committed migration step.

## Implementation guidance

Use expand–migrate–contract so each commit remains buildable. Prefer one module with a small
state-oriented interface over a set of shallow wrappers around individual Qt widgets. The deletion
test should show that removing the module would force presentation complexity back into the
coordinator.

## Comments

- Extracted `ViewingSurface` as the owner of presentation states, delayed loading, transient status
  and teaching, reduced-motion fades, and reversible drop feedback. `ViewerWindow` now coordinates
  browsing, decoding outcomes, and view commands through a state-oriented interface.
- Unified selection, retry, and approved-large-image entry through one current-image loading path.
  The production build and all 10 CTest targets pass, including `flick.application` and
  `flick.performance-smoke`.
