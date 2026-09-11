# 01: Track zoom policy and refit against the current viewport

**What to build:** Flick keeps automatically fitted images fitted as the viewing surface changes,
while preserving an explicitly chosen manual or 100% scale.

**Blocked by:** None (can start immediately).

**Status:** ready-for-human

- [x] Add a failing application-level regression for empty launch → resize/maximize-sized viewport
      → open a large image, proving that initial Auto scale uses the final displayed viewport.
- [x] Represent Auto, Fit, and Fixed zoom policies explicitly instead of inferring intent from the
      current floating-point scale.
- [x] Recompute Auto and Fit after the image viewport receives its final resize, coalescing layout
      events if necessary.
- [x] Keep Auto capped at 100%; allow explicit Fit to enlarge small images.
- [x] Make Actual Size and every manual zoom entry point select Fixed before changing scale.
- [x] Return to Auto whenever a different image becomes current.
- [x] Cover viewport growth and shrinkage, fullscreen transitions, small-image capping, explicit
      Fit, Actual Size, manual zoom, and navigation through the running-process boundary.
- [x] Preserve cursor-centered zoom, panning, centering, rotation, status/information updates, and
      the visible-region high-zoom rendering constraint.
- [x] Run the focused presentation tests, the complete CTest suite, and `git diff --check`.
- [ ] Record native maximize/restore confirmation as evidence in UX validation ticket 06 without
      resolving that ticket.

## Implementation notes

The current scale is calculated in `ViewerWindowImplementation::applyInitialZoom()` from
`fitZoom()`, but there is no resize path that reapplies an automatic policy. The initial calculation
also occurs before the viewing surface switches to Displayed. Keep widget event handling shallow:
the resize signal or callback should tell the window coordinator that the viewport changed, while
the coordinator owns zoom policy and rendering decisions.

Avoid treating every resize as Fit. Doing so would overwrite a user's Actual Size or manual zoom.
Avoid using equality between the current zoom and a previously calculated fit scale to infer the
mode; rounding and coincidental equality make that contract unstable.

## Comments

- Prepared from the reported first-launch screenshot. The centered aspect-correct margins are
  expected; the stale 47% scale in a much larger viewport is the regression target.
- Automated running-process coverage is implemented and green. Native maximize/restore still needs
  confirmation through ticket 06, so this ticket is handed to a human with that checkbox open.

## Answer

Zoom intent is now explicit (`Auto`, `Fit`, or `Fixed`). Coalesced viewport resize handling refits
automatic policies after layout, keeps Auto capped at native size, lets explicit Fit enlarge, and
preserves Actual Size and manual zoom across resizing and fullscreen transitions. Navigating or
opening another current image restores Auto. Process-level regressions cover the policy matrix and
the empty-launch resize/open defect without weakening the visible-region high-zoom renderer.
