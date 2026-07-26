# 12 — Recoverable errors, retry, and large-image guard

**What to build:** Malformed, inaccessible, transiently unavailable, or exceptionally large images cannot crash or freeze the browsing session, and users retain an informed recovery path.

**Blocked by:** 04 — Asynchronous decoding and bounded prefetch cache; 05 — Supported formats, animation, and orientation.

**Status:** resolved

- [x] Decode and access failures produce an in-window error while adjacent navigation remains usable.
- [x] Optional technical details can be expanded without replacing the user-facing explanation.
- [x] F5 retries the current file and can recover after a transient failure is resolved.
- [x] Declared dimensions above 100 megapixels or estimated decoded allocation above 1 GB require explicit confirmation before decode.
- [x] Rejecting the warning avoids the excessive allocation; approving it still decodes away from the UI thread.
- [x] Tests cover malformed, truncated, permission-denied, extreme-dimension, and recoverable fixtures.
