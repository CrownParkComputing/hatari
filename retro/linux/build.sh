#!/usr/bin/env bash
# Build libatarist_core.so for the host.
#
# Used by native bridge/frontend smoke tests and host-side profiling.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
core="$(dirname "$here")"
# The Hatari tree is this repository. Our build layer lives beside it in
# retro/, so "up one from retro/" is Hatari's own top level.
root="$(cd "$core/.." && pwd)"

if [ ! -f "$root/src/main.c" ]; then
	echo "this does not look like the Hatari tree: $root" >&2
	exit 1
fi

# Hatari is the top-level project; we are injected into it. See embed.cmake.
build="$here/build"
cmake -S "$root" -B "$build" \
	-DCMAKE_PROJECT_INCLUDE="$core/embed.cmake" \
	-DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}" \
	"$@"
# --target, so the `hatari` executable and the SDL front end are never built.
cmake --build "$build" --target atarist_core --parallel "$(nproc)"

echo
echo "built: $build/libatarist_core.so"
# Worth printing: an accidentally-exported Hatari symbol is invisible until it
# collides with something in the host process months later.
echo -n "exported symbols: "
nm -D --defined-only "$build/libatarist_core.so" | grep -c ' T ' || true
