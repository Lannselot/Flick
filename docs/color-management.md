# Color management

Flick preserves valid embedded image profiles during decoding and interprets
untagged images as sRGB. At presentation time it transforms the current source
frame into the active display profile. Source frames remain unchanged in the
decode cache, which keeps profile changes deterministic and avoids cumulative
conversion error.

Flick queries colord for the profile associated with the active Qt screen. On
X11 it first reads the conventional `_ICC_PROFILE` and `_ICC_PROFILE_n`
properties when the XCB development library was available at build time. It
updates the current frame when Qt reports that the window moved to another
screen. Platforms that do not expose a display profile through either interface
use sRGB output.

## Manual release checks

Perform these checks on a desktop with color management enabled:

1. Assign visibly different, valid ICC profiles to two displays.
2. Open a trusted tagged reference image and compare it with another
   color-managed viewer on each display.
3. Move Flick between the displays. Confirm that colors update after the window
   changes screens and match the reference viewer on both.
4. Open a matching untagged sRGB fixture and confirm stable sRGB interpretation.
5. Repeat with a tagged animated GIF or WebP and confirm every frame uses the
   same display transform.
6. Remove the published display profile and confirm Flick falls back to stable
   sRGB rendering.
