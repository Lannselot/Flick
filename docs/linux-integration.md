# Linux desktop integration checks

Flick uses Qt's platform abstraction for keyboard input, focus, accessibility,
system palette colors, and high-DPI scaling. Linux-only display-profile and
file-manager integration stays behind `PlatformServices`; desktop packaging is
installed only when CMake targets Linux. This keeps the viewer UI and action
model reusable by a future macOS adapter.

The application ID and desktop filename are `org.flick.Flick`. The installed
desktop entry advertises JPEG, PNG, WebP, GIF, and BMP. It opens one selected
file and Flick then builds the normal directory-backed sequence.

## Automated checks

Run the application-boundary and metadata checks:

```sh
ctest --test-dir build --output-on-failure
cmake --install build --prefix /tmp/flick-install
```

Confirm that the staging prefix contains:

- `bin/flick`
- `share/applications/org.flick.Flick.desktop`
- `share/metainfo/org.flick.Flick.metainfo.xml`

Distribution packaging should refresh the desktop and AppStream caches as part
of its normal post-install process. Flick's CMake install does not mutate the
host caches, which also keeps staged and AppImage builds reproducible.

## Manual GNOME and KDE matrix

Perform each row once in a native Wayland session and once in an X11 session.
Use the same image directory and settings for both runs. Record the desktop,
session type, compositor version, Qt version, scaling factor, and result.

| Check | GNOME (Files) | KDE Plasma (Dolphin) |
| --- | --- | --- |
| “Open With Flick” opens the selected supported image | Open With menu | Open With menu |
| Left/Right browse the containing directory in natural order | Keyboard only | Keyboard only |
| `Ctrl+O` uses the native-themed picker and remembers its directory | GTK-styled portal/dialog as configured | KDE-styled portal/dialog as configured |
| Context menu lists the viewing commands and native shortcut labels | Right-click and keyboard Menu key | Right-click and keyboard Menu key |
| Every core workflow completes without a mouse | Open, navigate, zoom, pan, rotate, inspect, copy, reveal, settings, fullscreen | Same |
| Focus remains visible in Settings, Information, error details, and the large-image confirmation | Tab/Shift+Tab through controls | Tab/Shift+Tab through controls |
| Screen reader announces the image viewport, status, warnings, and buttons | Orca | Orca or the configured AT-SPI client |
| Light, dark, and high-contrast themes keep text and focus legible | System Appearance variants | Global Theme/Colors variants |
| 100%, 150%, and 200% scaling preserve usable sizes and sharp text | Display settings | Display Configuration |
| `F11`/`Esc`, transient status, and pointer hiding behave equivalently | Native Wayland and X11 | Native Wayland and X11 |
| “Show in File Manager” selects the current file | Files selection | Dolphin selection |
| Tagged and untagged fixtures remain visually consistent on the same display | Include colord profile availability | Include colord/X11 profile availability |

Expected externally visible behavior is identical between Wayland and X11.
Platform-specific differences are limited to compositor decoration, native
picker appearance, shortcut rendering supplied by Qt, and whether the desktop
publishes a display ICC profile.

## Localization readiness

User-facing application strings are created through Qt translation contexts.
Stable identifiers, settings keys, MIME names, and the product name are not
translated. A future platform can load a `QTranslator` before constructing
`ViewerWindow` without changing the action or accessibility model.
