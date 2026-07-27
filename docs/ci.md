# Continuous integration

Flick uses GitHub Actions for pull-request validation and tagged x86_64 Linux
releases.

## Required checks

`.github/workflows/ci.yml` runs for pull requests, pushes to `main`, and manual
dispatches. It:

1. validates workflow syntax and the repository's workflow contract;
2. installs Qt 6.5 and the Linux build dependencies;
3. builds the instrumented driver with `BUILD_TESTING=ON`;
4. runs the complete CTest suite;
5. independently builds the non-instrumented production target with
   `BUILD_TESTING=OFF`.

The `main` branch protection in GitHub requires a pull request and the
`Linux build and test` status check. It also requires the branch to be current,
linear history and resolved conversations, and blocks force pushes and
deletion. The workflow itself has only `contents: read` permission.

GitHub-hosted runners need no repository secrets for CI.

## Linux releases

The operator-facing procedure is in [`releasing.md`](releasing.md).

`.github/workflows/release-linux.yml` runs for tags matching `v*`. The tag must
match the CMake project version exactly; version `0.1.0` is released as
`v0.1.0`.

The release workflow repeats the complete tests, builds a clean production
binary, creates the AppImage and development archive, runs offline release
verification, writes `SHA256SUMS`, creates GitHub artifact attestations, retains
a short-lived workflow artifact, and publishes the files to a GitHub Release.
It uses the workflow-provided `GITHUB_TOKEN`; no personal access token is
required.

Build, test, and packaging run in a job restricted to `contents: read`. A
separate tag-only `publish` job downloads that job's immutable workflow
artifact and alone receives `contents: write`, `id-token: write`, and
`attestations: write`.

Before creating a tag:

```sh
git switch main
git pull --ff-only
git tag -s v0.1.0 -m "Flick 0.1.0"
git push origin v0.1.0
```

If signed tags are not yet configured, use an annotated tag and retain GitHub
branch/ruleset protection as the source-control gate.

Create a GitHub Environment named `release` and restrict it to protected tags.
Add required reviewers if releases should require a final human approval. The
environment contains no Linux secrets; it gates the privileged publish job.

The linuxdeploy and Qt plugin `continuous` assets are mutable upstream URLs.
Their currently reviewed bytes are pinned by SHA-256 in the release workflow.
When upstream rotates either asset, the release intentionally fails. Download
both assets, review the upstream changes, run the packaging flow locally, then
update both checksums in one pull request.

## Actions maintenance

Dependabot checks GitHub Actions weekly. Third-party actions are limited to the
Qt installer; GitHub-maintained checkout, artifact upload, and attestation
actions provide the remaining integrations. Review action upgrade notes before
merging Dependabot changes, especially minimum self-hosted runner versions.

`scripts/validate-github-actions.sh` downloads the fixed actionlint release,
verifies its checksum, checks every workflow, and then runs the semantic
workflow-contract test.

## Desktop release matrix

GitHub-hosted Ubuntu jobs prove build, tests, artifact contents, and offscreen
launch. They do not prove compositor integration. Before publishing a release,
run the matrix in `docs/performance-and-release.md` on real or self-hosted
Ubuntu, Fedora, and Arch machines under both Wayland and X11.

Recommended self-hosted labels are:

```text
self-hosted,linux,x64,ubuntu,wayland
self-hosted,linux,x64,ubuntu,x11
self-hosted,linux,x64,fedora,wayland
self-hosted,linux,x64,fedora,x11
self-hosted,linux,x64,arch,wayland
self-hosted,linux,x64,arch,x11
```

Keep those runners dedicated, ephemeral where possible, and do not attach them
to workflows triggered by untrusted fork code.

## Future macOS CI

The implementation scope and native acceptance criteria are tracked in
[`17-full-macos-support.md`](../.scratch/flick/issues/17-full-macos-support.md);
the platform audit is in
[`research/macos-support-audit.md`](research/macos-support-audit.md). macOS CI
must use separate workflow and packaging modules:

```text
.github/workflows/ci-macos.yml
.github/workflows/release-macos.yml
packaging/macos/
```

Linux workflows must not acquire Cocoa branches, Apple tools, or signing
credentials. Platform-neutral CTest behavior may be shared through CMake, but
bundle construction and release verification stay behind the macOS packaging
boundary.

### Decisions required before enabling the workflow

Record these decisions in ticket 17 before selecting runner labels or artifact
names:

1. the minimum supported macOS and Xcode versions;
2. the Qt baseline;
3. whether releases are one universal `arm64;x86_64` artifact or separate
   architecture artifacts;
4. the oldest machine on which the packaged application is verified;
5. the Developer ID team and bundle identifier used for signing.

Every job must print `sw_vers`, `uname -m`, `xcodebuild -version`, the Qt
version, and `CMAKE_OSX_ARCHITECTURES`. Do not infer the produced architecture
from a hosted-runner label.

### Phase 1 — non-required build check

Add `ci-macos.yml` for pull requests, pushes to `main`, and manual dispatches.
Start it as a non-required check on the selected hosted macOS runner. Give it
only `contents: read`; PR builds never receive Apple credentials.

The job must:

1. validate the GitHub workflows;
2. install the pinned Qt version, including every advertised image-format
   plugin;
3. configure and build the instrumented driver with `BUILD_TESTING=ON`;
4. run deterministic platform-neutral CTest cases;
5. independently configure and build `flick` with `BUILD_TESTING=OFF`;
6. install the application into a staging directory;
7. verify that the result is a `Flick.app` bundle and inspect its declared
   identifier, version, document types, executable, architectures, frameworks,
   Cocoa platform plugin, and JPEG/PNG/GIF/BMP/WebP plugins.

The production build must never contain `FLICK_ENABLE_TEST_HARNESS`. Keep
offscreen logic tests separate from Cocoa smoke tests so a passing offscreen
suite is not presented as native integration evidence.

### Phase 2 — Cocoa smoke check

After the application has a bundle, native platform services, and file-open
handling, add a Cocoa smoke job. It must launch the installed bundle rather than
the build-tree executable and exercise:

- Finder/Open With and `QFileOpenEvent`;
- native Open and Settings commands;
- Finder reveal;
- clipboard and drag-and-drop;
- fullscreen and Spaces;
- Retina scaling;
- settings through the normal macOS storage backend;
- every packaged static and animated format;
- display-profile discovery and refresh when moving between screens.

Checks involving Finder UI, Spaces, input devices, or calibrated displays may
require a dedicated self-hosted Mac. Do not weaken them into offscreen
assertions to make a hosted runner pass. Keep the real-device matrix from
ticket 17 as release evidence.

Make the macOS check required by `main` branch protection only when it has
passed consistently, its deterministic suite is not quarantined, and the Cocoa
smoke coverage represents the supported workflows. Until then it is visible
and non-required.

### Phase 3 — unsigned release candidate

Add `release-macos.yml` for manual dispatches and `v*` tags. Mirror the Linux
privilege split:

- a `contents: read` job builds, tests, deploys Qt frameworks/plugins, verifies
  the unsigned `.app`, creates the selected archive or disk image, writes
  checksums, and uploads a workflow artifact;
- a tag-only publish job downloads that exact artifact and performs privileged
  release operations.

The build job must fail when bundle dependencies escape the application,
required plugins are absent, the architecture set differs from policy, or a
test harness marker is present. An unsigned artifact is for CI inspection only
and must not be published as a user release.

### Phase 4 — signing and notarized release

Create a separate GitHub Environment named `release-macos`, restricted to
protected `v*` tags and required reviewers. Store Apple material only as
environment secrets. Expected secret categories are:

- the Developer ID Application certificate and its import password;
- a temporary keychain password;
- App Store Connect issuer, key identifier, and private key, or the selected
  notarization credential alternative;
- the Apple team identifier.

The privileged job imports credentials into a temporary keychain, then:

1. signs nested frameworks, plugins, and the application with hardened runtime
   and a secure timestamp;
2. runs strict `codesign` verification;
3. submits the final archive for notarization and waits for success;
4. staples the ticket;
5. runs Gatekeeper assessment;
6. verifies the stapled artifact on a clean machine or release-test account;
7. creates checksums and GitHub artifact attestations;
8. publishes only the verified artifact to the GitHub Release.

Delete the temporary keychain in an `always()` cleanup step. Never print
certificate contents, private keys, notarization responses containing
credentials, or derived passwords. Fork-triggered workflows must have no path
to the `release-macos` Environment.

### Required evidence before claiming macOS support

The macOS CI check can be required before the product is released, but CI alone
does not establish full support. A release is eligible only when all of the
following are retained with it:

- a green deterministic CTest run;
- a green installed-bundle Cocoa smoke run;
- bundle dependency/plugin and architecture reports;
- successful signing, notarization, stapling, and Gatekeeper output;
- checksums and build provenance;
- real-device results for the supported Intel/Apple Silicon policy, Retina,
  mouse and trackpad, light/dark mode, fullscreen/Spaces, and single/dual
  calibrated displays.
