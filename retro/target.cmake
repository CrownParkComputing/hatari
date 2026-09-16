# libatarist_core -- Hatari's emulation core plus this project's UI backend
# and stable C bridge, as one native library.
#
# This file is NOT a CMakeLists. It is include()d into Hatari's own top-level
# directory scope by embed.cmake -- read that first; it explains why the
# nesting goes this way round, and why this is an include rather than an
# add_subdirectory. The practical consequence is that everything Hatari's CMake
# worked out (config.h, ZLIB_FOUND, the generated 68000 emulator, the
# cross-compilation rules for its code generators) is already in scope here.
#
# Because it is included rather than added as a subdirectory, CMAKE_CURRENT_*
# refers to HATARI's directory, not this one. Every path below therefore goes
# through ATARIST_CORE_DIR, which embed.cmake sets. A bare relative source path
# here would be silently resolved against the Hatari tree.
#
# The object-library set below is deliberately the same one upstream's own
# libretro target uses -- Core, CoreHmsa, Falcon, UaeCpu, Debug -- with Ui and
# GuiSdl replaced by our backend/.

# Derived here rather than taken from embed.cmake: variables set during
# CMAKE_PROJECT_INCLUDE do not survive into deferred execution, so relying on
# one produced an empty path and a "cannot find source file /backend/screen.c".
# CMAKE_CURRENT_LIST_DIR is the directory of THIS file even when it is
# include()d into someone else's scope, which is exactly what is wanted.
get_filename_component(ATARIST_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HATARI_TOP "${CMAKE_SOURCE_DIR}")

set(BACKEND_SOURCES
	"${ATARIST_CORE_DIR}/backend/screen.c"
	"${ATARIST_CORE_DIR}/backend/audio.c"
	"${ATARIST_CORE_DIR}/backend/timing.c"
	"${ATARIST_CORE_DIR}/backend/gui_event.c"
	"${ATARIST_CORE_DIR}/backend/keymap.c"
	"${ATARIST_CORE_DIR}/backend/joy_ui.c"
	"${ATARIST_CORE_DIR}/backend/statusbar.c"
	"${ATARIST_CORE_DIR}/backend/dialogs.c"
	"${ATARIST_CORE_DIR}/backend/microphone.c")

# Exactly one audio sink is compiled in. SDL only where SDL is already being
# linked anyway (desktop); everywhere else the null sink keeps the build
# working and the emulator silent until a native sink is written.
if(ANDROID)
	# AAudio: in the NDK, plain C, no new dependency beyond libaaudio itself.
	set(AUDIO_SINK "${ATARIST_CORE_DIR}/bridge/audio_sink_aaudio.c")
elseif(APPLE)
	# RemoteIO Audio Unit. Objective-C only because AVAudioSession is: without
	# configuring the session, iOS silences the app whenever the ringer switch
	# is on, with no error anywhere.
	set(AUDIO_SINK "${ATARIST_CORE_DIR}/bridge/audio_sink_ios.m")
elseif(SDL2_LIBRARIES)
	set(AUDIO_SINK "${ATARIST_CORE_DIR}/bridge/audio_sink_sdl.c")
else()
	# Builds and runs silent rather than failing to link, so a new platform
	# is never blocked on its audio backend.
	set(AUDIO_SINK "${ATARIST_CORE_DIR}/bridge/audio_sink_null.c")
endif()

set(BRIDGE_SOURCES
	"${ATARIST_CORE_DIR}/bridge/atarist_bridge.c"
	"${AUDIO_SINK}")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	list(APPEND BRIDGE_SOURCES
		"${ATARIST_CORE_DIR}/bridge/paths_ios.m")

	# src/paths.c and gui-osx/paths.m both become paths.o under Xcode. The
	# latter is a macOS desktop preference helper; replace it with our uniquely
	# named sandbox-aware implementation to avoid the object-name collision.
	get_target_property(ATARIST_CORE_SOURCES Core SOURCES)
	list(FILTER ATARIST_CORE_SOURCES EXCLUDE REGEX "gui-osx/paths\\.m$")
	set_property(TARGET Core PROPERTY SOURCES ${ATARIST_CORE_SOURCES})
endif()

# Upstream UaeCpu owns Xcode build phases that compile and execute its source
# generators. Those phases inherit the iPhone SDK and produce helper programs
# that cannot execute on the Mac host. ios/build.sh generates the identical,
# architecture-independent C sources before configuration; compile them in a
# separate object target so Xcode never pulls in upstream's generator phases.
set(ATARIST_UAE_CPU_OBJECTS "$<TARGET_OBJECTS:UaeCpu>")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	set(MOBILE_CPU_SOURCE "${HATARI_TOP}/src/cpu")
	set(MOBILE_CPU_GENERATED "${CMAKE_BINARY_DIR}/retro-atarist-cpu")
	set(MOBILE_CPUEMU_SOURCES
		cpustbl.c cpuemu_0.c cpuemu_11.c cpuemu_13.c cpuemu_20.c cpuemu_21.c
		cpuemu_22.c cpuemu_23.c cpuemu_24.c cpuemu_31.c cpuemu_32.c
		cpuemu_33.c cpuemu_34.c cpuemu_35.c cpuemu_40.c cpuemu_50.c)
	list(TRANSFORM MOBILE_CPUEMU_SOURCES PREPEND "${MOBILE_CPU_GENERATED}/")
	set(MOBILE_CPU_SOURCES
		"${MOBILE_CPU_GENERATED}/cpudefs.c"
		"${MOBILE_CPU_SOURCE}/cpummu.c"
		"${MOBILE_CPU_SOURCE}/cpummu030.c"
		"${MOBILE_CPU_SOURCE}/debug.c"
		"${MOBILE_CPU_SOURCE}/disasm.c"
		"${MOBILE_CPU_SOURCE}/newcpu_common.c"
		"${MOBILE_CPU_SOURCE}/newcpu.c"
		"${MOBILE_CPU_SOURCE}/readcpu.c"
		"${MOBILE_CPU_SOURCE}/writelog.c"
		"${MOBILE_CPU_SOURCE}/fpp.c"
		"${MOBILE_CPU_SOURCE}/fpp_native.c"
		"${MOBILE_CPU_SOURCE}/fpp_softfloat.c"
		"${MOBILE_CPU_SOURCE}/softfloat/softfloat.c"
		"${MOBILE_CPU_SOURCE}/softfloat/softfloat_decimal.c"
		"${MOBILE_CPU_SOURCE}/softfloat/softfloat_fpsp.c"
		"${MOBILE_CPU_SOURCE}/machdep/m68k.c"
		"${MOBILE_CPU_SOURCE}/custom.c"
		"${MOBILE_CPU_SOURCE}/events.c"
		"${MOBILE_CPU_SOURCE}/memory.c"
		"${MOBILE_CPU_SOURCE}/hatari-glue.c")
	add_library(AtariSTUaeCpu OBJECT
		${MOBILE_CPUEMU_SOURCES}
		${MOBILE_CPU_SOURCES})
	target_include_directories(AtariSTUaeCpu PRIVATE
		"${MOBILE_CPU_SOURCE}"
		"${HATARI_TOP}/src"
		"${HATARI_TOP}/src/includes"
		"${HATARI_TOP}/src/debug"
		"${HATARI_TOP}/src/falcon"
		"${MOBILE_CPU_SOURCE}/softfloat"
		"${MOBILE_CPU_GENERATED}"
		"${CMAKE_BINARY_DIR}")
	target_compile_options(AtariSTUaeCpu PRIVATE
		-Wno-sign-compare
		-Wno-unused-variable
		-Wno-unused-function
		-Wno-unused-label
		-Wno-missing-braces
		-fwrapv)
	set_target_properties(AtariSTUaeCpu PROPERTIES
		POSITION_INDEPENDENT_CODE ON)
	set(ATARIST_UAE_CPU_OBJECTS "$<TARGET_OBJECTS:AtariSTUaeCpu>")
endif()

# The mobile iOS app links the core statically. This avoids an otherwise
# unnecessary embedded-framework/code-signing boundary and lets the native
# frontend call the C ABI directly. Other platforms keep the shared library
# used by their host integration and smoke tests.
set(ATARIST_CORE_LIBRARY_TYPE SHARED)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	set(ATARIST_CORE_LIBRARY_TYPE STATIC)
endif()

add_library(atarist_core ${ATARIST_CORE_LIBRARY_TYPE}
	${BACKEND_SOURCES}
	${BRIDGE_SOURCES}
	$<TARGET_OBJECTS:Core>
	$<TARGET_OBJECTS:CoreHmsa>
	$<TARGET_OBJECTS:Falcon>
	${ATARIST_UAE_CPU_OBJECTS}
	$<TARGET_OBJECTS:Debug>)

if(ANDROID)
	# NDK r28's linux/stddef.h defines __counted_by as a compatibility macro,
	# while Hatari's log.c probes Clang's similarly named field attribute and
	# applies it to pointers (where Clang 19 rejects it). The annotation is only
	# diagnostic metadata, so disable attribute probing for this one object
	# library rather than patching the Hatari submodule.
	target_compile_options(Debug PRIVATE
		"-D__has_attribute(x)=0"
		-Wno-builtin-macro-redefined
		-Wno-macro-redefined)
endif()

set_target_properties(atarist_core PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED ON)

target_include_directories(atarist_core PRIVATE
	"${ATARIST_CORE_DIR}/bridge"
	"${HATARI_TOP}/src"
	"${HATARI_TOP}/src/includes"
	"${HATARI_TOP}/src/debug"
	"${HATARI_TOP}/src/falcon"
	"${HATARI_TOP}/src/cpu"
	# config.h is generated into the build tree, not the source tree.
	"${CMAKE_BINARY_DIR}")

# Hatari treats every Apple platform as macOS in file.c and includes
# <sys/disk.h> for raw /dev/disk size probing. The header and raw devices do
# not exist in the iOS SDK, so let that one upstream object see our no-op
# compatibility header. Normal application files continue through fseeko().
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	target_include_directories(CoreHmsa PRIVATE
		"${ATARIST_CORE_DIR}/ios/compat")
endif()

# pthread is a separate library on glibc but is part of libc on Android's
# bionic, where -lpthread does not exist at all ("ld.lld: error: unable to
# find library -lpthread"). Apple's libSystem is the same story.
target_link_libraries(atarist_core PRIVATE m)
if(NOT ANDROID AND NOT APPLE)
	target_link_libraries(atarist_core PRIVATE pthread)
endif()

# Mirrors the optional-dependency list in Hatari's own src/CMakeLists.txt.
# Kept in that order and with the same variable names so a diff against
# upstream is readable when a new dependency appears there.
#
# Missing one of these does NOT fail the link -- the symbols come from Hatari's
# object files, which link fine -- it fails at dlopen with
# "undefined symbol: udev_new", which looks like a broken library rather than a
# missing -l. That is exactly how this list was found to be incomplete.
if(Math_FOUND AND NOT APPLE)
	target_link_libraries(atarist_core PRIVATE ${MATH_LIBRARY})
endif()
if(Readline_FOUND)
	target_link_libraries(atarist_core PRIVATE ${READLINE_LIBRARY})
endif()
if(ZLIB_FOUND)
	target_link_libraries(atarist_core PRIVATE ${ZLIB_LIBRARY})
endif()
if(LibArchive_FOUND)
	target_link_libraries(atarist_core PRIVATE ${LibArchive_LIBRARIES})
endif()
if(PNG_FOUND)
	target_link_libraries(atarist_core PRIVATE ${PNG_LIBRARY})
endif()
if(X11_FOUND)
	target_link_libraries(atarist_core PRIVATE ${X11_LIBRARIES})
endif()
if(PortMidi_FOUND)
	target_link_libraries(atarist_core PRIVATE ${PORTMIDI_LIBRARY})
endif()
if(CapsImage_FOUND)
	target_link_libraries(atarist_core PRIVATE ${CAPSIMAGE_LIBRARY})
endif()
if(Udev_FOUND)
	target_link_libraries(atarist_core PRIVATE ${UDEV_LIBRARY})
endif()
if(Capstone_FOUND)
	target_link_libraries(atarist_core PRIVATE ${CAPSTONE_LIBRARY})
endif()
if(ANDROID)
	target_link_libraries(atarist_core PRIVATE aaudio)
endif()
if(APPLE)
	# Frameworks, not -l: AudioToolbox provides the Audio Unit API and
	# AVFoundation the session. Both are present on iOS and macOS.
	target_link_libraries(atarist_core PRIVATE
		"-framework AudioToolbox"
		"-framework AVFoundation"
		"-framework Foundation")
	set_source_files_properties("${ATARIST_CORE_DIR}/bridge/audio_sink_ios.m"
		PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

# A few Hatari core sources include SDL headers for their type definitions even
# though this library links no SDL code at all.
if(SDL2_INCLUDE_DIRS)
	target_include_directories(atarist_core PRIVATE ${SDL2_INCLUDE_DIRS})
endif()
if(SDL2_LIBRARIES)
	target_link_libraries(atarist_core PRIVATE ${SDL2_LIBRARIES})
endif()

# Only the atarist_core_* entry points are exported. Everything else -- and
# "everything else" here is the whole of Hatari, thousands of symbols with
# names as generic as `main`, `select` and `debug` -- stays local.
#
# This is not tidiness. On Android the app and every native dependency share
# one symbol namespace, and an exported `main` or `select` from an
# emulator core is a genuinely vicious source of crashes in unrelated code.
# macOS/iOS use a two-level namespace and are safe either way, but hiding
# costs nothing there.
if(NOT APPLE AND NOT WIN32)
	target_link_options(atarist_core PRIVATE
		"-Wl,--version-script=${ATARIST_CORE_DIR}/atarist_core.map")
endif()

set_target_properties(atarist_core PROPERTIES
	C_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	include("${ATARIST_CORE_DIR}/ios/app.cmake")
endif()
