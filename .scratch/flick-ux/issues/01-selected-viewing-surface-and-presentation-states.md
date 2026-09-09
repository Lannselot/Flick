# 01: Selected viewing surface and presentation states

**What to build:** Flick presents the accepted centered-native viewing surface and a coherent family
of Empty, Loading, Load error, Large-image confirmation, and drop-target states while preserving
browsing and maximizing image area.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] The normal window has one fixed dark viewing surface with no permanent toolbar, sidebar,
      status bar, or navigation controls.
- [ ] Empty, Loading, Load error, and Large-image confirmation use the accepted Variant A centered
      composition at both 480×320 and a typical desktop size.
- [ ] Navigating to an uncached image removes the preceding image immediately, while the loading
      indicator appears only after approximately 120 ms.
- [ ] Load error offers Retry and expandable Details in-surface, and adjacent navigation remains
      available.
- [ ] Large-image confirmation reports dimensions and estimated memory, supports Open anyway and
      Skip, and leaves adjacent navigation available.
- [ ] Valid drag feedback remains legible over empty, dark-image, and bright-image content and
      restores the preceding state when the drag leaves or is cancelled.
- [ ] Presentation-state transitions respect reduced-motion preferences.

