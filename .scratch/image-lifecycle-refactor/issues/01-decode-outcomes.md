# 01: Introduce tested decode outcomes

**What to build:** Flick produces the same decoded frames, safety confirmation, and decode failures
through a dedicated image-loading module while the running application continues to display every
supported format exactly as before.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] Direct tests cover static images, animated timing metadata, orientation, color-space defaults, malformed input, and exceptional-dimension confirmation.
- [ ] The application consumes typed loading outcomes without changing visible success, warning, or error behaviour.
- [ ] Qt image reading and container metadata parsing are hidden inside the module implementation.
- [ ] The complete existing CTest suite passes.
