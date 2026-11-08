#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# KernelSU late_start service: start the SUSFS management daemon
MODDIR=${0%/%}
STATE_DIR=/data/adb/susfs_ksu
[ -f "$MODDIR/early_load" ] && { [ -e /proc/susfs_path ] || [ -d /sys/module/susfs_guard_lkm ]; } && exit 0
[ -d "$STATE_DIR" ] || exit 0
[ -f "$STATE_DIR/susfs/rpc.ready" ] && exit 0
# Start the management daemon in background
nohup "$MODDIR/bin/susfs_ksu_daemon" "$MODDIR" "$STATE_DIR" >> "$STATE_DIR/susfs/daemon.log" 2>&1 &