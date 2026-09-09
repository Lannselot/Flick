# 03: Command surfaces and Escape precedence

**What to build:** Users can discover every relevant viewing command through compact native command
surfaces, retain reliable keyboard focus, and dismiss layered UI with one predictable Escape order.

**Blocked by:** 01: Selected viewing surface and presentation states; 02: Transient status and
first-use feedback.

**Status:** ready-for-agent

- [ ] The viewport context menu groups Open, View, Image, Copy & Reveal, and Preferences actions in
      the accepted order, with unavailable actions visible but disabled.
- [ ] Platform application menus expose conventional Open, View, Image, Help, and Settings entries
      with native Linux and macOS shortcut conventions.
- [ ] Previous and Next remain discoverable through teaching and shortcuts without being duplicated
      as context-menu rows.
- [ ] The viewing surface regains focus after opening an image and after dialogs close, and viewing
      shortcuts do not depend on pointer position.
- [ ] Escape closes a menu or dialog first, then skips large-image confirmation, then leaves
      fullscreen, and otherwise does nothing.
- [ ] The complete native context menu remains usable near screen edges and at the minimum supported
      window size on target Linux desktops and macOS.

