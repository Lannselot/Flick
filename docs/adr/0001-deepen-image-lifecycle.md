---
status: accepted
---

# Keep presentation separate from image lifecycle state

Flick will keep `ViewerWindow` responsible for translating user and platform events into visible
presentation, while deep modules own image loading and the browsing sequence. This preserves the
existing process-level behaviour while creating direct test seams for asynchronous loading,
memory policy, and sequence invariants; a single broad window implementation was rejected because
those policies currently share mutable UI state and can only be verified through expensive
whole-process tests.

## Consequences

The migration uses expand–migrate–contract so every intermediate commit remains buildable and the
existing process tests remain the behavioural compatibility gate. Qt decoding stays inside the
image-loading implementation until a second real adapter justifies another seam.
