# 06: Contract and verify ViewerWindow ownership

**What to build:** The deepening finishes with a focused window coordinator, accurate architecture
documentation, and evidence that no user-visible or performance behavior changed.

**Blocked by:** 02: Extract the transactional Settings editor; 03: Extract live Image Information;
04: Extract the process test adapter; 05: Split the application process suite by capability.

**Status:** ready-for-agent

- [ ] Remove superseded test hooks, dialog fields, transaction state, formatting helpers, command
      dispatch, and compatibility scaffolding from the window and entry-point implementations.
- [ ] Review the resulting window interface and keep only responsibilities needed to coordinate
      user/platform events, view state, the browsing sequence, image loading, and the viewing
      surface.
- [ ] Reassess command-surface assembly after contraction; extract it only if doing so hides
      meaningful policy behind a smaller interface rather than creating a shallow wrapper.
- [ ] Update architecture documentation with the final module ownership and test-adapter seam.
- [ ] Verify that UX validation ticket 06 remains open and unchanged by this engineering effort.
- [ ] Build the production and test-driver targets, run the complete CTest suite, and run the
      performance smoke check successfully.
- [ ] Complete a Standards and Spec review against this effort before marking it resolved.

