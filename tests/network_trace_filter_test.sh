#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 SOURCE_DIR" >&2
    exit 2
fi

source "$1/packaging/linux/network-trace.sh"

test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
trace="$test_dir/network.log"

cat >"$trace" <<'EOF'
5184  --- SIGTERM {si_signo=SIGTERM, si_code=SI_USER, si_pid=5180, si_uid=1001} ---
5185  +++ killed by SIGTERM +++
EOF
if contains_network_syscall "$trace"; then
    echo "process termination was incorrectly classified as network activity" >&2
    exit 1
fi

cat >"$trace" <<'EOF'
5184  socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_IP) = 3
EOF
if ! contains_network_syscall "$trace"; then
    echo "socket syscall was not detected" >&2
    exit 1
fi

cat >"$trace" <<'EOF'
connect(3, {sa_family=AF_INET, sin_port=htons(443)}, 16) = 0
EOF
if ! contains_network_syscall "$trace"; then
    echo "network syscall without a pid prefix was not detected" >&2
    exit 1
fi
