# retro/ — the embedding layer

Everything in this directory is Crown Park Computing's, not Hatari's. It turns
Hatari from an application into `libatarist_core`, a shared library with a
stable C ABI that a separate front end drives.

**No file outside this directory is patched.** Hatari stays the top-level CMake
project and this layer is injected through `CMAKE_PROJECT_INCLUDE` — read
`embed.cmake`, which explains why the nesting has to go that way round. The
practical consequence is that taking an upstream update is a merge, not a
patch-queue rebase.

| Path | What |
|---|---|
| `bridge/` | The C ABI (`atarist_bridge.c`) and the per-platform audio sinks. |
| `backend/` | A third peer of Hatari's own `src/sdl/` and `src/retro/`: screen, audio, timing, keymap, input, statusbar, dialogs. |
| `embed.cmake` | The injection point. Run by CMake immediately after Hatari's `project()`. |
| `target.cmake` | Defines `atarist_core`. Included into Hatari's own scope, so every path in it is written against `ATARIST_CORE_DIR`. |
| `linux/build.sh` | Host build, for smoke tests and profiling. |
| `android/build.sh` | NDK build, per ABI. |
| `ios/` | `app.cmake` (how the core links into an iOS bundle) and `compat/`. |
| `cmake-stubs/` | `SDL2Config.cmake`, so Hatari's `find_package(SDL2)` is satisfied without SDL where the core does not need it. |

## The invariant

```sh
nm -D --defined-only libatarist_core.so | grep -c ' T '
```

**must be 29.** An accidentally exported Hatari symbol is invisible until it
collides with something else in the host process months later, so the number is
checked rather than trusted. `linux/build.sh` prints it on every build.

## Who consumes this

More than one application, which is the whole reason the layer lives here
rather than inside one of them:

- **Retro-AtariST** — the Android application.
- **Fuji** — the iOS application.
