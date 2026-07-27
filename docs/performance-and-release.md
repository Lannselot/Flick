# Performance and release verification

## Reproducible performance run

Configure a release build with the test harness, build it, and run the
representative benchmark:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen FLICK_EXECUTABLE=build/flick_test_driver \
  build/tests/flick_performance > performance.json
```

The instrumented test driver is a separate, non-installed executable. Release
artifacts always contain the production `flick` target, and the builder rejects
a binary containing the harness marker. The benchmark generates its corpus in a
private temporary directory:

- two independently named, solid-color, lossless 5000 × 4000 PNGs (20 MP);
- an 8000 × 6000 PNG for the decode-responsiveness query;
- a header-only 20000 × 10000 BMP for exceptional-image memory handling;
- 10,000 linked 1 × 1 PNG directory entries for sequence construction.

It measures through the running-process boundary. The application performs its
normal asynchronous decoding, sequence construction, prefetch, rendering,
large-image guard, and cache eviction. The test harness only requests the
already-rendered window image and observable state; it does not bypass those
paths. `--smoke` uses smaller fixtures to validate the benchmark itself in
CTest. It is not a performance gate.

Record at least three representative runs after reboot, with the machine idle,
on AC power and with the CPU governor unchanged. Keep the median JSON result
with the release. Do not compare debug and release builds.

## Baseline machine and result

The initial baseline was recorded on 2026-07-27:

- Intel Core i5-6200U, 2 cores / 4 threads, 2.30–2.80 GHz;
- 7.5 GiB RAM and Samsung SSD 870 EVO SATA;
- x86_64 CachyOS (Arch family), Qt 6.5+, offscreen Qt platform;
- release build from the repository working tree for issue 16.

Observed values: cold visible content 193 ms, prefetched navigation 212 ms,
UI query during a confirmed in-flight 48 MP decode below 1 ms, 10,000-item
sequence construction 54 ms, and 80,000,000 cache bytes. RSS was 212,811,776
bytes before cache churn and 210,743,296 after it; the guarded exceptional
image used 47,763,456 bytes and answered in 6 ms.

The 300 ms cold-launch target passes on this older reference machine. The
100 ms prefetched-navigation target does not pass (212 ms) and must be reported
as a release regression/limitation rather than hidden by loosening the target.
The UI remained immediately queryable while decode-in-flight was observable.

## Build artifacts

On an x86_64 Linux release builder:

```sh
packaging/linux/build-release.sh build dist
```

This creates:

- `Flick-<version>-linux-x86_64.tar.gz`, a reproducible development archive
  containing the binary, desktop entry, AppStream metadata, icon, and full GPL
  license;
- `Flick-<version>-linux-x86_64.AppImage`, built with `linuxdeploy` and its Qt
  plugin. Set `LINUXDEPLOY` when it is not on `PATH`.

Linux packaging is isolated under `packaging/linux`. A future macOS `.app`,
deployment, signing, and notarization flow belongs under `packaging/macos` and
can reuse the platform-neutral performance report schema without inheriting
AppImage assumptions.

## Release gates

The builder runs `verify-release.sh` against the AppImage. It launches the
extracted image with private XDG directories and requires `strace` to prove that
the process makes no network syscalls. CTest also rejects source references to
network APIs or remote URLs and network-capable linked libraries.

Before publishing, run the AppImage on current x86_64 Ubuntu, Fedora, and Arch
installations. For each, record:

- Wayland and X11 launch with no developer Qt installation;
- JPEG, PNG, WebP, GIF, and BMP opening, including animated GIF/WebP;
- desktop-file association and file-manager launch;
- `verify-release.sh` success and no network syscalls.

The archive layout is checked automatically by `flick.linux-release-layout`.
The distribution matrix remains a real-environment release gate; a container
without Wayland/X11 is not evidence for compositor integration.
