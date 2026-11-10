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

"$binary" -c >"$output" 2>"$workdir/errors" || {
    cat "$workdir/errors" >&2
    fail 'generating config failed'
}
cmp -s "$output" "$config" || fail 'generated config differs from command output'

log=$("$binary" -c 2>&1 >/dev/null) || fail "reading config failed: $log"
case $log in
    *"config:"*) fail "fresh config failed validation: $log" ;;
esac

sed 's/borderpx [0-9]*/borderpx 7/; s/showbar/shobwar/' "$config" >"$workdir/edited"
mv "$workdir/edited" "$config"
log=$("$binary" -c 2>&1 >"$output") || fail "reading edited config failed: $log"
case $log in
    *"bar.shobwar is not a setting"*) ;;
    *) fail "invalid setting was not reported: $log" ;;
esac
grep -q 'borderpx 7' "$output" || fail 'valid setting was not applied'

sed '/^private {/,/^}/d' "$config" >"$workdir/base"
mv "$workdir/base" "$config"
cat >>"$config" <<'CONFIG'
private {
    text "Hidden {app} {title}"
    color "#12345600"
    textcolor "#abcdef00"
}
rule {
    id PrivacyTest
    private true
    privatetext ""
    privatecolor "#654321ff"
    privatetextcolor "#fedcbaff"
}
CONFIG
log=$("$binary" -c 2>&1 >"$output") || fail "reading privacy settings failed: $log"
case $log in
    *"private."*|*"rule.private"*) fail "privacy settings failed validation: $log" ;;
esac
for value in 'Hidden {app} {title}' '#12345600' '#abcdef00' 'privatetext ""' '#654321ff' '#fedcbaff'; do
    grep -Fq "$value" "$output" || fail "privacy setting was not preserved: $value"
done
"$binary" -c 2>/dev/null >"$workdir/repeated"
cmp -s "$output" "$workdir/repeated" || fail 'privacy settings changed on repeated reads'
