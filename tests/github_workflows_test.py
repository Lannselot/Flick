# SPDX-License-Identifier: GPL-3.0-or-later

import pathlib
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


require(
    ".github/workflows/ci.yml",
    [
        "pull_request:",
        "branches: [main]",
        "workflow_dispatch:",
        "permissions:\n  contents: read",
        "actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803",
        "jurplel/install-qt-action@48d3ad6db93f3627c8ee7a0454bc6f3744f7e730",
        "modules: qtimageformats",
        "-DBUILD_TESTING=ON",
        "ctest --test-dir build/test --output-on-failure",
        "-DBUILD_TESTING=OFF",
        "cmake --build build/production",
    ],
)
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
require(
    ".github/dependabot.yml",
    [
        "package-ecosystem: github-actions",
        'directory: "/"',
        "interval: weekly",
    ],
)
