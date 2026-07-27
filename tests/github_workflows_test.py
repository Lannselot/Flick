# SPDX-License-Identifier: GPL-3.0-or-later

import pathlib
import re
import sys


root = pathlib.Path(sys.argv[1])


def require(path: str, snippets: list[str]) -> None:
    workflow = root / path
    if not workflow.is_file():
        raise AssertionError(f"missing {path}")
    contents = workflow.read_text(encoding="utf-8")
    for snippet in snippets:
        if snippet not in contents:
            raise AssertionError(f"{path} is missing {snippet!r}")


def require_actions_pinned(path: str) -> None:
    contents = (root / path).read_text(encoding="utf-8")
    actions = re.findall(r"^\s*uses:\s*([^@\s]+)@([^\s#]+)", contents, re.MULTILINE)
    if not actions:
        raise AssertionError(f"{path} does not use any actions")
    for action, reference in actions:
        if not re.fullmatch(r"[0-9a-f]{40}", reference):
            raise AssertionError(f"{path} does not pin {action} to a full commit SHA")


require(
    ".github/workflows/ci.yml",
    [
        "pull_request:",
        "branches: [main]",
        "workflow_dispatch:",
        "permissions:\n  contents: read",
        "uses: actions/checkout@",
        "uses: jurplel/install-qt-action@",
        "modules: qtimageformats",
        "-DBUILD_TESTING=ON",
        "ctest --test-dir build/test --output-on-failure",
        "-DBUILD_TESTING=OFF",
        "cmake --build build/production",
    ],
)
require_actions_pinned(".github/workflows/ci.yml")
require(
    ".github/workflows/release-linux.yml",
    [
        'tags: ["v*"]',
        "contents: write",
        "id-token: write",
        "attestations: write",
        "permissions:\n      contents: read",
        "needs: build",
        "environment: release",
        "actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c",
        "-DBUILD_TESTING=ON",
        "-DBUILD_TESTING=OFF",
        "packaging/linux/build-release.sh build/production dist",
        "sha256sum",
        "uses: actions/attest@",
        'gh release create "$GITHUB_REF_NAME"',
    ],
)
require_actions_pinned(".github/workflows/release-linux.yml")
require(
    ".github/dependabot.yml",
    [
        "package-ecosystem: github-actions",
        'directory: "/"',
        "interval: weekly",
    ],
)
