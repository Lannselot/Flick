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
