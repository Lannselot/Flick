# 04: Introduce the browsing-sequence module

**What to build:** Flick constructs directory-backed and explicit browsing sequences with the same
supported-file rules, canonical paths, deduplication, and natural ordering through a dedicated
browsing-sequence module.

**Blocked by:** 03 — Move decoded cache and prefetch policy.

**Status:** resolved

- [x] Direct tests cover supported and hidden files, canonicalization, duplicate paths, mixed case, and numeric natural ordering.
- [x] A single opened or dropped image creates the existing directory-backed sequence.
- [x] Multiple dropped paths create the existing explicit sequence and ignore unsupported content.
- [x] The window consumes the selected path without owning sequence construction invariants.
- [x] Existing opening, drop, and natural-order process tests pass.

## Comments

Implemented `BrowsingSequence` as the construction seam for directory-backed and explicit
sequences. The window now consumes its canonical ordered paths and selected index; focused module
tests and the existing application process suite pass.
