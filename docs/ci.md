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

Configure the `main` branch ruleset in GitHub to require a pull request and the
`CI / Linux build and test` status check. Also require the branch to be current,
resolved conversations, and block force pushes. The workflow itself has only
`contents: read` permission.

GitHub-hosted runners need no repository secrets for CI.

## Linux releases

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

macOS support should use a separate `ci-macos.yml` and `packaging/macos`
implementation. Add a non-required `macos-14` configure/build/test job first;
make it required only after the Cocoa tests are stable. Signing and notarization
belong in a tag-only release job protected by a GitHub Environment, with Apple
credentials stored as environment secrets. Linux workflows must not acquire
macOS conditionals or signing credentials.
