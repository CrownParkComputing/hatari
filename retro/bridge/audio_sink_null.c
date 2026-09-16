/*
 * audio_sink_null.c - the fallback sink: no output at all.
 *
 * Compiled on any target whose real sink has not been written yet (Android's
 * AAudio, iOS's AudioUnit). The emulator runs correctly and silently.
 *
 * Silence rather than a link error on purpose: an ST that plays no sound is
 * still an ST you can use, and a build that fails outright on a new platform
 * blocks everything else about bringing that platform up.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "audio_sink.h"

bool AtariSt_AudioSinkStart(int sample_rate)
{
	(void)sample_rate;
	return false;
}

void AtariSt_AudioSinkStop(void) { }

const char *AtariSt_AudioSinkName(void)
{
	return "none";
}
