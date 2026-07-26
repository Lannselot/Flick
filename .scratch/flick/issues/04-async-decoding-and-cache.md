# 04 — Asynchronous decoding and bounded prefetch cache

**What to build:** Image loading stays responsive while Flick decodes away from the UI thread, prefetches adjacent images, and bounds decoded memory with automatic eviction.

**Blocked by:** 02 — Directory sequence and keyboard navigation.

**Status:** resolved

- [x] Loading and navigation remain responsive while image decoding is in progress.
- [x] The previous and next sequence items are prefetched when available.
- [x] Navigation to a prefetched item uses the ready result without redundant visible loading.
- [x] Decoded content observes an approximately 512 MB default cache budget and is evicted automatically.
- [x] Rapid navigation cannot allow stale decode results to replace the current image.
- [x] Process-level tests exercise responsiveness and bounded memory under cache churn.
