# Flick MVP Specification

Status: ready-for-agent

## Problem Statement

Linux users need a fast, focused desktop application for viewing local images without the weight and complexity of a photo manager or editor. Existing tools may start slowly, hide folder navigation behind a file browser, couple viewing to editing workflows, or behave inconsistently across Wayland and X11. The user wants an IrfanView-like viewing experience centered on immediate launch, responsive keyboard-driven navigation through a directory, predictable zooming, and a minimal interface.

## Solution

Flick is an offline, English-language image viewer for modern 64-bit Linux desktops. It opens a local image from the file manager, command line, file chooser, or drag-and-drop, then builds a naturally sorted sequence of supported images so the user can browse immediately. It provides responsive rendering, animated image playback, fullscreen viewing, zooming, panning, temporary rotation, basic image information, safe clipboard and file-location actions, persisted preferences, color management, and defensive handling of malformed or exceptionally large files.

The MVP prioritizes startup speed and viewing responsiveness over editing, asset management, extensibility, or broad format support. It is distributed primarily as an x86_64 AppImage and released as open source under GPL-3.0-or-later.

## User Stories

1. As a Linux user, I want an image to appear quickly after launching Flick, so that inspecting a file does not interrupt my workflow.
2. As a file-manager user, I want to open a supported image with Flick, so that Flick can be my default image viewer.
3. As a command-line user, I want to run `flick` with an image path, so that I can inspect images without leaving the terminal.
4. As a desktop user, I want each invocation to open an independent window, so that I can compare images side by side.
5. As a user with Flick already open, I want `Ctrl+O` to show the system file picker, so that I can open another image without restarting the application.
6. As a user returning to the file picker, I want it to remember the last directory, so that repeated opening is efficient.
7. As a drag-and-drop user, I want to drop one image onto Flick, so that the image opens immediately and navigation follows its directory.
8. As a drag-and-drop user, I want to drop multiple images onto Flick, so that I can browse exactly that supplied set in natural order.
9. As a user browsing a directory, I want only supported image formats included, so that unrelated files do not interrupt navigation.
10. As a user browsing numbered files, I want natural filename sorting, so that `image2` appears before `image10`.
11. As a user browsing mixed-case filenames, I want sorting to ignore case, so that ordering is predictable.
12. As a user browsing a directory, I want hidden files omitted, so that incidental system content does not appear.
13. As a keyboard user, I want the arrow keys to move to the previous or next image, so that browsing is fast.
14. As a user at the start or end of a directory, I want navigation to stop with a brief message, so that I do not unexpectedly wrap around.
15. As a user in a large directory, I want the previous and next images prefetched, so that moving between files feels immediate.
16. As a user with many large images, I want the cache bounded and automatically evicted, so that Flick does not consume unbounded memory.
17. As a user viewing JPEG files, I want their pixels rendered correctly, so that common photographs work out of the box.
18. As a user viewing PNG files, I want their pixels and transparency rendered correctly, so that common graphics work out of the box.
19. As a user viewing WebP files, I want static and animated content rendered correctly, so that modern web images work out of the box.
20. As a user viewing GIF files, I want static and animated content rendered correctly, so that animations play as authored.
21. As a user viewing BMP files, I want their pixels rendered correctly, so that legacy bitmap files remain viewable.
22. As a user viewing animation, I want frame delays and loop behavior respected, so that playback matches the file.
23. As a user viewing animation, I want Space to pause and resume it, so that I can inspect a frame.
24. As a user opening a photograph, I want EXIF orientation applied automatically, so that it appears upright.
25. As a color-conscious user, I want embedded ICC profiles honored, so that image colors are accurate.
26. As a user viewing an untagged image, I want Flick to treat it as sRGB, so that color behavior is deterministic.
27. As a multi-monitor user, I want colors transformed to the display profile when available, so that rendering follows the active screen.
28. As a user opening an image larger than the window, I want it fitted inside the viewport, so that I can initially see the whole image.
29. As a user opening a smaller image, I want it shown at 100% rather than enlarged, so that it initially remains sharp.
30. As a user, I want `1` to restore 100% zoom, so that pixel-level inspection is immediate.
31. As a user, I want `F` to fit the image to the window, so that I can quickly restore an overview.
32. As a mouse user, I want zoom centered on the cursor, so that the point I am inspecting stays in place.
33. As a user switching images, I want the initial fit-or-100% policy reapplied, so that manual zoom does not unexpectedly carry over.
34. As a user viewing a zoomed image, I want to drag it with the left mouse button, so that I can inspect off-screen areas.
35. As a keyboard user, I want `Shift` plus arrow keys to pan a zoomed image, so that all core viewing remains keyboard accessible.
36. As a user, I want plain arrow keys to keep navigating files even when zoomed, so that navigation remains consistent.
37. As a user, I want to choose whether the mouse wheel navigates or zooms, so that Flick matches my preferred workflow.
38. As a new user, I want the mouse wheel to navigate images by default, so that Flick behaves like a classic image viewer.
39. As a user, I want `Ctrl` plus the mouse wheel to perform the alternate wheel action, so that both navigation and zoom remain immediately available.
40. As a user, I want wheel preferences saved between launches, so that I configure Flick only once.
41. As a user, I want `L` and `R` to rotate the view left or right by 90 degrees, so that temporarily misoriented content is readable.
42. As a cautious user, I want rotation to leave the source file unchanged, so that viewing cannot damage my image.
43. As a user navigating away, I want temporary rotation discarded, so that it does not silently affect another file or persist as an edit.
44. As a user, I want `F11` or a double-click to enter and leave fullscreen, so that I can maximize viewing space quickly.
45. As a fullscreen user, I want `Esc` to return to windowed mode, so that exiting fullscreen is predictable.
46. As a fullscreen user, I want the cursor and status display to hide automatically, so that they do not obscure the image.
47. As a user, I want a minimal window with no permanent toolbar, so that the image receives most of the available space.
48. As a user, I want a temporary status display with filename, sequence position, and zoom, so that essential context is available without persistent clutter.
49. As a user, I want the status display to reappear when I move the mouse, so that contextual information remains discoverable.
50. As a user, I want a context menu containing the available commands, so that keyboard shortcuts are not the only way to discover actions.
51. As a user, I want `I` to show image information, so that I can inspect the path, format, dimensions, file size, modification time, zoom, and sequence position.
52. As a user, I want to copy the current file path, so that I can reference the image elsewhere.
53. As a user, I want to copy the current image to the clipboard, so that I can paste its visual content into another application.
54. As a user, I want to reveal the current file in my file manager, so that I can perform file operations outside Flick.
55. As a user, I want Flick to notice supported files added to the current directory, so that navigation stays current.
56. As a user, I want Flick to notice files removed or renamed externally, so that navigation does not retain stale entries.
57. As a user whose current file is deleted externally, I want the nearest remaining image opened with a brief notice, so that browsing can continue.
58. As a user in a directory that becomes empty, I want a neutral empty state, so that the application remains stable and understandable.
59. As a user opening a damaged or inaccessible image, I want a clear error instead of an application exit, so that one bad file does not stop browsing.
60. As a user encountering a failed image, I want to continue to adjacent files, so that the rest of the directory remains usable.
61. As a user encountering a transient failure, I want `F5` to retry loading, so that I can recover after an external issue is resolved.
62. As a technical user, I want optional error details, so that I can diagnose unsupported or malformed content.
63. As a user opening an exceptionally large image, I want Flick to warn before excessive allocation, so that a file cannot unexpectedly exhaust memory.
64. As an informed user, I want to override the large-image warning, so that legitimate exceptional images remain accessible.
65. As a user, I want image decoding off the UI thread, so that the window remains responsive while content loads.
66. As a user, I want settings for wheel action, background color, status visibility, window restoration, and cache size, so that I can tune the viewer.
67. As a user, I want settings changes applied immediately, so that I can see their effect without restarting.
68. As a returning user, I want preferences stored in the standard XDG location, so that Flick follows Linux conventions.
69. As a user, I want Flick optionally to restore window size and position, so that repeated sessions fit my desktop workflow.
70. As a keyboard user, I want every core action reachable without a mouse, so that Flick supports efficient and accessible operation.
71. As a high-DPI user, I want the interface to honor system scaling, so that controls and text remain legible.
72. As a user of system themes, I want Flick to respect system colors and visible focus indication, so that it integrates with my desktop.
73. As a privacy-conscious user, I want Flick to work entirely offline, so that images, paths, metadata, and usage data never leave my computer.
74. As a Wayland user, I want Flick to behave correctly in my native session, so that I do not need an X11 compatibility workaround.
75. As an X11 user, I want Flick to behave correctly in my session, so that the viewer works across established Linux environments.
76. As a user of Ubuntu, Fedora, Arch, GNOME, or KDE, I want a portable x86_64 build, so that I can run Flick on a mainstream modern Linux desktop.
77. As a user, I want Flick supplied as an AppImage, so that I can run it without installing distribution-specific packages.
78. As a developer, I want a binary archive with desktop integration metadata, so that I can inspect and integrate development builds.
79. As a community contributor, I want Flick released under GPL-3.0-or-later, so that distributed modifications remain open.
80. As a user, I want an English interface with localization-ready strings, so that the MVP remains focused without preventing future translations.

## Implementation Decisions

- Flick will be implemented in C++20 with Qt 6. Qt will be dynamically linked under its applicable LGPL terms; Flick itself will use GPL-3.0-or-later.
- The supported deployment target is modern x86_64 Linux under both Wayland and X11, with current Ubuntu, Fedora, and Arch systems using GNOME or KDE as representative environments.
- Each process owns one independent top-level window. Launching Flick again creates another process and window; the MVP has no single-instance IPC or tabs.
- The application accepts an image path from the command line. Opening one file establishes a directory-backed sequence; dropping multiple files establishes an explicit-list sequence containing only the supplied supported files.
- Directory-backed sequences contain supported, non-hidden files and use case-insensitive natural filename ordering. Sequence navigation stops at both ends and reports the boundary briefly instead of wrapping.
- The decoder supports JPEG, PNG, WebP, GIF, and BMP. Animated GIF and WebP preserve source frame timing and loop semantics and support pause/resume.
- EXIF orientation is applied automatically. Embedded ICC profiles are converted for the active display when a display profile is available; untagged images are interpreted as sRGB.
- Image decoding and prefetching run away from the UI thread. The cache targets one previous and one next item, observes an approximately 512 MB default budget, and evicts automatically.
- The initial view policy fits images larger than the viewport and displays smaller images at 100%. Manual zoom is cursor-centered. Moving to another file resets to the initial view policy.
- Panning is available by left-button drag and `Shift` plus arrow keys. Plain arrow keys always navigate the sequence.
- The wheel action is configurable between sequence navigation and zoom. Navigation is the default; holding `Ctrl` invokes the alternate action. The preference persists.
- Temporary view rotation supports 90-degree left and right operations. It neither modifies nor writes the source and resets when navigation changes the current image.
- Fullscreen is toggled by `F11` or double-click and exited with `Esc`. Fullscreen automatically hides the pointer and transient status display after inactivity.
- The normal interface has system window decoration, an image viewport, a transient status display, an information panel, a context menu, a settings dialog, errors, warnings, and empty states. It has no permanent toolbar.
- The status display communicates filename, sequence position, and zoom. The information panel adds full path, format, dimensions, byte size, modification date, and current viewing state.
- Safe external actions are limited to copying the path, copying rendered image content, and revealing the current file in the system file manager.
- A filesystem watcher updates directory-backed sequences after supported files are added, removed, or renamed. If the current file disappears, the nearest remaining item becomes current; an empty sequence produces an empty state.
- Decode or access failure produces an in-window error without terminating Flick. Navigation remains available, `F5` retries, and technical details can be expanded.
- Images whose declared dimensions exceed 100 megapixels or whose estimated decoded allocation exceeds 1 GB require explicit user confirmation. An approved decode still occurs asynchronously.
- The MVP uses current Qt decoding facilities and defensive size checks but does not isolate decoders in a sandboxed helper process.
- Settings cover wheel action, viewport background color, status visibility, window geometry restoration, and cache budget. They apply immediately and persist under the standard XDG configuration location.
- The file chooser remembers its last directory. Desktop integration supports file associations and opening supported files from common Linux file managers.
- The application performs no network requests. It has no telemetry, update check, cloud integration, or remote content loading.
- User-facing text is English. Strings are structured for future localization. Qt's system theme, DPI scaling, keyboard focus, and accessibility metadata are used where supported.
- The primary artifact is an x86_64 AppImage. A development-oriented binary archive includes the metadata required for desktop integration.
- Performance targets on a representative modern computer with an SSD are: visible content within 300 ms for a cold launch with a JPEG or PNG up to 20 megapixels; prefetched adjacent navigation within 100 ms; responsive UI throughout decoding; graceful opening of images up to 100 megapixels without a speed guarantee.
- `Flick` is the working product and executable name. Final naming uniqueness will be checked before public release.

## Testing Decisions

- Tests verify externally observable behavior through the public application boundary rather than private methods, internal classes, cache representation, or implementation-specific signals. A good test remains valid when internals are refactored without changing user-visible behavior.
- The primary and preferred test boundary is a running Flick process supplied with controlled image fixtures and a temporary XDG environment. Tests interact through command-line arguments, keyboard input, pointer input, drag-and-drop, filesystem changes, and visible application output.
- The same application-level boundary covers startup and file opening, sequence construction and natural sorting, boundary behavior, navigation, view state, animation controls, fullscreen behavior, information and status presentation, preferences, clipboard actions, file-manager reveal behavior, directory watching, errors, retries, and large-image warnings.
- Format fixtures cover valid static JPEG, PNG, WebP, GIF, and BMP files; animated GIF and WebP files with known timings and loop counts; embedded ICC profiles; EXIF orientations; transparency; malformed headers; truncated data; permission failures; extreme declared dimensions; and independently known expected pixels or metadata.
- Expected values come from fixed, independently verified fixtures and specifications. Tests must not recreate decoder or natural-sort logic to calculate their own expected result.
- Wayland and X11 application runs verify equivalent externally visible behavior. Representative manual release checks cover GNOME and KDE integration where automation cannot reliably observe compositor or file-manager behavior.
- A benchmark through the same launch boundary measures cold time to first visible content for images up to 20 megapixels, prefetched adjacent-navigation latency, UI responsiveness during decode, cache-budget behavior, and sequence construction for a directory containing 10,000 images.
- Memory tests observe process-level consumption and externally visible survival under cache churn and oversized-image fixtures rather than inspecting cache internals.
- Accessibility checks exercise keyboard-only completion of every core workflow, visible focus, system color integration, and high-DPI scaling.
- Because the repository contains no application code or test suite yet, there is no existing test prior art to inherit. The first implementation ticket must establish the application-level harness and fixture conventions before behavior is added.
- Manual release verification supplements automation for visual color correctness on profiled displays, AppImage portability, desktop file association, clipboard interoperability, system file-manager reveal behavior, window geometry restoration, and subjective responsiveness.

## Out of Scope

- Editing pixels or metadata, saving changes, exporting transformed copies, or overwriting source files.
- Deleting, renaming, moving, or otherwise managing image files.
- Thumbnail grids, contact sheets, a built-in file browser, folder picker, catalog, library, tagging, rating, or search.
- Slideshow playback or timed automatic navigation.
- Tabs, a mandatory single-instance mode, and cross-process window coordination.
- Horizontal or vertical reflection.
- RAW, SVG, TIFF, HEIF/HEIC, AVIF, PDF, and other unspecified formats.
- Multi-page image or document navigation.
- Printing and scanning.
- Plugins, scripting, extension APIs, or external decoder discovery.
- Detailed EXIF/GPS/camera metadata browsing beyond automatic orientation and the basic information panel.
- Animation timeline, seeking, frame stepping, or frame export.
- Decoder isolation in a separate sandboxed process.
- Dedicated screen-reader optimization and custom high-contrast themes beyond Qt-provided accessibility behavior.
- 32-bit systems, non-x86_64 builds, legacy Linux distributions, macOS, and Windows.
- Flatpak, Snap, DEB, RPM, distribution repositories, and automatic updating.
- Networking of any kind, including telemetry, analytics, update checks, cloud storage, sharing, or remote URL opening.
- Russian or other translated interfaces in the MVP.
- A final public product-name decision or trademark clearance.

## Further Notes

- The product concept is inspired by IrfanView's speed and directness, not by a requirement to reproduce its complete feature set or interface.
- The MVP should be judged first by startup and navigation feel. Features that compromise responsiveness should be deferred or redesigned rather than allowed to erode the core promise.
- Opening potentially untrusted downloaded images is expected. The MVP mitigates risk through current dependencies, strict dimension/allocation checks, no external-content execution, and offline operation; stronger process isolation remains a post-MVP hardening option.
- Transparency uses a configurable solid viewport background in the MVP; a checkerboard transparency visualization is deferred.
- The working performance thresholds require a documented representative machine and repeatable fixture corpus before implementation results can be compared meaningfully.
- The next workflow step is to decompose this specification into blocking-aware tracer-bullet tickets with `to-tickets`.
