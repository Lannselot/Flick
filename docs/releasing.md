# Releasing Flick

This runbook describes the x86_64 Linux release process. GitHub Actions builds
and publishes the artifacts; a release operator prepares the version and tag,
approves the protected deployment, verifies the published files, and records
the real-desktop results.

## Preconditions

Before starting:

- all intended changes are merged into `main`;
- `Linux build and test` is green for the release commit;
- the manual build and verification procedure in
  [`manual-build-and-test.md`](manual-build-and-test.md) has passed for the
  release candidate;
- the working tree is clean;
- the CMake project version is the version being released;
- the operator can create tags and approve the GitHub `release` Environment;
- the Ubuntu, Fedora, and Arch Wayland/X11 verification environments are
  available.

The tag and CMake version must match exactly. CMake version `0.1.0` is released
with tag `v0.1.0`.

## 1. Prepare the version through a pull request

Start from the current protected branch:

```sh
git switch main
git pull --ff-only
git switch -c release/0.1.1
```

Update the version in `CMakeLists.txt`:

```cmake
project(Flick VERSION 0.1.1 LANGUAGES CXX)
```

Update user-facing release notes or compatibility documentation when the
release changes supported behavior or requirements. Commit and open a pull
request:

```sh
git add CMakeLists.txt
git commit -m "Prepare Flick 0.1.1"
git push -u origin HEAD
gh pr create --base main --title "Prepare Flick 0.1.1"
```

Merge only after the required CI check passes. Do not prepare a release by
committing directly to `main`.

## 2. Select and verify the release commit

After the preparation pull request is merged:

```sh
git switch main
git pull --ff-only
git status --short
```

`git status --short` must produce no output. Read the version from CMake and
record the release commit:

```sh
sed -n 's/^project(Flick VERSION \([^ ]*\).*/\1/p' CMakeLists.txt
git rev-parse HEAD
```

Confirm that the `Linux build and test` workflow succeeded for this exact
commit:

```sh
gh run list --workflow CI --commit "$(git rev-parse HEAD)" --limit 1
```

## 3. Create and push the release tag

Prefer a signed tag:

```sh
git tag -s v0.1.1 -m "Flick 0.1.1"
git tag --verify v0.1.1
```

If signing is not configured, create an annotated tag and retain the protected
branch and reviewed preparation pull request as the source-control gate:

```sh
git tag -a v0.1.1 -m "Flick 0.1.1"
```

Inspect the tag before publishing it:

```sh
git show --stat v0.1.1
```

Push only the intended tag:

```sh
git push origin v0.1.1
```

The tag starts `.github/workflows/release-linux.yml`. A manual dispatch can
exercise the build path, but it does not publish a GitHub Release; publication
requires a `v*` tag.

## 4. Observe the unprivileged build

Open the `Release Linux` run:

```sh
gh run list --workflow "Release Linux" --limit 3
```

The read-only build job must:

1. confirm that the tag matches the CMake version;
2. validate the workflows;
3. install the pinned Qt version and image-format plugins;
4. build the instrumented test driver and pass the complete CTest suite;
5. independently build the production target with `BUILD_TESTING=OFF`;
6. download linuxdeploy and its Qt plugin and verify their pinned SHA-256
   values;
7. create and offline-verify the AppImage and development archive;
8. write `SHA256SUMS`;
9. upload the result as a same-run workflow artifact.

Expected files are:

```text
Flick-0.1.1-linux-x86_64.AppImage
Flick-0.1.1-linux-x86_64.tar.gz
SHA256SUMS
```

If a pinned linuxdeploy checksum fails, do not replace it blindly. Download the
new upstream asset, review the upstream change, reproduce the package locally,
and update the checksum through a pull request.

## 5. Approve the protected publication

After the build succeeds, the tag-only publish job waits on the GitHub
Environment named `release`.

In GitHub:

1. open the `Release Linux` workflow run;
2. select **Review deployments**;
3. select the `release` Environment;
4. inspect the build result and tag;
5. choose **Approve and deploy**.

The Environment accepts only tags matching `v*` and requires reviewer
`Lannselot`. Only the publish job receives `contents: write`,
`id-token: write`, and `attestations: write`; the build and third-party tools do
not run with publication permissions.

The publish job creates build-provenance attestations, creates the GitHub
Release, and attaches the three expected files.

## 6. Verify the published release

Inspect and download the release into a new directory:

```sh
gh release view v0.1.1
mkdir -p release-check
gh release download v0.1.1 --dir release-check
cd release-check
```

Verify file integrity:

```sh
sha256sum --check SHA256SUMS
```

Verify GitHub build provenance:

```sh
gh attestation verify \
  Flick-0.1.1-linux-x86_64.AppImage \
  --repo Lannselot/Flick

gh attestation verify \
  Flick-0.1.1-linux-x86_64.tar.gz \
  --repo Lannselot/Flick
```

Smoke-test the downloaded AppImage rather than a build-directory copy:

```sh
chmod +x Flick-0.1.1-linux-x86_64.AppImage
./Flick-0.1.1-linux-x86_64.AppImage
```

## 7. Complete the real-desktop release gate

GitHub-hosted CI does not prove native desktop integration. Run the downloaded
AppImage on the representative matrix:

- Ubuntu under Wayland and X11;
- Fedora under Wayland and X11;
- Arch under Wayland and X11.

On every environment verify:

- launch without a developer Qt installation;
- JPEG, PNG, WebP, GIF, and BMP;
- animated GIF and WebP;
- desktop-file association and file-manager launch;
- clipboard and file-manager reveal;
- normal shutdown and relaunch;
- successful offline/network-syscall verification.

Retain the environment versions and results with the release. The detailed
performance, privacy, and desktop gates are in
[`performance-and-release.md`](performance-and-release.md) and
[`linux-integration.md`](linux-integration.md).

## Failed release handling

### Failure before publication

Fix the cause through a pull request. If the tag has not been announced and no
GitHub Release was published, the operator may remove the failed tag after
confirming its exact target:

```sh
git show --no-patch v0.1.1
git tag -d v0.1.1
git push origin :refs/tags/v0.1.1
```

Create a replacement tag only after the fix is merged and CI is green.

### Failure after publication

Never move or reuse a published tag. Mark the affected release as withdrawn or
prerelease when appropriate, fix the problem through a pull request, increment
the patch version, and publish a new tag such as `v0.1.2`. Preserve the old
checksums and provenance so existing downloads remain auditable.

### Mutable build-tool asset

The linuxdeploy URLs use upstream `continuous` assets, but the accepted bytes
are pinned in the workflow by SHA-256. A checksum failure is a supply-chain
review event, not a reason to disable the check.
