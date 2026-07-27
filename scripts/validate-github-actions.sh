#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

source_dir=$(realpath "$(dirname "$0")/..")
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
version=1.7.12
archive="actionlint_${version}_linux_amd64.tar.gz"
checksum=8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8

curl --fail --location --retry 3 \
    --output "$work_dir/$archive" \
    "https://github.com/rhysd/actionlint/releases/download/v${version}/${archive}"
echo "$checksum  $work_dir/$archive" | sha256sum --check --strict
tar -xzf "$work_dir/$archive" -C "$work_dir"
"$work_dir/actionlint" "$source_dir"/.github/workflows/*.yml
python3 "$source_dir/tests/github_workflows_test.py" "$source_dir"
