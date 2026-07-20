# 11 — Live directory sequence updates

**What to build:** A directory-backed browsing session stays coherent as supported files are added, removed, or renamed externally.

**Blocked by:** 02 — Directory sequence and keyboard navigation.

**Status:** ready-for-agent

- [ ] Newly added supported, non-hidden files appear in the correct natural-sort position.
- [ ] Removed and renamed files no longer leave stale sequence entries.
- [ ] If the current file disappears, the nearest remaining image becomes current and a brief notice is shown.
- [ ] If no images remain, Flick enters a neutral empty state.
- [ ] Explicit-list sequences are not silently expanded from directory changes.
- [ ] Tests drive real filesystem changes and observe behavior through the running application.
