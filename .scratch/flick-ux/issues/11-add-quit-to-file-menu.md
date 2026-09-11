# 11: Add Quit to application command surfaces

**What to build:** A desktop user can exit Flick through a conventional Quit command in the File
application menu or the viewing-surface context menu on every supported platform.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] The File menu includes Quit on Linux as well as macOS.
- [x] The action uses the platform-standard Quit role and shortcut, including the appropriate
      Control or Command modifier.
- [x] Triggering the action exits the current Flick process cleanly through the application command
      rather than bypassing normal shutdown behavior.
- [x] Quit is the final context-menu command, separated from Preferences and the image-related
      command groups.
- [x] The File menu and context menu share one action and one shutdown path rather than maintaining
      duplicated command state or behavior.
- [x] The application-boundary test verifies placement in both command surfaces, standard shortcut
      metadata, and clean process termination without making platform-specific text assumptions
      unnecessarily.

## Comments

- Added one native-role Quit action shared by the File and viewing-surface context menus on every
  supported platform. Application-level coverage verifies menu placement, standard role and
  shortcut semantics, shared action identity, and a clean zero-status process exit.
