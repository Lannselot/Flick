# 03: Move decoded cache and prefetch policy

**What to build:** Flick retains bounded decoded memory and fast adjacent navigation while the
image-loading module owns cache accounting, recency, eviction, budget changes, and prefetch reuse.

**Blocked by:** 02 — Move the asynchronous request lifecycle.

**Status:** resolved

- [x] Direct tests cover insertion, reuse, recency, eviction, oversized entries, budget reduction, and protection of the current request.
- [x] Previous and next browsing-sequence entries are prefetched without redundant decoding.
- [x] The configured cache budget continues to apply immediately and persist through existing settings behaviour.
- [x] Existing cache, performance, responsiveness, and memory-oriented tests pass.
- [x] Superseded loading, cache, and in-flight state is removed from the window implementation.
