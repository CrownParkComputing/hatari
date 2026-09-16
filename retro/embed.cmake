# Injection point: how this project's target gets into Hatari's build without
# touching a single line of Hatari's CMake.
#
# The first attempt at this was the obvious one -- our CMakeLists as the top
# level, add_subdirectory(vendor/hatari). It does not work, and the reason is
# worth recording so nobody tries it again. Hatari's own CMake resolves several
# paths through ${CMAKE_SOURCE_DIR}:
#
#   cmake/config-cmake.h        (the config.h template)
#   cmake/                      (its Find*.cmake modules)
#   tests/*/CMakeLists.txt      (source files listed as src/file.c)
#
# and CMAKE_SOURCE_DIR means "the TOP-level source directory", so every one of
# those starts pointing at *our* directory the moment Hatari is nested. That is
# not a bug upstream -- nobody else embeds it -- but it means the nesting has
# to go the other way.
#
# So Hatari stays the top-level project and we are injected via
# CMAKE_PROJECT_INCLUDE, which CMake runs immediately after Hatari's project()
# call. "Immediately after" is too early to reference Hatari's targets or the
# results of its find_package calls, so the actual add_subdirectory is DEFERred
# to the end of that directory's processing, by which point Core, UaeCpu,
# ZLIB_FOUND and the rest all exist.
#
# Used as:
#   cmake -S vendor/hatari -B build \
#         -DCMAKE_PROJECT_INCLUDE=<this file> \
#         -DCMAKE_POSITION_INDEPENDENT_CODE=ON
#
# which is what native/atarist_core/*/build.sh does.

if(CMAKE_VERSION VERSION_LESS 3.19)
	message(FATAL_ERROR
		"CMake 3.19+ is required (cmake_language(DEFER)); found ${CMAKE_VERSION}")
endif()

# Hatari's object libraries normally end up in an executable, so they are not
# built relocatable by default. They are going into a shared library here, and
# a non-PIC object only fails at LINK time with a message naming an object file
# rather than a target -- so it is set globally, before anything is defined.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The SDL front end and setup GUI are never linked, but Hatari's CMake still
# configures them. They are simply never built: the build scripts invoke
# `--target atarist_core`, so CMake builds what our target needs and nothing
# else.
get_filename_component(ATARIST_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

# project(Hatari C) enables only C. The mobile bundle also contains C++,
# Objective-C and Objective-C++; these languages must be enabled here while
# project() is still configuring because CMake rejects enable_language() from
# the deferred target injection below.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	# CMake's Xcode generator can report a Darwin host targeting iOS as a
	# native build. Hatari would then build its source generators for the
	# simulator and try to execute them on macOS. Select Hatari's existing
	# cross-build rules so build68k and gencpu are compiled by the host `cc`.
	set(CMAKE_CROSSCOMPILING TRUE)

	enable_language(CXX)
	enable_language(OBJC)
	enable_language(OBJCXX)

	# CMake makes every executable an application bundle for iOS, including
	# Hatari's desktop executable and its command-line helper tools. Upstream's
	# RUNTIME-only install() declarations are therefore invalid during iOS
	# configuration even though none of those targets is built. This embedded
	# build has no install phase, so suppress those declarations at the command
	# boundary rather than carrying a patch in the Hatari submodule.
	function(install)
	endfunction()
endif()

# include(), not add_subdirectory(): CMake refuses to create a subdirectory
# during deferred execution ("Subdirectories may not be created during deferred
# execution"). Including target.cmake into Hatari's own top-level scope has the
# same effect for our purposes -- and it is why every path in target.cmake is
# written against ATARIST_CORE_DIR rather than CMAKE_CURRENT_SOURCE_DIR, which
# at that point names the Hatari tree.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
	CALL include "${ATARIST_CORE_DIR}/target.cmake")
