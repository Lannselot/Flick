# 06: Contract legacy state and verify releases

**What to build:** Flick completes the refactor with one authoritative image-loading lifecycle and
one authoritative browsing sequence, with no obsolete parallel state and no user-visible change.

**Blocked by:** 03 — Move decoded cache and prefetch policy; 05 — Move navigation and directory reconciliation.

**Status:** ready-for-agent

- [ ] Old loading, cache, prefetch, sequence, and selection paths are deleted rather than retained as compatibility code.
- [ ] The window contains presentation coordination but no duplicated loading or browsing-sequence policy.
- [ ] Architecture documentation describes the final modules and their seams using the project glossary.
- [ ] Linux build, complete CTest suite, performance smoke checks, and release verification pass locally.
- [ ] macOS build and platform tests remain required CI gates, with any unavailable local checks reported explicitly.
- [ ] A final code review finds no standards or specification regressions.
