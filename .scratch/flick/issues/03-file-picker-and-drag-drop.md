# 03 — File picker and drag-and-drop sequences

**What to build:** Users can open images from the system picker or by dropping files, with single-file drops browsing the containing directory and multi-file drops browsing exactly the supplied supported set.

**Blocked by:** 02 — Directory sequence and keyboard navigation.

**Status:** ready-for-agent

- [ ] Ctrl+O opens the system file picker and a selected supported image becomes current.
- [ ] Dropping one supported image opens it as a directory-backed sequence.
- [ ] Dropping multiple files creates an explicit-list sequence containing only the supplied supported images in case-insensitive natural order.
- [ ] Cancelled selection and unsupported dropped content leave the application stable with clear feedback where needed.
- [ ] The workflows are covered through application-boundary input and visible output.
