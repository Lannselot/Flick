# 09: Express native primary actions

**What to build:** Load error and large-image confirmation communicate one unambiguous primary
recovery action using native platform behavior, visual emphasis, and keyboard activation.

**Blocked by:** 07: Extract the viewing-surface module.

**Status:** resolved

- [x] Retry is the primary action in Load error; Details remains secondary and does not become the
      default when expanded or collapsed.
- [x] Open anyway is the primary action in Large-image confirmation; Skip remains secondary.
- [x] Pressing Enter activates the visible primary action when focus has not intentionally moved to
      another actionable control.
- [x] Primary emphasis uses the platform's native default-button and accent treatment rather than a
      custom color stylesheet.
- [x] Escape behavior remains unchanged and never triggers a primary destructive or expensive
      action.
- [x] Application-level tests verify default-action identity and activation for both presentation
      states.

## Comments

- Retry and Open anyway now become the active native Qt default button whenever their presentation
  state is entered. Details and Skip retain ordinary secondary-button behavior, including when
  Details is expanded.
- The running-application test boundary verifies default identity and Enter activation for both
  states. Existing application coverage continues to verify that Escape skips large-image
  confirmation instead of activating its primary action.
