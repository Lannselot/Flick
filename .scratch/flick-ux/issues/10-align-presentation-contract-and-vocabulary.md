# 10: Align the presentation contract and vocabulary

**What to build:** Users, accessibility tools, tests, and maintainers encounter one consistent
viewing-surface vocabulary and an unambiguous motion contract that preserves immediate image
navigation.

**Blocked by:** 07: Extract the viewing-surface module; 09: Express native primary actions.

**Status:** resolved

- [x] User-facing labels and accessibility metadata use Viewing surface wherever they name the
      content-first area defined by the domain glossary.
- [x] Viewport remains an implementation term only where it specifically means the scrolling or
      painting geometry rather than the domain concept.
- [x] Tests assert the canonical user-facing vocabulary without encoding obsolete synonyms.
- [x] The UX/UI specification explicitly states that ordinary browsing transitions do not fade or
      slide the image or its Loading-to-Displayed path.
- [x] Optional opacity reveal is limited to independently entered in-surface cards and transient
      feedback, and reduced-motion mode removes it without hiding state changes.
- [x] The clarified motion contract is covered at the running-application boundary without timing-
      fragile pixel animation assertions.

## Comments

- Replaced user-facing viewport synonyms with the domain term Viewing surface in accessibility,
  Settings, validation documentation, and application-level assertions. Internal scrolling and
  painting geometry continues to use viewport terminology.
- Centralized presentation motion policy in `ViewingSurface`: Loading and Displayed transitions are
  immediate, while independently entered Empty, Load error, and Large-image cards may use opacity.
  Reduced-motion mode continues to suppress optional transitions. The running-process test harness
  verifies the semantic policy without sampling animation pixels or elapsed frames.
