# 13 — Immediately applied and persisted XDG settings

**What to build:** Users can tune core viewing behavior once and have those choices apply immediately and survive later launches using standard Linux configuration storage.

**Blocked by:** 07 — Configurable wheel action; 09 — Fullscreen and transient status UI.

**Status:** ready-for-agent

- [ ] Settings cover wheel action, viewport background, status visibility, cache budget, and optional window geometry restoration.
- [ ] Changes visibly take effect without restarting Flick.
- [ ] Preferences persist in the standard XDG configuration location and are restored on a later launch.
- [ ] The file picker remembers its last successfully used directory.
- [ ] Window size and position are restored only when the user enables that behavior and the window manager permits it.
- [ ] Tests isolate configuration state with a temporary XDG environment.
