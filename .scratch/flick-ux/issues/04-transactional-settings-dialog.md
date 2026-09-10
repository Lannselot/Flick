# 04: Transactional Settings dialog

**What to build:** Users can preview and safely commit the complete settings set in one compact
native dialog whose layout remains comfortable at the minimum supported size.

**Blocked by:** 01: Selected viewing surface and presentation states.

**Status:** resolved

- [x] Settings are organized into Navigation, Appearance, and Performance & Window groups without a
      sidebar or category navigation.
- [x] Wheel action, viewport background, status visibility, cache budget, and geometry restoration
      preview immediately while the dialog is open.
- [x] Apply commits the preview, Cancel restores all values from dialog opening, and Reset Defaults
      previews the complete default set without prematurely committing it.
- [x] File and color selection continue to use platform-native dialogs.
- [x] The full dialog, focus order, labels, and action row remain reachable at 480×320, high DPI, and
      under light and dark system chrome.
- [x] Automated coverage proves commit, rollback, reset, and persistence behavior through the public
      application boundary.

## Comments

- Replaced the flat save-on-OK form with compact native Navigation, Appearance, and Performance &
  Window groups. Controls preview through the existing runtime settings behavior; Apply is the only
  action that writes the preview, while Cancel restores the complete opening snapshot and Reset
  Defaults remains an uncommitted preview.
- Added application-boundary coverage for dialog structure and 480×320 reachability, preview versus
  stored state, rollback, reset, Apply, restored viewing focus, and persistence across relaunch.
