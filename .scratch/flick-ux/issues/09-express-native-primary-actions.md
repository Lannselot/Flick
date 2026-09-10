# 09: Express native primary actions

**What to build:** Load error and large-image confirmation communicate one unambiguous primary
recovery action using native platform behavior, visual emphasis, and keyboard activation.

**Blocked by:** 07: Extract the viewing-surface module.

**Status:** ready-for-agent

- [ ] Retry is the primary action in Load error; Details remains secondary and does not become the
      default when expanded or collapsed.
- [ ] Open anyway is the primary action in Large-image confirmation; Skip remains secondary.
- [ ] Pressing Enter activates the visible primary action when focus has not intentionally moved to
      another actionable control.
- [ ] Primary emphasis uses the platform's native default-button and accent treatment rather than a
      custom color stylesheet.
- [ ] Escape behavior remains unchanged and never triggers a primary destructive or expensive
      action.
- [ ] Application-level tests verify default-action identity and activation for both presentation
      states.
