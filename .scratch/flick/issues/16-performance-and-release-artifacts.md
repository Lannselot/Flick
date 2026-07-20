# 16 — Performance gates and release artifacts

**What to build:** Flick's speed and memory promises are measured repeatably, and the finished MVP is delivered as portable x86_64 Linux artifacts with the required metadata and licensing.

**Blocked by:** 04 — Asynchronous decoding and bounded prefetch cache; 05 — Supported formats, animation, and orientation; 11 — Live directory sequence updates; 12 — Recoverable errors, retry, and large-image guard; 15 — Linux desktop integration and accessibility.

**Status:** ready-for-agent

- [ ] A documented representative machine and stable fixture corpus make performance results reproducible.
- [ ] Benchmarks measure cold launch to visible content, prefetched navigation latency, UI responsiveness during decode, cache-budget behavior, and sequence construction with 10,000 images.
- [ ] Results assess the 300 ms cold-launch and 100 ms prefetched-navigation targets without hiding regressions behind test-only shortcuts.
- [ ] Memory tests observe process-level behavior during cache churn and exceptional-image handling.
- [ ] An x86_64 AppImage runs on the representative Ubuntu, Fedora, and Arch environments under the supported Wayland and X11 sessions.
- [ ] A development binary archive includes desktop integration metadata, and distributed artifacts include GPL-3.0-or-later licensing information.
- [ ] Release verification confirms that Flick performs no network requests, telemetry, update checks, or remote loading.
