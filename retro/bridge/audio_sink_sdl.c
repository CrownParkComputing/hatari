/*
 * audio_sink_sdl.c - desktop audio output through SDL.
 *
 * SDL is already linked into this library on desktop -- Hatari's own CMake
 * calls find_package(SDL2) and a few of its core headers need SDL types --
 * so using it for the sink adds no dependency that was not already paid for.
 *
 * Note the deliberate asymmetry with the rest of the project: backend/ is
 * carefully SDL-free so that Android and iOS never have to ship it, and the
 * ST screen, keyboard and joystick all avoid it. Audio is the one place SDL
 * earns its keep on desktop, and the one place each mobile platform will want
 * its own native API anyway (AAudio, AudioUnit) rather than a portable one.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "config.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "atarist_internal.h"
#include "audio_sink.h"

static bool sink_started;

#if ENABLE_SDL3

static SDL_AudioStream *stream;

/*
 * SDL asks for [additional_amount] more bytes. We answer with whatever the
 * bridge ring has, and silence for the rest -- AtariSt_BridgeReadAudio pads
 * an underrun itself, which matters: handing back a short buffer would let
 * SDL replay the tail of the previous one, and that is an audible buzz rather
 * than a gap.
 */
static void SDLCALL feed(void *userdata, SDL_AudioStream *s,
                         int additional_amount, int total_amount)
{
	(void)userdata;
	(void)total_amount;

	/* One frame is two int16 samples. */
	while (additional_amount > 0) {
		int16_t chunk[512 * 2];
		int want_frames = additional_amount / (int)sizeof(int16_t) / 2;
		if (want_frames > 512)
			want_frames = 512;
		if (want_frames <= 0)
			break;

		AtariSt_BridgeReadAudio(chunk, want_frames);
		const int bytes = want_frames * 2 * (int)sizeof(int16_t);
		SDL_PutAudioStreamData(s, chunk, bytes);
		additional_amount -= bytes;
	}
}

bool AtariSt_AudioSinkStart(int sample_rate)
{
	if (sink_started)
		return true;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		AtariSt_BridgeSetError("SDL audio init failed: %s", SDL_GetError());
		return false;
	}

	SDL_AudioSpec spec;
	SDL_zero(spec);
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	spec.freq = sample_rate > 0 ? sample_rate : 44100;

	stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
	                                   &spec, feed, NULL);
	if (!stream) {
		AtariSt_BridgeSetError("could not open audio device: %s",
		                       SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	SDL_ResumeAudioStreamDevice(stream);
	sink_started = true;
	return true;
}

void AtariSt_AudioSinkStop(void)
{
	if (!sink_started)
		return;
	SDL_DestroyAudioStream(stream);
	stream = NULL;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	sink_started = false;
}

const char *AtariSt_AudioSinkName(void)
{
	return "SDL3";
}

#else /* SDL2 */

static SDL_AudioDeviceID device;

static void feed(void *userdata, Uint8 *out, int len)
{
	(void)userdata;
	AtariSt_BridgeReadAudio((int16_t *)out, len / (int)sizeof(int16_t) / 2);
}

bool AtariSt_AudioSinkStart(int sample_rate)
{
	if (sink_started)
		return true;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		AtariSt_BridgeSetError("SDL audio init failed: %s", SDL_GetError());
		return false;
	}

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = sample_rate > 0 ? sample_rate : 44100;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	/* 1024 frames is ~23ms at 44.1kHz: short enough that ST sound effects
	 * stay in step with the picture, long enough to survive an ordinary
	 * scheduling hiccup without an underrun. */
	want.samples = 1024;
	want.callback = feed;

	SDL_AudioSpec have;
	device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (device == 0) {
		AtariSt_BridgeSetError("could not open audio device: %s",
		                       SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	SDL_PauseAudioDevice(device, 0);
	sink_started = true;
	return true;
}

void AtariSt_AudioSinkStop(void)
{
	if (!sink_started)
		return;
	SDL_CloseAudioDevice(device);
	device = 0;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	sink_started = false;
}

const char *AtariSt_AudioSinkName(void)
{
	return "SDL2";
}

#endif
