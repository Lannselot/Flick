# 04: Extract the process test adapter

**What to build:** Application tests control and inspect a running Flick test driver through a
test-only adapter, while the production entry point and production executable contain no command
dispatcher or stdin protocol.

**Blocked by:** 01: Establish the ViewerWindow seam; 02: Extract the transactional Settings editor;
03: Extract live Image Information.

**Status:** resolved

- [x] Move stdin parsing, command dispatch, query serialization, screenshot requests, synthetic
      input, and deterministic controls into a source compiled only for the test driver.
- [x] Production startup has no test-protocol branches and does not link the test adapter.
- [x] The adapter crosses one explicit window inspection/control seam and prefers typed snapshots or
      existing commands over one method per internal widget.
- [x] Test-only hooks remain compile-time excluded from the production executable and cannot change
      normal runtime behavior when unused.
- [x] Every existing application and performance command continues to work without changing its
      observable semantics during this migration.
- [x] A build-level check proves the production target does not include the adapter source.
