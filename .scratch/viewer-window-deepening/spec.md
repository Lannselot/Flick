# Viewer Window Deepening Specification

Status: resolved

## Problem Statement

Flick's image loading, browsing sequence, and viewing surface now live behind explicit seams, but
`ViewerWindow` still shares one large source file with process startup and the test command loop. It
also owns construction, transactional state, persistence, and lifecycle for Settings and Image
Information. The process-level test suite reaches this broad class through many test-only query
methods and has accumulated unrelated behavior in one fixture.

The current design passes all automated checks and conforms to the UX specification. This effort is
therefore a behavior-preserving deepening, not a redesign or a prerequisite for the still-open native
UX validation ticket `.scratch/flick-ux/issues/06-cross-platform-ux-validation.md`.

## Outcome

`ViewerWindow` becomes a focused coordinator of user and platform events, the browsing sequence,
image-loading outcomes, view state, and the viewing surface. Settings editing, Image Information,
and the process test protocol move behind cohesive seams. Process tests remain the compatibility
gate but are organized by capability and no longer require a command dispatcher inside the
production entry point.

## Design Decisions

### Establish the window seam before deepening children

Move `ViewerWindow` into its own module and reduce `main.cpp` to application construction, platform
adapter selection, initial path handling, and process startup. This first step is mechanical and
must not change the window's interface or behavior. It gives subsequent modules and the test adapter
a stable place to integrate without adding more conditional code to the entry point.

### Settings editor

The Settings module owns:

- construction and lifecycle of the native dialog;
- the opening snapshot and editable values;
- control-to-value conversion;
- live preview dispatch;
- Reset Defaults, Apply, and Cancel transaction semantics;
- persistence of accepted values through the platform-native settings backend.

Its interface accepts the opening/default values and a preview operation, then reports completion.
It must not receive the image loader, viewing surface, or `ViewerWindow` internals. Runtime
application of a preview remains coordination work because it spans navigation, appearance, cache,
and window behavior.

### Image Information

The Image Information module owns fact formatting, native dialog construction, live updates,
empty/unavailable descriptions, selection behavior, and dialog lifecycle. Its interface consumes
one typed snapshot of the current image and view state rather than a growing list of primitives or
pointers to `ViewerWindow` internals.

The coordinator constructs the snapshot because it owns view state and combines outcomes from the
browsing sequence and image loading. The dialog decides how those facts are presented.

### Test adapter

The process test protocol is a genuine seam because production and test-driver executables already
vary at it. A test-only adapter owns stdin dispatch, replies, screenshot requests, synthetic input,
and deterministic test controls. The production executable must not compile the adapter.

`ViewerWindow` exposes the smallest inspection/control interface needed by the adapter. Prefer
typed snapshots or existing public commands over one getter per widget. Test-only interface remains
behind `FLICK_ENABLE_TEST_HARNESS`; production behavior must not depend on it.

#### Accepted test seam contract

The adapter is the only module that knows the line-oriented process protocol. It owns command-name
matching, argument parsing, `QByteArray` reply formatting, separators and field order, error replies,
and translation between protocol values and typed application values. Changing protocol spelling or
serialization must not require editing `ViewerWindow`, `ViewingSurface`, Settings, or Image
Information.

Inspection crosses the window seam as a small set of typed capability snapshots rather than one
method per protocol query or widget. The intended capability families are:

- runtime/loading and cache state;
- view and presentation state;
- command-surface actions, ordering, availability, and focus;
- Settings values and dialog transaction state;
- Image Information and accessibility state;
- window, screen, popup, and displayed-content geometry.

These are capability families, not a required one-struct-per-line design. An implementation may
combine them when that reduces the interface without coupling unrelated changes. Snapshots contain
typed values such as enums, booleans, numbers, rectangles, action descriptors, and lists; they do
not contain preformatted protocol replies.

Control operations express user or test intent, such as activating a named command, selecting a
presentation action, applying typed Settings values, sending an input event, or requesting a
capture. They must not name private widgets (`focusDetails`, `toggleDetails`, or equivalent), expose
widget pointers, or mirror the control tree. When a test needs to distinguish primary and secondary
actions, it selects the semantic action role and observes a typed presentation snapshot.

Settings and other production modules may expose a minimal typed inspection hook under
`FLICK_ENABLE_TEST_HARNESS` only when the same outcome cannot be observed at the window seam. They
must not parse test protocol fields, serialize protocol responses, or provide direct setters for
individual controls. Deterministic environment overrides belong to test-driver construction or the
adapter unless they are genuine injected inputs to a production module.

The following shapes are explicitly rejected:

- a forwarding method for every text command;
- `QByteArray` description methods whose field order is part of the process protocol;
- dialog-structure, focus, or state strings assembled by production modules;
- control methods named after private widgets;
- moving the existing dispatcher to another file while leaving its serialization in the window.

### Test organization

Keep application-level behavior at the running-process interface. Split the monolithic suite by
coherent capability while sharing process startup, temporary environment, fixture creation,
command transport, screenshot capture, and common assertions. Do not replace end-to-end coverage
with tests of private Qt widgets.

## Invariants

- The UX/UI specification and ADR 0002 remain unchanged.
- Presentation states remain owned by `ViewingSurface`.
- Image loading remains owned by `ImageLoading::Loader`.
- Browsing paths and selection remain owned by `BrowsingSequence`.
- Each invocation remains one process with one independent window.
- Settings retain live preview, Apply commit, Cancel rollback, and Reset Defaults preview.
- Image Information remains non-modal, live during browsing, and keyboard operable.
- The production executable remains free of network activity.
- All existing automated behavior and performance checks remain green after each slice.
- Native UX validation ticket 06 stays open and is neither replaced nor marked complete here.

## Verification Strategy

- Run the complete CTest suite after every migration slice.
- Preserve tests through the running Flick process rather than asserting private representation.
- Compare available application-test coverage before and after splitting so no scenario disappears.
- Build the production executable without `FLICK_ENABLE_TEST_HARNESS` and verify the test adapter is
  absent from its sources and linked symbols.
- Run the performance smoke check after the final contraction to detect accidental startup or
  navigation regressions.

## Out of Scope

- New user-visible behavior, visual restyling, or changes to command placement.
- Completing or weakening the native GNOME/KDE/macOS validation matrix.
- Replacing Qt Widgets or introducing a general UI framework.
- Extracting tiny wrappers around individual controls.
- Changing image-loading, browsing-sequence, cache, color-management, or platform-service policy.
- Adding a second production window type, dependency-injection framework, or hypothetical adapter.
- Security sandboxing of image decoders.

## Completion Conditions

- `main.cpp` contains process entry and wiring rather than the window implementation or test command
  loop.
- `ViewerWindow` coordinates the established deep modules without owning Settings or Image
  Information widget trees and transaction state.
- The test command protocol is implemented in a test-only adapter.
- No production module or window test-control seam parses process commands or constructs serialized
  protocol replies; inspection crosses the seam as typed capability snapshots and semantic actions.
- Application tests are split by capability and reuse one process-driver implementation.
- All repository tests pass and architecture documentation describes the resulting ownership.
- A final review finds no behavior drift against this specification or the Flick UX/UI
  specification.

## References

- Domain language: `../../CONTEXT.md`
- Presentation architecture: `../../docs/adr/0001-deepen-image-lifecycle.md`
- Content-first UI decision: `../../docs/adr/0002-content-first-presentation.md`
- UX/UI specification: `../flick-ux/spec.md`
- Open native validation gate: `../flick-ux/issues/06-cross-platform-ux-validation.md`
