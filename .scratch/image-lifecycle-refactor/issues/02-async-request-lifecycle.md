# 02: Move the asynchronous request lifecycle

**What to build:** Flick keeps the UI responsive and presents only the currently requested image
while the image-loading module owns worker scheduling, duplicate-request suppression, completion,
retry, and stale-result decisions.

**Blocked by:** 01 — Introduce tested decode outcomes.

**Status:** resolved

- [x] Direct tests deterministically exercise concurrent requests, duplicate suppression, stale completion, retry, and cancellation-by-obsolescence.
- [x] Rapid navigation cannot allow an older completion to replace the current image.
- [x] Loading, warning, failure, and success outcomes still drive the existing visible states.
- [x] Responsiveness and process-level decode tests pass without weaker assertions or increased timeouts.

## Comments

Implemented by moving scheduling, in-flight suppression, current-request tracking, retry, and stale
completion filtering into `ImageLoading::Loader`. The existing process behavior remains the
compatibility boundary; the full Linux CTest suite passes.
