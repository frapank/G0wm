#!/bin/sh

battery_interval=${1:-30}

have_date=0
command -v date >/dev/null 2>&1 && have_date=1
[ "$have_date" -eq 1 ] ||
	echo "dwl-status.sh: 'date' not found, clock disabled" >&2

battery_method=none
bats=

case $(uname -s 2>/dev/null) in
Linux)
	for bat in /sys/class/power_supply/BAT*; do
		[ -d "$bat" ] && bats="$bats $bat/capacity"
	done
	[ -n "$bats" ] && battery_method=linux
	;;
FreeBSD)
	command -v sysctl >/dev/null 2>&1 && battery_method=freebsd
	;;
OpenBSD)
	if command -v apm >/dev/null 2>&1; then
		battery_method=openbsd
	else
		echo "dwl-status.sh: apm not found, battery status disabled" >&2
	fi
	;;
esac

battery=
read_battery() {
	battery=
	case $battery_method in
	linux)
		for cap_file in $bats; do
			read -r cap <"$cap_file" 2>/dev/null && battery="${battery}${cap}% "
		done
		;;
	freebsd)
		cap=$(sysctl -n hw.acpi.battery.life 2>/dev/null)
		[ -n "$cap" ] && [ "$cap" -ge 0 ] 2>/dev/null && battery="${cap}% "
		;;
	openbsd)
		cap=$(apm -l 2>/dev/null)
		[ -n "$cap" ] && battery="${cap}% "
		;;
	esac
}

tick=0
while :; do
	if [ "$tick" -le 0 ]; then
		[ "$battery_method" != none ] && read_battery
		tick=$battery_interval
	fi
	if [ "$have_date" -eq 1 ]; then
		clock=$(date '+%a %d %b %H:%M:%S')
	else
		clock=
	fi
	printf ' %s %s\n' "$clock" "$battery"
	tick=$((tick - 1))
	sleep 1
done
