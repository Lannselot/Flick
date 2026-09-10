# Flick

Flick is a focused desktop viewer for opening local images and browsing them without modifying
their source files.

## Language

**Browsing sequence**:
The ordered set of supported local image paths available for navigation, together with the
currently selected path. A sequence is either directory-backed or an explicit list.
_Avoid_: Playlist, file list, gallery

**Current image**:
The image selected by the browsing sequence and requested for presentation. Its decoded result
may still be pending, rejected, or unavailable.
_Avoid_: Active file, selected frame

**Image loading**:
The lifecycle that turns a requested local image path into decoded frames or a defined loading
outcome while enforcing memory, safety, and freshness policies.
_Avoid_: Fetching, importing

**View state**:
The temporary presentation of the current image, including zoom, pan, rotation, animation
position, and display color conversion. It never changes the source file.
_Avoid_: Edit state, image state

**Viewing surface**:
The content-first area in which Flick presents the current image and its temporary feedback.
It stays visually quiet during normal browsing and does not contain permanent navigation or
editing controls.
_Avoid_: Canvas, workspace, main panel

**Status overlay**:
Temporary, non-interactive information shown over the viewing surface, such as filename,
position in the browsing sequence, zoom, or short feedback. It must disappear without changing
view state.
_Avoid_: Status bar, HUD, notification

**Command surface**:
Any discoverable place from which a user can invoke Flick actions, including menus, the context
menu, and keyboard shortcuts. A command surface is separate from the viewing surface and need
not remain visible while viewing.
_Avoid_: Toolbar, controls

**Empty state**:
The presentation shown when there is no current image. It invites the user to choose or drop an
image and may teach the essential browsing commands; it is not a file library or browsing mode.
_Avoid_: Home screen, start page, welcome screen

**Presentation state**:
The mutually exclusive content shown in the window for the current image request: loading,
displayed image, load error, or large-image confirmation. Changing presentation state does not
replace or block the browsing sequence.
_Avoid_: Screen, page, application state
