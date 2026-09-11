# 02: Extract the transactional Settings editor

**What to build:** A user retains the complete Settings workflow while dialog construction,
editable values, persistence, and transactional Apply/Cancel/Reset behavior live in one deep
Settings module instead of the window coordinator.

**Blocked by:** 01: Establish the ViewerWindow seam.

**Status:** resolved

- [x] The Settings module owns the dialog widget tree, opening snapshot, control conversion, native
      color selection, accepted persistence, and dialog lifecycle.
- [x] Its interface accepts typed opening/default settings plus one live-preview operation and does
      not expose individual controls or depend on image loading, the browsing sequence, or the
      viewing surface.
- [x] Live preview, Apply commit, Cancel rollback, Reset Defaults preview, immediate focus recovery,
      and single-dialog behavior remain unchanged through the running-application interface.
- [x] Settings remain in the platform-native backend, including removal of saved geometry when
      restoration is disabled.
- [x] Existing transactional and relaunch coverage passes without tests reaching private widgets.
