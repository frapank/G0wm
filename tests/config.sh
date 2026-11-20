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
    image "/tmp/private image.png"
    imagesize 80
    textscale 1.5
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
for value in '/tmp/private image.png' 'imagesize 80' 'textscale 1.5' 'Hidden {app} {title}' '#12345600' '#abcdef00' 'privatetext ""' '#654321ff' '#fedcbaff'; do
    grep -Fq "$value" "$output" || fail "privacy setting was not preserved: $value"
done
"$binary" -c 2>/dev/null >"$workdir/repeated"
cmp -s "$output" "$workdir/repeated" || fail 'privacy settings changed on repeated reads'

sed '/^key /d; /^button /d; s/tap_delay [0-9]*/tap_delay 250/' "$config" >"$workdir/base"
mv "$workdir/base" "$config"
cat >>"$config" <<'CONFIG'
key logo*2 spawn foot
key ctrl*3 togglefloating
key logo+e*2 spawn thunar
key a*1 spawn foot
button root left*2 spawn foot
button tagbar right*2 tag
button client logo+middle*2 togglefloating
CONFIG
log=$("$binary" -c 2>&1 >"$output") || fail "reading multi-tap bindings failed: $log"
case $log in
    *"not a tap count"*|*"is not a key"*|*"need a modifier"*) fail "valid multi-tap binding rejected: $log" ;;
esac
for value in 'tap_delay 250' 'key logo*2 spawn foot' 'key ctrl*3 togglefloating' 'key logo+e*2 spawn thunar' 'key a spawn foot' 'button root left*2 spawn foot' 'button tagbar right*2 tag' 'button client logo+middle*2 togglefloating'; do
    grep -Fq "$value" "$output" || fail "multi-tap binding was not preserved: $value"
done
cp "$output" "$config"
"$binary" -c 2>/dev/null >"$workdir/repeated"
cmp -s "$output" "$workdir/repeated" || fail 'multi-tap bindings changed on repeated reads'

for spec in 'a*0' 'a*-2' 'a*' 'a*2x' 'a*4294967296' 'a*2*3'; do
    printf '\nkey %s spawn foot\n' "$spec" >>"$config"
done
cat >>"$config" <<'CONFIG'
button client left*2 spawn foot
CONFIG
sed 's/tap_delay 250/tap_delay 0/' "$config" >"$workdir/edited"
mv "$workdir/edited" "$config"
log=$("$binary" -c 2>&1 >"$output") || fail "reading invalid multi-tap bindings failed: $log"
case $log in
    *"tap_delay must be between"*) ;;
    *) fail 'invalid tap delay was accepted' ;;
esac
grep -q 'tap_delay 300' "$output" || fail 'invalid tap delay did not fall back to the default'
[ "$(printf '%s' "$log" | grep -c 'not a tap count')" -eq 6 ] || fail "invalid tap count accepted: $log"
case $log in
    *"client multi-tap buttons need a modifier"*) ;;
    *) fail 'client multi-tap without a modifier was accepted' ;;
esac
if grep -Fq 'button client left*2' "$output"; then
    fail 'invalid client multi-tap was preserved'
fi
