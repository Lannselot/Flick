# 01 — Runnable viewer and application-level test harness

**What to build:** A minimal C++20 and Qt 6 Flick application that launches as an independent process, opens a static JPEG or PNG supplied on the command line, and establishes the application-level test and fixture conventions used by later work.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] A launch with a valid JPEG or PNG displays the image in a top-level Flick window.
- [ ] A launch without an image displays a stable neutral empty state.
- [ ] Separate invocations create independent processes and windows.
- [ ] Automated tests launch the real application with controlled image fixtures and an isolated temporary XDG environment.
- [ ] The project builds as C++20 against Qt 6 and carries the GPL-3.0-or-later licensing baseline.
