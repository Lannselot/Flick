# 10: Align the presentation contract and vocabulary

**What to build:** Users, accessibility tools, tests, and maintainers encounter one consistent
viewing-surface vocabulary and an unambiguous motion contract that preserves immediate image
navigation.

**Blocked by:** 07: Extract the viewing-surface module; 09: Express native primary actions.

**Status:** ready-for-agent

- [ ] User-facing labels and accessibility metadata use Viewing surface wherever they name the
      content-first area defined by the domain glossary.
- [ ] Viewport remains an implementation term only where it specifically means the scrolling or
      painting geometry rather than the domain concept.
- [ ] Tests assert the canonical user-facing vocabulary without encoding obsolete synonyms.
- [ ] The UX/UI specification explicitly states that ordinary browsing transitions do not fade or
      slide the image or its Loading-to-Displayed path.
- [ ] Optional opacity reveal is limited to independently entered in-surface cards and transient
      feedback, and reduced-motion mode removes it without hiding state changes.
- [ ] The clarified motion contract is covered at the running-application boundary without timing-
      fragile pixel animation assertions.
