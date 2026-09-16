/*
 * backend/audio.c - Hatari's audio seam.
 *
 * Hatari mixes the YM2149 and the STE DMA channel into AudioMixBuffer, a ring
 * it owns, and calls Audio_Unlock when fresh samples are in it. We copy those
 * into the bridge's own ring, which the platform sink (Oboe / AudioUnit /
 * PipeWire) drains on its own thread.
 *
 * Two rings rather than reading AudioMixBuffer directly from the sink: the
 * sink callback runs at hard-realtime priority and must never touch anything
 * the 68000 might be mutating, and AudioMixBuffer's read cursor is shared with
 * Hatari's own resampling.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "sound.h"

#include "atarist_internal.h"

bool bSoundWorking = false;
int SoundBufferSize = 1024 / 4;
int SdlAudioBufferSize = 0;

/* Hatari's rate-control feedback term. Left at zero: it exists so an SDL
 * audio device that is draining slightly fast or slow can pull the emulation
 * rate with it, and our sink is decoupled by the bridge ring instead. */
int pulse_swallowing_count = 0;

static bool playing = false;

void Audio_Init(void)
{
	bSoundWorking = true;
	Audio_EnableAudio(true);
}

void Audio_UnInit(void)
{
	Audio_EnableAudio(false);
	bSoundWorking = false;
}

/* No-ops: nothing else reads AudioMixBuffer concurrently, because the only
 * consumer is Audio_Unlock below and it runs on the emulation thread too. */
void Audio_Lock(void) { }

void Audio_Unlock(void)
{
	if (!playing || !nGeneratedSamples)
		return;

	/* The ring can wrap mid-batch, so this is up to two copies. */
	int first = nGeneratedSamples;
	if (AudioMixBuffer_pos_read + first > AUDIOMIXBUFFER_SIZE)
		first = AUDIOMIXBUFFER_SIZE - AudioMixBuffer_pos_read;

	AtariSt_BridgePushAudio(&AudioMixBuffer[AudioMixBuffer_pos_read][0], first);
	if (first < nGeneratedSamples)
		AtariSt_BridgePushAudio(&AudioMixBuffer[0][0],
		                        nGeneratedSamples - first);

	AudioMixBuffer_pos_read =
		(AudioMixBuffer_pos_read + nGeneratedSamples) & AUDIOMIXBUFFER_SIZE_MASK;
	nGeneratedSamples = 0;
}

void Audio_EnableAudio(bool bEnable)
{
	playing = bEnable;
}

void Audio_FreeSoundBuffer(void)
{
}
