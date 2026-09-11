# 05: Split the application process suite by capability

**What to build:** Maintainers can run and evolve focused application-level suites for core
browsing, presentation, settings, commands, animation, and platform behavior while all suites share
one reliable running-process driver.

**Blocked by:** 04: Extract the process test adapter.

**Status:** resolved

- [x] Extract reusable process startup, isolated XDG environment, fixture creation, command
      transport, screenshot capture, and shared assertions into one test-support module.
- [x] Split the monolithic application fixture into coherent executables or suites whose names make
      the covered capability clear.
- [x] Preserve every existing scenario; document a before/after inventory showing where each test
      moved.
- [x] Each suite can run independently and failures identify the affected capability without first
      running unrelated scenarios.
- [x] Tests continue to assert observable behavior through the running process, not private window
      or dialog representation.
- [x] Full CTest runtime does not regress materially solely because the process suite was split.

## Comments

Split the 52 existing scenarios into independently selectable `flick.application.browsing`,
`presentation`, `settings`, `commands`, `animation`, and `platform` suites. All use the shared
`application_process_test_support` process driver. The before/after mapping is recorded in
`tests/application-process-suite-inventory.md`; a mechanical name comparison found no missing or
renamed scenarios. The six suites pass independently in 38 seconds total on the implementation
host.
