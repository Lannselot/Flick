# 01: Establish the ViewerWindow seam

**What to build:** Flick has a dedicated window module whose interface supports normal application
startup and file-open events, while the process entry point contains only construction and wiring.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] Move the existing window implementation behind a dedicated module without changing visible
      behavior, shortcuts, object identity relied on by accessibility, or platform behavior.
- [x] Keep application creation, platform adapter selection, initial-path handling, and process
      startup in the entry point.
- [x] Preserve the existing application-test protocol during this expand step so the full suite
      remains a compatibility gate.
- [x] Update build inputs for production and test-driver executables without duplicating window
      implementation.
- [x] The complete test suite and performance smoke check pass after the move.
