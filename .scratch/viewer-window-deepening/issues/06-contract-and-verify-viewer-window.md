# 06: Contract and verify ViewerWindow ownership

**What to build:** The deepening finishes with a focused window coordinator, accurate architecture
documentation, and evidence that no user-visible or performance behavior changed.

**Blocked by:** 02: Extract the transactional Settings editor; 03: Extract live Image Information;
04: Extract the process test adapter; 05: Split the application process suite by capability.

**Status:** resolved

- [x] Remove superseded test hooks, dialog fields, transaction state, formatting helpers, command
      dispatch, and compatibility scaffolding from the window and entry-point implementations.
- [x] Review the resulting window interface and keep only responsibilities needed to coordinate
      user/platform events, view state, the browsing sequence, image loading, and the viewing
      surface.
- [x] Reassess command-surface assembly after contraction; extract it only if doing so hides
      meaningful policy behind a smaller interface rather than creating a shallow wrapper.
- [x] Update architecture documentation with the final module ownership and test-adapter seam.
- [x] Verify that UX validation ticket 06 remains open and unchanged by this engineering effort.
- [x] Build the production and test-driver targets, run the complete CTest suite, and run the
      performance smoke check successfully.
- [x] Complete a Standards and Spec review against this effort before marking it resolved.

## Answer

`ViewerWindow` is contracted to coordination work. Settings test parsing and storage formatting now
stay with `Settings::Editor` and `ProcessTestAdapter`; production excludes the remaining test-only
formatting path. Command-surface assembly remains in the window because it encodes shared-action,
menu-role, grouping, and focus-restoration policy and extracting it would create a shallow wrapper.

Architecture documentation now records Settings, Image Information, and test-adapter ownership.
Dedicated contracts protect the documented ownership, keep native UX validation ticket 06 open,
and verify both source-list and linked-symbol exclusion of the test adapter from production.

Production and test-driver targets built successfully. The complete 17-test CTest suite passed in
45.34 seconds, including `flick.performance-smoke`. Final independent Standards and Spec reviews
reported no findings. Native UX validation ticket 06 remains unchanged and `ready-for-human`.
