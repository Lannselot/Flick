# 06: Cross-platform UX validation

**What to build:** The accepted Flick UX is verified as a coherent, accessible native utility on
Linux and macOS, with implementation-specific prototype questions resolved by evidence.

**Blocked by:** 02: Transient status and first-use feedback; 03: Command surfaces and Escape
precedence; 04: Transactional Settings dialog; 05: Live Image Information dialog.

**Status:** ready-for-agent

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

