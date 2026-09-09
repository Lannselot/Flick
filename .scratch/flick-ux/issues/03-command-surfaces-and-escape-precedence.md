# 03: Command surfaces and Escape precedence

**What to build:** Users can discover every relevant viewing command through compact native command
surfaces, retain reliable keyboard focus, and dismiss layered UI with one predictable Escape order.

**Blocked by:** 01: Selected viewing surface and presentation states; 02: Transient status and
first-use feedback.

**Status:** resolved

- [x] The viewport context menu groups Open, View, Image, Copy & Reveal, and Preferences actions in
      the accepted order, with unavailable actions visible but disabled.
- [x] Platform application menus expose conventional Open, View, Image, Help, and Settings entries
      with native Linux and macOS shortcut conventions.
- [x] Previous and Next remain discoverable through teaching and shortcuts without being duplicated
      as context-menu rows.
- [x] The viewing surface regains focus after opening an image and after dialogs close, and viewing
      shortcuts do not depend on pointer position.
- [x] Escape closes a menu or dialog first, then skips large-image confirmation, then leaves
      fullscreen, and otherwise does nothing.
- [x] The complete native context menu remains usable near screen edges and at the minimum supported
      window size on target Linux desktops and macOS.

## Comments

- Replaced the flat automatic context menu with one native menu arranged into the accepted five
  groups. Previous and Next remain window shortcuts and teaching content, but are not menu rows.
- Shared the same actions with File, View, Image, and Help application menus so shortcut and
  enabled-state behavior stays consistent across command surfaces.
- Added application-level coverage for exact context-menu structure, conventional application-menu
  entries, restored viewing focus, and menu-before-fullscreen Escape precedence. Native `QMenu`
  popup placement supplies platform edge clamping at the 480×320 minimum window size.
