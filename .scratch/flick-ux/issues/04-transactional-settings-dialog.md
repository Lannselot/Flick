# 04: Transactional Settings dialog

**What to build:** Users can preview and safely commit the complete settings set in one compact
native dialog whose layout remains comfortable at the minimum supported size.

**Blocked by:** 01: Selected viewing surface and presentation states.

**Status:** ready-for-agent

- [ ] Settings are organized into Navigation, Appearance, and Performance & Window groups without a
      sidebar or category navigation.
- [ ] Wheel action, viewport background, status visibility, cache budget, and geometry restoration
      preview immediately while the dialog is open.
- [ ] Apply commits the preview, Cancel restores all values from dialog opening, and Reset Defaults
      previews the complete default set without prematurely committing it.
- [ ] File and color selection continue to use platform-native dialogs.
- [ ] The full dialog, focus order, labels, and action row remain reachable at 480×320, high DPI, and
      under light and dark system chrome.
- [ ] Automated coverage proves commit, rollback, reset, and persistence behavior through the public
      application boundary.

