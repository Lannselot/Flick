# Cross-platform UX validation

Flick's deterministic application checks cover shared behavior; native desktop
checks cover the parts that an offscreen Qt backend cannot truthfully verify.
Both are required before a release candidate is accepted. Record the operating
system, desktop/compositor, Qt version, theme, scale, display arrangement, and
result for every native run.

## Automated application boundary

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen FLICK_EXECUTABLE=build/flick_test_driver \
  build/tests/flick_performance > performance.json
```

The application test exercises the presentation-state family, drop feedback,
loading delay, status and teaching overlays, Settings, Image Information,
keyboard-only command paths, Escape precedence, accessible names and roles,
disabled command visibility, selected light/dark and 100%/200% variants, and
minimum 480 × 320 dialog bounds. It does not exercise every Cartesian product
of state, size, theme, and scale; the native matrix below does. The automated
test also opens the complete context menu at all four screen corners and
requires Qt's resulting popup frame to remain inside the screen's available
geometry.

The test process requests reduced motion, so its state assertions provide
supporting evidence that state changes remain observable with optional opacity
transitions disabled. Native reduced-motion behavior remains part of the matrix
below. The performance report's
`uncached_open_visible` measurement includes real asynchronous Qt decoding and
the repaint that makes a newly dropped, non-prefetched image visible. Use its
three-run median when evaluating the 120 ms loading-indicator delay.

## Native desktop matrix

Use a 480 × 320 window and an approximately 1280 × 800 window. Repeat with
light and dark system chrome and at 100%, 150% (where supported), and 200%
scale. Exercise Empty, Loading, Displayed, Load error, Large-image confirmation,
and Drop target, then normal/fullscreen status, the context menu, Settings, and
Image Information. Include a filename long enough to elide.

| Environment | Required sessions/hardware |
| --- | --- |
| GNOME | Current supported release, Wayland and X11, one and two displays |
| KDE Plasma | Current supported release, Wayland and X11, one and two displays |
| macOS | Current supported release on Retina Apple Silicon; universal bundle verification covers Intel architecture |

For each combination verify:

- every label is readable; overlays remain placed inside the viewing surface;
  dialog actions are visible and reachable;
- Tab and Shift+Tab follow native focus order and show native focus indication;
  every core workflow completes without a pointer;
- a screen reader announces the viewing surface, transient status, warnings,
  dialog fields, and enabled or disabled command state;
- reduced-motion mode removes optional fades without removing feedback or
  changing Escape behavior;
- context menus remain wholly reachable at every edge and corner of every
  display, including displays with negative virtual-desktop coordinates;
- Linux uses Control labels and desktop menu/button conventions; macOS uses
  Command labels, the global application menu, native button order, Spaces,
  and standard window behavior;
- the image-area geometry and meaning of every presentation state remain the
  same across platforms even where native chrome differs.

Attach screenshots for the two sizes and two themes, the median performance
JSON, and a short exception note for any platform-specific adaptation. A CI
build or offscreen screenshot is supporting evidence, not a substitute for
this native pass.
