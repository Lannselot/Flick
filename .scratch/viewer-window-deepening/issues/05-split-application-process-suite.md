# 05: Split the application process suite by capability

**What to build:** Maintainers can run and evolve focused application-level suites for core
browsing, presentation, settings, commands, animation, and platform behavior while all suites share
one reliable running-process driver.

**Blocked by:** 04: Extract the process test adapter.

**Status:** ready-for-agent

- [ ] Extract reusable process startup, isolated XDG environment, fixture creation, command
      transport, screenshot capture, and shared assertions into one test-support module.
- [ ] Split the monolithic application fixture into coherent executables or suites whose names make
      the covered capability clear.
- [ ] Preserve every existing scenario; document a before/after inventory showing where each test
      moved.
- [ ] Each suite can run independently and failures identify the affected capability without first
      running unrelated scenarios.
- [ ] Tests continue to assert observable behavior through the running process, not private window
      or dialog representation.
- [ ] Full CTest runtime does not regress materially solely because the process suite was split.

