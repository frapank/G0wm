#!/bin/sh

interval=${1:-5}

battery() {
	for bat in /sys/class/power_supply/BAT*; do
		[ -d "$bat" ] || continue
		cap=$(cat "$bat/capacity" 2>/dev/null) || continue
		printf '%s%% ' "$cap"
	done
}

while :; do
	printf ' %s %s\n' "$(date '+%a %d %b %H:%M:%S')" "$(battery)"
	sleep "$interval"
done
