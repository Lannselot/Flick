# Responsive image fitting

Status: ready-for-human

## Problem

When Flick starts without an image, the user can resize or maximize the empty window and then open
an image. The displayed image can retain a scale calculated for an earlier, smaller viewport. The
same stale scale is visible when a fitted image is followed by a window resize or a transition to
or from fullscreen: the viewing surface grows, but the image remains small and centered.

The dark margins required to preserve an image's aspect ratio are expected. The defect is that the
image does not continue to use the maximum available area while its zoom behavior is automatic.

## Desired behavior

Zoom has an explicit policy as well as a numeric scale:

- **Auto** is selected whenever a new image becomes current. Its scale is
  `min(100%, fit-to-current-viewport)`, so small images remain sharp at native size and large images
  remain entirely visible.
- **Fit** is selected by the Fit to Window command. Its scale is the current fit-to-viewport scale;
  unlike Auto, it may enlarge a small image beyond 100% because the user explicitly requested it.
- **Fixed** is selected by Actual Size and by every manual zoom interaction. Its numeric scale is
  preserved when the window changes size.

While Auto or Fit is active, resize, maximize/restore, and fullscreen transitions recompute the
scale from the final laid-out image viewport. While Fixed is active, those transitions preserve the
user's scale and only update centering and scroll ranges.

Opening or navigating to another image always returns to Auto. Temporary rotation keeps the
existing product behavior; this correction must not silently redefine rotation or pan semantics.

## Interaction details

- Initial scaling is calculated only after the Displayed presentation owns the laid-out viewport.
- A square image in a landscape window remains centered with side margins; it fills the available
  height in Auto or Fit when height is the limiting dimension.
- Auto never enlarges an image above its decoded pixel dimensions.
- Explicit Fit can enlarge an image and remains responsive to later viewport changes.
- Actual Size sets 100% and remains 100% across viewport changes.
- Zoom In, Zoom Out, Ctrl+wheel zoom, and any other manual zoom path switch to Fixed before applying
  their requested scale.
- Resize handling must not create feedback loops, repeated status messages, or unbounded image
  allocations. Rendering continues to draw only the visible region at high zoom.

## Acceptance criteria

- [x] Starting empty, resizing or maximizing the window, and then opening a large image uses the
      final displayed viewport for the initial Auto scale.
- [x] Enlarging and shrinking the window recomputes the scale while Auto is active, capped at 100%.
- [x] Fit to Window tracks subsequent viewport changes and may enlarge a small image beyond 100%.
- [x] Actual Size and manual zoom remain numerically stable across window and fullscreen changes.
- [x] Opening and navigating to another image restore Auto behavior.
- [x] Images remain centered and aspect-correct; unavoidable letterboxing is not treated as a
      failure to fit.
- [x] Existing zoom anchoring, panning, rotation, animation, high-zoom allocation, status, and image
      information behavior remain covered and green.

## Testing strategy

Exercise the behavior through the running Flick process and its typed window test seam. Use fixed
image dimensions and query the visible viewport and reported zoom after the UI has settled. Expected
scales must be derived from the public rule and observed viewport dimensions, with an appropriate
rounding tolerance; tests must not depend on private widget names or event ordering.

At minimum cover:

1. empty launch → resize → drop/open a large image;
2. large image in Auto → grow viewport → shrink viewport;
3. small image in Auto → grow viewport, proving the 100% cap;
4. explicit Fit on a small image → resize;
5. Actual Size and manual zoom → resize;
6. Auto and Fixed behavior across fullscreen entry/exit where the offscreen platform can observe it;
7. navigation after manual zoom, proving that the next image returns to Auto.

Native confirmation of maximize/restore behavior belongs in the existing cross-platform UX
validation matrix; deterministic process tests remain the automated regression gate.

## Out of scope

- Stretching an image without preserving aspect ratio.
- Cropping the image to eliminate letterboxing.
- Persisting zoom policy or numeric zoom between application launches.
- Changing the default window size or the optional window-geometry preference.
- Redesigning rotation, pan, animation, or the status overlay.
