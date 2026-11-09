#!/bin/sh
# SPDX-FileCopyrightText: 2026 frapank
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

[ "$#" -eq 2 ] || { echo "usage: $0 BINARY BUILD_DIR" >&2; exit 1; }
binary=$1
mkdir -p "$2"
workdir=$(mktemp -d "$2/test-config.XXXXXX")
trap 'rm -rf "$workdir"' EXIT
export XDG_CONFIG_HOME="$workdir"
config=$workdir/g0wm/config
output=$workdir/output

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

"$binary" -c >"$output" 2>/dev/null
cmp -s "$output" "$config" || fail 'generated config differs from command output'

log=$("$binary" -c 2>&1 >/dev/null)
case $log in
    *"config:"*) fail "fresh config failed validation: $log" ;;
esac

sed 's/borderpx [0-9]*/borderpx 7/; s/showbar/shobwar/' "$config" >"$workdir/edited"
mv "$workdir/edited" "$config"
log=$("$binary" -c 2>&1 >"$output")
case $log in
    *"bar.shobwar is not a setting"*) ;;
    *) fail "invalid setting was not reported: $log" ;;
esac
grep -q 'borderpx 7' "$output" || fail 'valid setting was not applied'
