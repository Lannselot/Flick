# 01: Selected viewing surface and presentation states

**What to build:** Flick presents the accepted centered-native viewing surface and a coherent family
of Empty, Loading, Load error, Large-image confirmation, and drop-target states while preserving
browsing and maximizing image area.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] The normal window has one fixed dark viewing surface with no permanent toolbar, sidebar,
      status bar, or navigation controls.
- [x] Empty, Loading, Load error, and Large-image confirmation use the accepted Variant A centered
      composition at both 480×320 and a typical desktop size.
- [x] Navigating to an uncached image removes the preceding image immediately, while the loading
      indicator appears only after approximately 120 ms.
- [x] Load error offers Retry and expandable Details in-surface, and adjacent navigation remains
      available.
- [x] Large-image confirmation reports dimensions and estimated memory, supports Open anyway and
      Skip, and leaves adjacent navigation available.
- [x] Valid drag feedback remains legible over empty, dark-image, and bright-image content and
      restores the preceding state when the drag leaves or is cancelled.
- [x] Presentation-state transitions respect reduced-motion preferences.

## Comments

- Implemented the selected centered-native presentation family as mutually exclusive states inside
  one `#181A1B` viewing surface. A short opacity reveal is suppressed when the platform disables
  widget animation, and the deterministic test harness exercises that reduced-motion path.
- Added process-level coverage for state content, the 120 ms loading threshold and stale-pixel
  removal, reversible drag feedback, large-image actions, and both minimum and typical sizes.
