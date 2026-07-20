# 02 — Directory sequence and keyboard navigation

**What to build:** Opening one image creates a directory-backed browsing sequence so users can move through supported, visible images predictably with the keyboard.

**Blocked by:** 01 — Runnable viewer and application-level test harness.

**Status:** ready-for-agent

- [ ] The sequence contains supported image files from the opened image's directory and omits hidden and unrelated files.
- [ ] Filenames are ordered using case-insensitive natural sorting, including `image2` before `image10`.
- [ ] Left and Right move to the previous and next image and display the newly selected image.
- [ ] Navigation stops at either end without wrapping and briefly communicates the boundary.
- [ ] Application-level tests cover sequence membership, ordering, navigation, and both boundaries.
