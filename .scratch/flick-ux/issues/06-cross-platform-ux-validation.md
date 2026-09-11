# 06: Cross-platform UX validation

**What to build:** The accepted Flick UX is verified as a coherent, accessible native utility on
Linux and macOS, with implementation-specific prototype questions resolved by evidence.

**Blocked by:** 02: Transient status and first-use feedback; 03: Command surfaces and Escape
precedence; 04: Transactional Settings dialog; 05: Live Image Information dialog; 07: Extract the
viewing-surface module; 08: Report finite animation completion truthfully; 09: Express native
primary actions; 10: Align the presentation contract and vocabulary; 11: Add Quit to application
command surfaces.

**Status:** ready-for-human

- [ ] Every presentation state and auxiliary surface is exercised at 480×320 and a typical desktop
      size under light and dark system chrome.
- [ ] High-DPI runs show no clipped labels, misplaced overlays, or unreachable dialog actions.
- [ ] Keyboard-only workflows, accessible names, native focus indication, and disabled-command
      discoverability pass the repository's application-level checks.
- [ ] Reduced-motion operation communicates every state change without depending on animation.
- [ ] Real Qt decode and repaint timings validate or adjust the approximately 120 ms loading delay
      without reopening the selected visual hierarchy.
- [ ] Native context-menu placement is verified near all screen edges on representative GNOME, KDE,
      and macOS environments.
- [ ] Any platform-specific adaptation preserves common behavior and viewing-surface geometry while
      following native menu, shortcut, button-ordering, and window conventions.

## Validation evidence

- `flick.application` exercises the accepted states, dialogs, command surfaces, accessibility,
  selected high-DPI/dark-palette variants, reduced-motion behavior, and screen-edge popup containment
  through the running application boundary. These checks support but do not complete the native
  visual and assistive-technology matrix.
- `flick.performance-smoke` keeps the performance probe executable in CI; representative reports now
  include an uncached open-to-visible Qt decode and repaint measurement for evaluating the 120 ms
  loading threshold.
- `docs/ux-validation.md` defines the native GNOME, KDE, and macOS matrix, required evidence, and the
  boundary between deterministic checks and real-desktop release acceptance. Platform-specific
  chrome may differ, while common presentation meaning and viewing-surface geometry may not.
- The acceptance criteria remain unchecked until the documented interactive runs are recorded on
  representative GNOME, KDE, and macOS desktops. Offscreen Qt containment and the local smoke timing
  are automated supporting evidence, not native-desktop or representative timing evidence.
- Review remediation tickets 07–11 must resolve before the native matrix is authoritative; their
  changes affect the surfaces and behavior this ticket validates.
- Responsive-fit process coverage now verifies resize and fullscreen transitions for Auto, Fit,
  Actual Size, and manual zoom against the final laid-out viewport. Native compositor-driven
  maximize and restore remain to be confirmed and recorded here on GNOME, KDE, and macOS; this
  automated evidence does not resolve the ticket.
