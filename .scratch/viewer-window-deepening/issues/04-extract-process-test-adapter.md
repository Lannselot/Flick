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

## Answer

The adapter now owns command matching, argument parsing, reply serialization, screenshots, and
synthetic input. Inspection crosses one window seam as typed capability snapshots; controls use
semantic operations and presentation action roles rather than widget names. Settings exposes typed
values, and deterministic environment configuration is interpreted during test-driver wiring.

The process protocol remained stable throughout the expand–migrate–contract sequence. Architecture
checks cover production exclusion, typed ownership, renamed protocol serializers, widget-specific
control declarations, and line-protocol output outside the adapter.

## Comments

- An independent review reopened this ticket after the first extraction because stdin dispatch had
  moved but query serialization, Settings parsing, and widget-shaped controls remained distributed.
- The reopened criteria were completed in the follow-up extraction and verified through all split
  running-process suites.
