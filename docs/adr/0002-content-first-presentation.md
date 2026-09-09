---
status: accepted
---

# Keep the viewing surface free of permanent controls

Flick will use one content-first viewing surface with no permanent toolbar, sidebar, library, or
playback controls. Commands remain discoverable through keyboard shortcuts, contextual and system
menus, transient teaching, and purpose-specific dialogs; loading, errors, and large-image
confirmation replace the surface content without blocking the browsing sequence. This hybrid keeps
the speed and density of a native Qt utility while protecting image area and avoiding the product
scope and presentation state of a media library.

## Consequences

The presentation layer needs explicit, mutually exclusive states and a shared transient status
overlay. Linux and macOS share those states and the viewing surface, while their menus, dialogs,
shortcuts, focus behavior, and window chrome follow platform conventions.
