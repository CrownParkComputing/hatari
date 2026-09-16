# Native iPhone/iPad application. This file is included after atarist_core is
# defined, from Hatari's deferred target injection (see ../embed.cmake).

get_filename_component(RETRO_ATARIST_ROOT "${ATARIST_CORE_DIR}/../.." ABSOLUTE)
set(RETRO_ATARIST_IOS "${RETRO_ATARIST_ROOT}/ios")
set(RETRO_ATARIST_FRONTEND "${RETRO_ATARIST_ROOT}/native/frontend")
set(IMGUI_DIR "${RETRO_ATARIST_ROOT}/third_party/imgui")

set(APP_RESOURCES
	"${RETRO_ATARIST_ROOT}/LICENSE"
	"${RETRO_ATARIST_ROOT}/THIRD_PARTY_NOTICES.md"
	"${IMGUI_DIR}/LICENSE.txt"
	"${RETRO_ATARIST_IOS}/RetroAtariST/PrivacyInfo.xcprivacy"
	"${RETRO_ATARIST_IOS}/RetroAtariST/Assets.xcassets"
	"${RETRO_ATARIST_ROOT}/native/assets/branding/retro-atarist-logo.png")
set_source_files_properties(${APP_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

set(EMUTOS_RESOURCES
	"${RETRO_ATARIST_ROOT}/native/assets/emutos/emutos-1.4-uk.img"
	"${RETRO_ATARIST_ROOT}/native/assets/emutos/LICENSE.txt"
	"${RETRO_ATARIST_ROOT}/native/assets/emutos/README.txt"
	"${RETRO_ATARIST_ROOT}/native/assets/emutos/SOURCE.md")
set_source_files_properties(${EMUTOS_RESOURCES} PROPERTIES
	MACOSX_PACKAGE_LOCATION "Resources/EmuTOS")

set(DEMO_RESOURCES
	"${RETRO_ATARIST_ROOT}/native/assets/demo/retro-atarist-core-demo.st"
	"${RETRO_ATARIST_ROOT}/native/assets/demo/README.md")
set_source_files_properties(${DEMO_RESOURCES} PROPERTIES
	MACOSX_PACKAGE_LOCATION "Resources/Demo")

add_executable(RetroAtariST MACOSX_BUNDLE
	"${RETRO_ATARIST_IOS}/RetroAtariST/main.mm"
	"${RETRO_ATARIST_IOS}/RetroAtariST/RetroMediaClient.mm"
	"${RETRO_ATARIST_FRONTEND}/frontend.cpp"
	"${IMGUI_DIR}/imgui.cpp"
	"${IMGUI_DIR}/imgui_draw.cpp"
	"${IMGUI_DIR}/imgui_tables.cpp"
	"${IMGUI_DIR}/imgui_widgets.cpp"
	"${IMGUI_DIR}/backends/imgui_impl_metal.mm"
	${APP_RESOURCES}
	${EMUTOS_RESOURCES}
	${DEMO_RESOURCES})

target_compile_features(RetroAtariST PRIVATE cxx_std_20)
target_compile_options(RetroAtariST PRIVATE
	"$<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>")
target_include_directories(RetroAtariST PRIVATE
	"${ATARIST_CORE_DIR}/bridge"
	"${RETRO_ATARIST_FRONTEND}"
	"${IMGUI_DIR}"
	"${IMGUI_DIR}/backends")
target_link_libraries(RetroAtariST PRIVATE atarist_core
	"-framework AudioToolbox"
	"-framework AVFoundation"
	"-framework Foundation"
	"-framework Metal"
	"-framework MetalKit"
	"-framework QuartzCore"
	"-framework Security"
	"-framework UIKit"
	"-framework UniformTypeIdentifiers")

# Automatic signing looks for an *App Development* profile, and a release
# machine only ever has the App Store distribution one, so an unattended
# device build fails with "No profiles for ... were found" before it reaches
# the archive. CI overrides these three to sign manually.
set(RETRO_ATARIST_BUILD_NUMBER "1" CACHE STRING "CFBundleVersion; CI passes a UTC timestamp")
set(RETRO_ATARIST_CODE_SIGN_STYLE "Automatic" CACHE STRING "Automatic or Manual")
set(RETRO_ATARIST_PROVISIONING_PROFILE "" CACHE STRING "profile name, Manual signing only")
set(RETRO_ATARIST_CODE_SIGN_IDENTITY "" CACHE STRING "e.g. Apple Distribution")

set_target_properties(RetroAtariST PROPERTIES
	MACOSX_BUNDLE_INFO_PLIST "${RETRO_ATARIST_IOS}/RetroAtariST/Info.plist"
	XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
	# Signing belongs ON THE TARGET. Passing PROVISIONING_PROFILE_SPECIFIER on
	# the xcodebuild command line applies it to every target in the project,
	# including the static libraries, which then fail with "does not support
	# provisioning profiles" on something that is not the app.
	XCODE_ATTRIBUTE_CODE_SIGN_STYLE "${RETRO_ATARIST_CODE_SIGN_STYLE}"
	XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${RETRO_ATARIST_PROVISIONING_PROFILE}"
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${RETRO_ATARIST_CODE_SIGN_IDENTITY}"
	# For the Xcode generator CMake leaves SKIP_INSTALL at YES, which produces
	# an archive whose Products/Applications is empty and carries no
	# ApplicationProperties -- -exportArchive then has nothing to export and
	# the failure says nothing about the cause.
	XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
	XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
	XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
	XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 15.0
	# Info.plist reads CFBundleVersion from this. CI overrides it on the
	# xcodebuild command line with a UTC timestamp; without a default here a
	# local build would produce an empty CFBundleVersion, which is invalid.
	XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${RETRO_ATARIST_BUILD_NUMBER}"
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.crownparkcomputing.retroatarist
	XCODE_ATTRIBUTE_PRODUCT_NAME Retro-AtariST
	XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")

if(DEFINED DEVELOPMENT_TEAM AND NOT DEVELOPMENT_TEAM STREQUAL "")
	set_target_properties(RetroAtariST PROPERTIES
		XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEVELOPMENT_TEAM}")
endif()
