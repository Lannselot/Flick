# 04: Introduce the browsing-sequence module

**What to build:** Flick constructs directory-backed and explicit browsing sequences with the same
supported-file rules, canonical paths, deduplication, and natural ordering through a dedicated
browsing-sequence module.

**Blocked by:** 03 — Move decoded cache and prefetch policy.

**Status:** ready-for-agent

- [ ] Direct tests cover supported and hidden files, canonicalization, duplicate paths, mixed case, and numeric natural ordering.
- [ ] A single opened or dropped image creates the existing directory-backed sequence.
- [ ] Multiple dropped paths create the existing explicit sequence and ignore unsupported content.
- [ ] The window consumes the selected path without owning sequence construction invariants.
- [ ] Existing opening, drop, and natural-order process tests pass.
