# A deliberately empty SDL2 package, for targets that link no SDL at all.
#
# Hatari's top-level CMakeLists ends with
#
#     find_package(SDL2)
#     if(NOT SDL2_FOUND)
#         message(FATAL_ERROR "SDL library not found!")
#
# which is entirely reasonable for Hatari -- its front end IS SDL. It is wrong
# for us: this project supplies its own UI backend (see backend/README.md) and
# the Hatari objects we link reference **zero** SDL symbols. Verified, not
# assumed:
#
#     find build -name '*.o' | xargs nm -u | grep -c '\bSDL_'   ->  0
#
# So on Android and iOS, where no SDL is present, the only thing standing
# between us and a build is that FATAL_ERROR. This satisfies it and provides
# nothing, which is exactly right: an empty SDL2_LIBRARIES means target.cmake's
# `if(SDL2_LIBRARIES)` guards skip the include and link steps too.
#
# The one Hatari source that genuinely needs an SDL header is
# falcon/dsp_cpu.c (<SDL_timer.h>, for SDL_GetTicks). That is the Falcon DSP,
# which no ST or STE title uses, so the mobile builds pass -DENABLE_DSP_EMU=0
# and it is never compiled.
#
# On desktop this stub is NOT used -- the real SDL is found, and the audio sink
# links it.

set(SDL2_FOUND TRUE)
set(SDL2_INCLUDE_DIRS "")
set(SDL2_LIBRARIES "")
set(SDL2_LIBDIR "")
set(SDL2_VERSION "0.0.0-stub")

message(STATUS "SDL2: using Retro-AtariST's empty stub (no SDL is linked)")
