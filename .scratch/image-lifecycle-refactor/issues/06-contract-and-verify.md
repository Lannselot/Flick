# 06: Contract legacy state and verify releases

**What to build:** Flick completes the refactor with one authoritative image-loading lifecycle and
one authoritative browsing sequence, with no obsolete parallel state and no user-visible change.

**Blocked by:** 03 — Move decoded cache and prefetch policy; 05 — Move navigation and directory reconciliation.

**Status:** ready-for-agent

- [x] Old loading, cache, prefetch, sequence, and selection paths are deleted rather than retained as compatibility code.
- [x] The window contains presentation coordination but no duplicated loading or browsing-sequence policy.
- [x] Architecture documentation describes the final modules and their seams using the project glossary.
- [ ] Linux build, complete CTest suite, performance smoke checks, and release verification pass locally.
- [x] macOS build and platform tests remain required CI gates, with any unavailable local checks reported explicitly.
- [x] A final code review finds no standards or specification regressions.

## Comments

- Contracted the last duplicated selection state from `ViewerWindow`; `BrowsingSequence` now
  remains the sole authority for the selected path of the current image. Updated the architecture
  diagrams and module-seam documentation to match the final implementation.
- Local Linux verification on 2026-09-09: production and test builds passed; all 10 CTest targets
  passed, including `flick.performance-smoke`, release layout, and network-dependency checks; a
  production Release archive was built successfully. Full AppImage verification could not run
  because this host has neither `linuxdeploy` nor `strace`, so the combined Linux verification
  checkbox remains open.
- macOS compilation and platform tests were unavailable on this Linux host. The `macos-14` CI job
  still builds the universal bundle, runs CTest (including the AppKit/ColorSync platform test),
  and verifies the installed bundle.
- Final two-axis review against the ticket/spec and repository standards found no remaining
  findings after documentation corrections.
