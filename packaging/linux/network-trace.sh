#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

contains_network_syscall() {
    local trace=$1
    grep -Eq \
        '^[[:space:]]*([0-9]+[[:space:]]+)?(socket|socketpair|bind|listen|accept|accept4|connect|getpeername|getsockname|sendto|recvfrom|sendmsg|recvmsg|recvmmsg|sendmmsg|shutdown|setsockopt|getsockopt)\(' \
        "$trace"
}
