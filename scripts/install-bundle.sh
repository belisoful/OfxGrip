#!/bin/sh
# Copies an OpenFX bundle into the system plug-in directory. A copy, never a symlink
# (Q-RESOLVE-025). Removes a previous copy of the same bundle first.
#
#   scripts/install-bundle.sh build/debug/plugins/Gain/OfxGripGain.ofx.bundle [--user]
#
# --user installs under the per-user directory instead (macOS and Linux only; Resolve reads
# only the system directory, so the default is the system one, which needs sudo).
set -eu

if [ $# -lt 1 ]; then
	echo "usage: $0 <bundle.ofx.bundle> [--user]" >&2
	exit 2
fi

bundle="$1"
mode="${2:-}"
if [ ! -d "$bundle" ]; then
	echo "not a bundle directory: $bundle" >&2
	exit 1
fi
if [ -L "$bundle" ]; then
	echo "refusing to install a symlinked bundle (Q-RESOLVE-025)" >&2
	exit 1
fi

case "$(uname -s)" in
	Darwin)
		dest="/Library/OFX/Plugins"
		[ "$mode" = "--user" ] && dest="$HOME/Library/OFX/Plugins"
		;;
	Linux)
		dest="/usr/OFX/Plugins"
		[ "$mode" = "--user" ] && dest="$HOME/.local/share/OFX/Plugins"
		;;
	*)
		echo "unsupported platform; copy the bundle to C:\\Program Files\\Common Files\\OFX\\Plugins by hand" >&2
		exit 1
		;;
esac

name="$(basename "$bundle")"
target="$dest/$name"
# Replacing a bundle a running host has loaded can end that host abruptly. Install with the
# host closed, or pass --force to replace anyway.
if pgrep -x Resolve >/dev/null 2>&1 && [ "$mode" != "--force" ] && [ "${3:-}" != "--force" ]; then
	echo "DaVinci Resolve is running; quit it first (or pass --force)" >&2
	exit 1
fi
if [ -w "$dest" ] || [ "$mode" = "--user" ]; then
	sudo_cmd=""
else
	sudo_cmd="sudo"
fi
$sudo_cmd mkdir -p "$dest"
if [ -e "$target" ]; then
	$sudo_cmd rm -rf "$target"
fi
$sudo_cmd cp -R "$bundle" "$target"
echo "installed $target"
if [ "$(uname -s)" = "Darwin" ]; then
	echo "Resolve caches descriptors in ~/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCacheV2.xml; bump the version or delete the cache when a descriptor changes (Q-RESOLVE-026)."
fi
