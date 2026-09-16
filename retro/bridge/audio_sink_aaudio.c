/*
 * audio_sink_aaudio.c - Android audio output through AAudio.
 *
 * AAudio rather than Oboe or OpenSL ES:
 *
 *   - Oboe is C++ and would pull a C++ runtime into a library that is
 *     otherwise pure C, for a wrapper over the same AAudio underneath. Oboe's
 *     real value is falling back to OpenSL ES on API < 26; this app already
 *     targets API 26+ and nothing older is a realistic ST target.
 *   - OpenSL ES is deprecated and higher latency.
 *   - AAudio is in the NDK, is plain C, and adds no dependency at all -- the
 *     library's NEEDED list stays libm/libz/libdl/libc plus libaaudio.
 *
 * Like the SDL sink, this PULLS from the bridge ring on the audio callback's
 * own thread. It never blocks the emulation thread, so emulation speed cannot
 * come to depend on whether the audio device is draining.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <aaudio/AAudio.h>

#include "atarist_internal.h"
#include "audio_sink.h"

static AAudioStream *stream;
static bool sink_started;

/*
 * Called on AAudio's own high-priority thread.
 *
 * Nothing here may allocate, lock, or log: this is a realtime callback, and a
 * single malloc under contention is an audible glitch. AtariSt_BridgeReadAudio
 * is lock-free and pads any shortfall with silence, which is exactly the
 * contract this needs.
 */
static aaudio_data_callback_result_t feed(AAudioStream *s, void *user,
                                          void *audio_data, int32_t num_frames)
{
	(void)s;
	(void)user;
	AtariSt_BridgeReadAudio((int16_t *)audio_data, (int)num_frames);
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/*
 * Disconnects happen in normal use -- headphones unplugged, a Bluetooth
 * speaker going away, a phone call taking the device. Left unhandled the
 * stream simply stops and the game plays silently for the rest of the
 * session, which reads as a bug in the emulator.
 *
 * The stream cannot be rebuilt from this callback (AAudio forbids it), so the
 * error is recorded and the sink is marked closed; the next start reopens it.
 */
static void on_error(AAudioStream *s, void *user, aaudio_result_t error)
{
	(void)s;
	(void)user;
	AtariSt_BridgeSetError("audio device disconnected (%s)",
	                       AAudio_convertResultToText(error));
	sink_started = false;
}

bool AtariSt_AudioSinkStart(int sample_rate)
{
	if (sink_started)
		return true;

	AAudioStreamBuilder *builder = NULL;
	if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
		AtariSt_BridgeSetError("could not create an AAudio stream builder");
		return false;
	}

	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setChannelCount(builder, 2);
	AAudioStreamBuilder_setSampleRate(builder,
	                                  sample_rate > 0 ? sample_rate : 44100);
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	/* SHARED, not EXCLUSIVE: exclusive mode gives lower latency but can be
	 * refused outright when anything else holds the device, and a game that
	 * is silent because a podcast is paused in another app is a bad trade
	 * for a few milliseconds. */
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
	AAudioStreamBuilder_setPerformanceMode(builder,
	                                       AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	/* AAudioStreamBuilder_setUsage(AAUDIO_USAGE_GAME) is deliberately NOT
	 * called: it is API 28+, and it only tells the system how to route and
	 * duck this stream. Using it would push the whole app's minSdk from 26
	 * to 28 to buy a policy hint, which is a bad trade. */
	AAudioStreamBuilder_setDataCallback(builder, feed, NULL);
	AAudioStreamBuilder_setErrorCallback(builder, on_error, NULL);

	aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
	AAudioStreamBuilder_delete(builder);
	if (result != AAUDIO_OK) {
		AtariSt_BridgeSetError("could not open the audio device: %s",
		                       AAudio_convertResultToText(result));
		stream = NULL;
		return false;
	}

	result = AAudioStream_requestStart(stream);
	if (result != AAUDIO_OK) {
		AtariSt_BridgeSetError("could not start the audio device: %s",
		                       AAudio_convertResultToText(result));
		AAudioStream_close(stream);
		stream = NULL;
		return false;
	}

	sink_started = true;
	return true;
}

void AtariSt_AudioSinkStop(void)
{
	if (!stream)
		return;
	AAudioStream_requestStop(stream);
	AAudioStream_close(stream);
	stream = NULL;
	sink_started = false;
}

const char *AtariSt_AudioSinkName(void)
{
	return "AAudio";
}
