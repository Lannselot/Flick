#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if [[ $# -ne 1 || ! -x "$1" ]]; then
    echo "usage: $0 APPIMAGE" >&2
    exit 2
fi

source_dir=$(realpath "$(dirname "$0")/../..")
source "$source_dir/packaging/linux/network-trace.sh"

appimage=$(realpath "$1")
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
export XDG_CONFIG_HOME="$work_dir/config"
export XDG_DATA_HOME="$work_dir/data"
export XDG_CACHE_HOME="$work_dir/cache"
export XDG_STATE_HOME="$work_dir/state"
export XDG_RUNTIME_DIR="$work_dir/runtime"
export QT_QPA_PLATFORM=offscreen
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
    "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

if command -v strace >/dev/null; then
    timeout 3 strace -f -qq -e trace=network -o "$work_dir/network.log" \
        "$appimage" --appimage-extract-and-run >/dev/null 2>&1 || status=$?
    if [[ ${status:-124} -ne 124 && ${status:-0} -ne 0 ]]; then
        echo "AppImage failed its launch smoke test" >&2
        exit 1
    fi
    if contains_network_syscall "$work_dir/network.log"; then
        echo "AppImage attempted a network syscall:" >&2
        cat "$work_dir/network.log" >&2
        exit 1
    fi
else
    echo "strace is required to verify that the release performs no network syscalls" >&2
    exit 1
fi
