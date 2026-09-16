# backend/ — the third Hatari UI

Hatari's "UI" is a **seam**, not a library. The emulator core calls out to
`Screen_*`, `Audio_*`, `Keymap_*`, `JoyUI_*`, `Statusbar_*`, `Timing_*` and a
handful of dialog functions, and expects *someone* to supply them. Upstream
ships two implementations of that seam:

| implementation | what it drives                    |
|----------------|-----------------------------------|
| `src/sdl/`     | a real SDL2 window and audio device |
| `src/retro/`   | libretro callbacks (experimental: it does not push video, and `retro_serialize` returns `false`) |

This directory is the **third**: it renders into memory, takes its input from
the native launcher, and hands both to `../bridge/` through its stable C ABI.

The consequence worth stating plainly: **no file under `vendor/hatari` is
patched by this project.** We are a peer of `src/sdl/`, not a modification of
it, so a `git submodule update --remote` is an ordinary version bump rather
than a rebase of a patch queue. If you find yourself editing Hatari sources,
the change almost certainly belongs here instead.

## Files

Each one mirrors its `src/sdl/` counterpart and implements exactly the symbols
that counterpart exports — no more, or the link picks the wrong one.

- `screen.c` — the framebuffer Hatari draws ST video into, plus the two public
  video entry points (`atarist_core_get_framebuffer`, `..._pixel_aspect`).
- `audio.c` — drains Hatari's `AudioMixBuffer` into the bridge's ring.
- `timing.c` — VBL pacing. **This is also where the emulation thread reaches
  the bridge**: `Timing_WaitOnVbl` is the one point per frame at which the ST
  is quiescent, so it is where the mailbox is serviced.
- `gui_event.c` — pushes the launcher's mouse state into the IKBD.
- `keymap.c` — we send ST scan codes directly, so this is mostly inert.
- `joy_ui.c` — presents the launcher's joystick mask as a "real" joystick.
- `statusbar.c` — no statusbar is drawn; the drive LEDs are forwarded to the
  launcher's status line instead.
- `dialogs.c` — the alert/dialog calls Hatari makes when something goes wrong.
  A launcher has no modal ST dialog to show, so these log and decline.
- `microphone.c` — Falcon-only; stubbed.
