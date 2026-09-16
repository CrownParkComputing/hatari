#!/usr/bin/env bash
# The iOS APPLICATION build does not live here, and cannot.
#
# This file used to exec ../../ios/build.sh, which worked while the core build
# and the Android application shared one repository. They no longer do: this
# repository is the Hatari core and its embedding layer, and an application --
# its UI, its assets, its bundle id, its signing -- belongs to the application
# repository that wraps this one as a submodule.
#
# app.cmake beside this file is still used: target.cmake includes it when
# CMAKE_SYSTEM_NAME is iOS, so the core knows how to be linked into an iOS
# bundle. What is missing here is only the caller.
set -euo pipefail
cat >&2 <<'MSG'
retro/ios/build.sh: there is no iOS application in this repository.

This repository builds the core. To build an iOS app against it, run that
application's own build script, from the application's repository, with this
repository checked out as its core submodule.

To build only the core for iOS from here, drive CMake directly the way
retro/linux/build.sh does, adding:

    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=<iphoneos|iphonesimulator>

MSG
exit 2
