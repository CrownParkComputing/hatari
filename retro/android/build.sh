#!/usr/bin/env bash
# Cross-build libatarist_core.so for the native Android shell.
#
# Builds one ABI at a time; pass it as $1 (default arm64-v8a).
#
#   arm64-v8a    every modern device
#   armeabi-v7a  the cheap 32-bit handhelds this family gets used on
#   x86_64       the Android emulator
#
# Build all three with:
#   for abi in arm64-v8a armeabi-v7a x86_64; do ./build.sh $abi; done
set -euo pipefail

abi="${1:-arm64-v8a}"
# 26 because AAudio is API 26+ ("error: unavailable:
# introduced in Android 26"). The alternatives were worse: dlopen'ing ~15
# AAudio entry points to keep API 24 alive, or shipping Android silent. API 26
# is Android 8.0, from 2017 -- the set of devices that lose out is vanishingly
# small next to the cost of either.
#
# The native Android app's minSdk must match.
api="${ANDROID_API:-26}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
core="$(dirname "$here")"
# The Hatari tree is this repository; retro/ sits beside it.
root="$(cd "$core/.." && pwd)"

: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to your NDK, e.g. \$HOME/Android/Sdk/ndk/26.1.10909125}"

build="$here/build-$abi"
ndk_stamp="$build/.retro-atarist-ndk"
# CMake cannot switch Android toolchains in an existing build tree. Keep a
# small stamp because merely changing CMAKE_TOOLCHAIN_FILE updates the cache
# while leaving CMakeSystem.cmake pointed at the previous NDK.
if [ -d "$build" ] && { [ ! -f "$ndk_stamp" ] || [ "$(<"$ndk_stamp")" != "$ANDROID_NDK_HOME" ]; }; then
	cmake -E remove_directory "$build"
fi
mkdir -p "$build"
printf '%s\n' "$ANDROID_NDK_HOME" > "$ndk_stamp"
# Two accommodations for a platform with no SDL:
#
#   SDL2_DIR -> our empty SDL2Config.cmake, because Hatari's CMake
#     FATAL_ERRORs without an SDL package even though we link none of it.
#     SDL2_DIR rather than CMAKE_PREFIX_PATH: a prefix path expects a
#     <prefix>/lib/cmake/SDL2/ layout, and SDL2_DIR names the directory
#     holding the config file directly. CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
#     has to be relaxed as well -- the NDK toolchain sets it to ONLY, which
#     confines find_package to the sysroot and would skip our stub.
#   ENABLE_DSP_EMU=0  -> falcon/dsp_cpu.c is the only Hatari source that needs
#     a real SDL header. It is the Falcon DSP; no ST or STE title uses it.
#
# ENABLE_SDL3=0 stops the SDL3 branch running first and finding a host SDL3.
cmake -S "$root" -B "$build" \
	-DCMAKE_PROJECT_INCLUDE="$core/embed.cmake" \
	-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
	-DANDROID_ABI="$abi" \
	-DANDROID_PLATFORM="android-$api" \
	-DSDL2_DIR="$core/cmake-stubs" \
	-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
	-DENABLE_SDL3=0 \
	-DENABLE_DSP_EMU=0 \
	-DCMAKE_BUILD_TYPE=Release \
	"${@:2}"
cmake --build "$build" --target atarist_core --parallel "$(nproc)"

dest="$here/prebuilt/$abi"
mkdir -p "$dest"

# Stripped, and the difference is not marginal: Hatari's CMake emits debug
# info even in a Release build, which leaves the arm64 library at ~83MB. That
# is ~140MB of APK across two ABIs, for symbols no shipped build can use.
# Stripped it is ~18MB.
#
# The unstripped copy stays in the build tree so a native crash from a test
# build can still be symbolicated -- llvm-symbolizer against
# libatarist_core.unstripped.so, matched by the BuildID both files share.
strip_tool="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
cp "$build/libatarist_core.so" "$build/libatarist_core.unstripped.so"
if [ -x "$strip_tool" ]; then
	"$strip_tool" --strip-unneeded "$build/libatarist_core.so"
else
	echo "warning: llvm-strip not found; shipping an unstripped library" >&2
fi

cp "$build/libatarist_core.so" "$dest/"
echo "installed: $dest/libatarist_core.so ($(du -h "$dest/libatarist_core.so" | cut -f1))"
echo "symbols:   $build/libatarist_core.unstripped.so"
