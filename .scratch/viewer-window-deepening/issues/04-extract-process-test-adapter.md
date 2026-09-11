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
- [x] The adapter crosses one explicit window inspection/control seam using a small set of typed
      capability snapshots and semantic operations rather than one method per protocol query or
      internal widget.
- [x] Test-only hooks remain compile-time excluded from the production executable and cannot change
      normal runtime behavior when unused.
- [x] Every existing application and performance command continues to work without changing its
      observable semantics during this migration.
- [x] A build-level check proves the production target does not include the adapter source.
- [x] Only the adapter matches textual command names, parses command fields, and serializes protocol
      replies; window, viewing-surface, Settings, and Image Information code return typed values.
- [x] Test control operations name semantic commands or action roles rather than private widgets,
      and snapshots describe capabilities without exposing widget pointers or control-tree layout.
- [x] Settings test parsing and serialization move out of the Settings module; deterministic
      environment overrides live in test-driver wiring or are passed as typed injected inputs.
- [x] Architecture tests reject protocol-shaped `QByteArray` description methods and widget-named
      operations outside the adapter instead of merely checking that its source is absent from the
      production target.

## Review reopening

Independent review found that the adapter owns stdin dispatch but not the complete protocol. Query
serialization remains in the window, Settings still parses and formats test values, and the window
test-control interface mirrors dozens of protocol queries and individual widgets. The completed
criteria above remain checked; the incomplete contract criteria are reopened and block final
contraction.

## Implementation sequence

1. Introduce typed snapshots for the capability families named in the specification alongside the
   current forwarding interface; keep every process command green.
2. Move protocol response formatting into the adapter one capability family at a time, then remove
   the corresponding string-description methods.
3. Replace widget-named controls with semantic action-role operations and migrate the adapter.
4. Move Settings test-value parsing and protocol formatting into the adapter; retain only typed
   Settings values across the seam.
5. Move deterministic environment interpretation to test-driver wiring or convert it to typed
   injected configuration before window construction.
6. Contract all forwarding compatibility methods and strengthen the architecture test before
   requesting final review.

Each step must preserve the running-process protocol so the already split application suites remain
green throughout the migration.
