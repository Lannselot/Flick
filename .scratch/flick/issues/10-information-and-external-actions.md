# 10 — Image information and safe external actions

**What to build:** Users can inspect essential image facts and hand the current image or its location to other desktop applications without modifying files.

**Blocked by:** 05 — Supported formats, animation, and orientation; 09 — Fullscreen and transient status UI.

**Status:** ready-for-agent

- [ ] `I` opens information showing full path, format, dimensions, byte size, modification time, zoom, and sequence position.
- [ ] The current file path can be copied to the system clipboard.
- [ ] The currently rendered image content can be copied to the system clipboard.
- [ ] The current file can be revealed in the system file manager.
- [ ] A context menu exposes the available commands and their shortcuts.
- [ ] Failures in clipboard or file-manager integration are reported without terminating Flick.
