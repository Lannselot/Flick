# 08: Report finite animation completion truthfully

**What to build:** A user keeping Image Information open sees whether an animated current image is
playing, paused, or finished, including when authored finite-loop playback ends naturally.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] A multi-frame image with remaining playback reports Playing.
- [x] A user-paused animation reports Paused and returns to Playing when resumed.
- [x] A finite animation that exhausts its authored loop count reports Finished without requiring
      user input.
- [x] An infinitely looping animation never reports Finished while its timer remains active.
- [x] A static image has a stable non-playing description consistent with the existing information
      vocabulary.
- [x] The open Image Information dialog updates when playback naturally finishes.
- [x] An application-level regression test observes the transition to Finished through the public
      application boundary.

## Comments

- Added an explicit terminal playback state that is reset for each displayed image and updates an
  open Image Information dialog when a finite animation naturally exhausts its authored loops.
- Application-boundary coverage now distinguishes playing, paused, resumed, finished, infinitely
  looping, and static-image descriptions without inspecting the animation timer directly.
