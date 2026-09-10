# 08: Report finite animation completion truthfully

**What to build:** A user keeping Image Information open sees whether an animated current image is
playing, paused, or finished, including when authored finite-loop playback ends naturally.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] A multi-frame image with remaining playback reports Playing.
- [ ] A user-paused animation reports Paused and returns to Playing when resumed.
- [ ] A finite animation that exhausts its authored loop count reports Finished without requiring
      user input.
- [ ] An infinitely looping animation never reports Finished while its timer remains active.
- [ ] A static image has a stable non-playing description consistent with the existing information
      vocabulary.
- [ ] The open Image Information dialog updates when playback naturally finishes.
- [ ] An application-level regression test observes the transition to Finished through the public
      application boundary.
