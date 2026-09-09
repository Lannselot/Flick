# Image Lifecycle Refactor Specification

Status: ready-for-agent

## Problem Statement

Flick's user-visible behaviour is well covered, but the implementation places image loading,
memory policy, browsing-sequence rules, and presentation state in one window module. A contributor
changing asynchronous decode, cache eviction, prefetch, or live directory refresh must understand
unrelated UI state and verify most results through a comparatively slow whole-process harness.
This raises regression risk and makes small policy changes expensive.

## Solution

Preserve Flick's current user experience while concentrating image loading and browsing-sequence
behaviour into two deep modules. The window remains the presentation coordinator and translates
module outcomes into existing empty, warning, error, animation, and image views. The migration is
incremental: introduce the new form beside the old behaviour, move one complete behaviour at a
time, and remove superseded state only after all callers use the new modules.

Defects discovered during migration are fixed only when reproduced by a regression test. The
refactor does not intentionally change formats, navigation rules, performance targets, platform
support, or visible interaction.

## User Stories

1. As a Flick user, I want images to open exactly as before, so that the refactor does not disrupt my workflow.
2. As a Flick user, I want the window to remain responsive during decoding, so that large files do not block interaction.
3. As a rapid-navigation user, I want an older decode result never to replace the current image, so that the display matches my selection.
4. As a user browsing adjacent images, I want prefetch to remain effective, so that navigation continues to feel immediate.
5. As a user with many large images, I want decoded memory to remain bounded, so that prolonged browsing remains stable.
6. As a user opening an exceptional image, I want the existing confirmation policy preserved, so that decoding cannot begin unexpectedly.
7. As a user opening damaged content, I want the existing error and retry behaviour preserved, so that one bad file does not stop browsing.
8. As an animation viewer, I want frame timing, loops, and orientation preserved, so that animated images still play as authored.
9. As a directory-browsing user, I want supported images naturally sorted as before, so that navigation order remains predictable.
10. As a drag-and-drop user, I want explicit sequences to remain distinct from directory-backed sequences, so that only intended images appear.
11. As a user whose directory changes externally, I want the current selection preserved when possible, so that refresh is not disruptive.
12. As a user whose current file disappears, I want the nearest remaining image selected with the existing feedback, so that browsing can continue.
13. As a contributor, I want loading policy testable through one interface, so that failures can be reproduced without driving the whole UI.
14. As a contributor, I want browsing-sequence invariants testable through one interface, so that sorting and reconciliation changes are safe.
15. As a release maintainer, I want all existing process and release checks retained, so that internal restructuring cannot silently change the product.

## Implementation Decisions

- Image loading is a deep module owning decode preparation, safety limits, animated metadata,
  worker scheduling, duplicate-request suppression, stale-result policy, decoded-memory accounting,
  eviction, and adjacent prefetch.
- The image-loading seam returns defined outcomes for success, decode failure, and confirmation
  required. Presentation widgets and translated user messages remain outside the module.
- Qt image reading is an internal implementation detail. No decoder seam is introduced because
  there is currently one real adapter.
- The browsing sequence is a deep module owning supported-path normalization, natural ordering,
  explicit-list deduplication, current selection, navigation limits, and reconciliation after
  directory changes.
- Filesystem watching remains a Qt adapter driven by the window. The browsing-sequence module
  consumes filesystem observations and returns selection outcomes; it does not own visible feedback.
- `ViewerWindow` retains actions, input translation, dialogs, platform integration, animation
  presentation, display color conversion, and viewport rendering.
- Existing externally visible semantics are the compatibility contract. Internal types and state
  may change freely when tests observe the same behaviour.
- Migration follows expand–migrate–contract. New and old forms may coexist temporarily, but every
  ticket must leave the full suite green.
- A newly discovered defect requires a red regression test at the highest suitable seam before its
  fix is included.
- Viewport geometry and test-driver protocol isolation remain roadmap candidates, not part of this
  implementation series.

## Testing Decisions

- A good test observes an image-loading or browsing-sequence outcome through that module's
  interface and does not inspect private collections, counters, worker objects, or Qt widget trees.
- Direct image-loading tests cover static and animated decoding, default sRGB assignment,
  orientation, malformed input, safety confirmation, stale completions, duplicate requests,
  eviction, cache-budget changes, retry, and prefetch reuse.
- Direct browsing-sequence tests cover supported-file filtering, canonicalization, deduplication,
  natural ordering, both sequence modes, navigation limits, selection preservation, file removal,
  rename, addition, and empty-directory recovery.
- Existing process tests remain the integration and behavioural-parity surface for responsiveness,
  screenshots, interactions, visible errors, settings, animation, color conversion, and filesystem
  changes.
- Existing performance smoke checks guard launch, decode responsiveness, high zoom, navigation,
  and large-directory behaviour.
- Linux builds must pass the complete local CTest suite and release verification. macOS-specific
  compilation and AppKit/ColorSync behaviour remain gated by CI and the documented release checks.
- Tests use fixed fixtures and temporary directories. Expected values must not reimplement the
  production algorithm under test.

## Out of Scope

- New image formats, remote sources, decoder plugins, or sandboxed decode processes.
- Changes to cache defaults, prefetch distance, large-image thresholds, or animation semantics.
- Changes to visible UI, shortcuts, settings, error copy, navigation order, or status behaviour.
- Reworking viewport geometry, input dispatch, platform adapters, or the test command protocol.
- Persisting temporary view state or modifying source images.
- Performance optimizations not required to preserve the documented targets.

## Further Notes

- The current process suite is the primary source for behavioural compatibility; direct module
  tests add diagnostic precision rather than replacing it.
- The preferred implementation order is image loading first, browsing sequence second, because
  both currently touch the same window state and serial migration reduces merge and review risk.
- The architecture decision is recorded in `docs/adr/0001-deepen-image-lifecycle.md`.
