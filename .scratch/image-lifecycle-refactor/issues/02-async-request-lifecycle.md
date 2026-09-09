# 02: Move the asynchronous request lifecycle

**What to build:** Flick keeps the UI responsive and presents only the currently requested image
while the image-loading module owns worker scheduling, duplicate-request suppression, completion,
retry, and stale-result decisions.

**Blocked by:** 01 — Introduce tested decode outcomes.

**Status:** ready-for-agent

- [ ] Direct tests deterministically exercise concurrent requests, duplicate suppression, stale completion, retry, and cancellation-by-obsolescence.
- [ ] Rapid navigation cannot allow an older completion to replace the current image.
- [ ] Loading, warning, failure, and success outcomes still drive the existing visible states.
- [ ] Responsiveness and process-level decode tests pass without weaker assertions or increased timeouts.
