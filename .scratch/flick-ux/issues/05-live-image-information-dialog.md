# 05: Live Image Information dialog

**What to build:** Users can keep a compact image-information window open while browsing and compare
the current image's facts without permanently reducing the viewing surface.

**Blocked by:** 01: Selected viewing surface and presentation states.

**Status:** resolved

- [x] Image Information is a non-modal floating dialog using the accepted Variant A composition.
- [x] It reports path, format, dimensions, byte size, modification time, zoom, rotation, animation
      state, and browsing-sequence position.
- [x] Navigating while the dialog is open updates every field to the new current image without
      reopening the dialog or stealing viewing focus.
- [x] Missing or failed current images produce a stable, understandable information state.
- [x] The dialog remains usable at 480×320 and high DPI without permanently resizing or obscuring an
      excessive portion of the viewing surface.
- [x] Automated coverage exercises live updates and keyboard operation through the public
      application boundary.
