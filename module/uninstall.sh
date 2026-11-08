#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
MODDIR=${0%/*}
HOOK=/data/adb/post-fs-data.d/00-susfs-ksu.sh
if [ -f "$HOOK" ] && cmp -s "$HOOK" "$MODDIR/early-dispatch.sh"; then
  rm -f "$HOOK"
fi
STATE_DIR=/data/adb/susfs_ksu
if [ -d "$STATE_DIR" ]; then
  # Stop the daemon via RPC
  if [ -x "$MODDIR/bin/susfs_ksu_rpc" ] && [ -S "$STATE_DIR/susfs/rpc.sock" ]; then
    "$MODDIR/bin/susfs_ksu_rpc" "$STATE_DIR/susfs/rpc.sock" '{"path":"/api/susfs/webui/exit","body":{}}' >/dev/null 2>&1 || true
    sleep 1
  fi
fi