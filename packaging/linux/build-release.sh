#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
    echo "usage: $0 [--archive-only] BUILD_DIR OUTPUT_DIR" >&2
}

archive_only=false
if [[ "${1:-}" == "--archive-only" ]]; then
    archive_only=true
    shift
fi
if [[ $# -ne 2 ]]; then
    usage
    exit 2
fi

build_dir=$(realpath "$1")
output_dir=$(mkdir -p "$2"; realpath "$2")
source_dir=$(realpath "$(dirname "$0")/../..")
version=$(sed -n 's/^project(Flick VERSION \([^ ]*\).*/\1/p' "$source_dir/CMakeLists.txt")
if [[ -z "$version" ]]; then
    echo "could not read Flick version from CMakeLists.txt" >&2
    exit 1
fi
artifact_base="Flick-${version}-linux-x86_64"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

if [[ $(uname -m) != x86_64 ]]; then
    echo "release artifacts must be built on x86_64" >&2
    exit 1
fi

if strings "$build_dir/flick" | grep -q FLICK_TEST_SCREENSHOT_FILE; then
    echo "refusing to package a BUILD_TESTING=ON instrumented Flick binary" >&2
    exit 1
fi

cmake --install "$build_dir" --prefix "$work_dir/$artifact_base/usr"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 --numeric-owner \
    -C "$work_dir" -czf "$output_dir/$artifact_base.tar.gz" "$artifact_base"

if $archive_only; then
    exit 0
fi

linuxdeploy=${LINUXDEPLOY:-$(command -v linuxdeploy || true)}
if [[ -z "$linuxdeploy" ]]; then
    echo "linuxdeploy is required to produce the AppImage (set LINUXDEPLOY)" >&2
    exit 1
fi
appdir="$work_dir/AppDir"
cmake --install "$build_dir" --prefix "$appdir/usr"
export ARCH=x86_64
export OUTPUT="$output_dir/$artifact_base.AppImage"
"$linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/flick" \
    --desktop-file "$appdir/usr/share/applications/org.flick.Flick.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/org.flick.Flick.svg" \
    --plugin qt --output appimage
"$source_dir/packaging/linux/verify-release.sh" "$OUTPUT"
