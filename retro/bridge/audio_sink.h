/*
 * audio_sink.h - the host audio output, behind one tiny interface.
 *
 * Hatari generates samples on the emulation thread and the bridge buffers them
 * in a ring (AtariSt_BridgePushAudio). Something has to drain that ring into
 * the platform's audio device, and "the platform's audio device" is the one
 * part of this project that genuinely differs per target:
 *
 *   Linux / Windows / macOS   audio_sink_sdl.c   (SDL is already linked here,
 *                                                 because Hatari's own CMake
 *                                                 finds it -- so this costs no
 *                                                 new dependency)
 *   Android                   AAudio             (not written yet)
 *   iOS                       AudioUnit          (not written yet)
 *
 * Exactly one implementation is compiled per target. audio_sink_null.c is the
 * fallback so the core still builds, and still runs silently, on a platform
 * whose sink has not been written -- silence is a far better failure than a
 * link error in an emulator that is otherwise working.
 *
 * The sink pulls; it is never pushed to. A push design would mean the
 * emulation thread blocking on the audio device, which makes emulation speed
 * depend on whether the sink is draining -- a much worse bug than a click.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef RETRO_ATARIST_AUDIO_SINK_H
#define RETRO_ATARIST_AUDIO_SINK_H

#include <stdbool.h>

/*
 * Open the host device at [sample_rate] Hz, 16-bit stereo, and start pulling
 * from the bridge ring. Safe to call when already started (no-op). Returns
 * false if no device could be opened -- which is not fatal: the emulator runs
 * silently.
 */
bool AtariSt_AudioSinkStart(int sample_rate);

/* Close the device. Safe to call when not started. */
void AtariSt_AudioSinkStop(void);

/* Name of the backend actually compiled in, for the About screen and logs. */
const char *AtariSt_AudioSinkName(void);

#endif /* RETRO_ATARIST_AUDIO_SINK_H */
