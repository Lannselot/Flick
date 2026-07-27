# 15 — Linux desktop integration and accessibility

**What to build:** Flick behaves as a native, keyboard-accessible image viewer across the supported Linux desktop environments and display systems.

**Blocked by:** 03 — File picker and drag-and-drop sequences; 09 — Fullscreen and transient status UI; 10 — Image information and safe external actions; 13 — Immediately applied and persisted XDG settings; 14 — Color-managed rendering.

**Status:** ready-for-agent

- [ ] Every core viewing action can be completed without a mouse and visible focus is preserved where controls are present.
- [ ] System theme colors, Qt accessibility metadata, and high-DPI scaling are honored.
- [x] User-facing English strings are structured for future localization.
- [ ] Wayland and X11 runs demonstrate equivalent externally visible core behavior.
- [x] Desktop entry and MIME metadata allow supported images to be opened from common Linux file managers.
- [x] Documented manual checks cover GNOME and KDE integration where compositor or file-manager behavior is not reliably automatable.
