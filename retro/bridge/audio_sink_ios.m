/*
 * audio_sink_ios.m - iOS/macOS audio output through a RemoteIO Audio Unit.
 *
 * Compiled by the unsigned iOS Simulator CI build. Physical-device behaviour
 * still needs validation on a Mac because the Linux development host cannot
 * exercise AVAudioSession, the ringer-switch policy or RemoteIO hardware.
 *
 * Objective-C rather than C, for exactly one reason: AVAudioSession. The Audio
 * Unit half below is pure C and would compile as .c, but without configuring
 * the session first, iOS decides the app's audio policy for you -- and the
 * default for an app that has not asked is "silent when the ringer switch is
 * on". A retro game front end that is inaudible for the half of users who keep
 * that switch flipped, with no error anywhere, is a genuinely awful bug to
 * diagnose.
 *
 * Like the SDL and AAudio sinks, this PULLS from the bridge ring on the audio
 * thread. The emulation thread is never blocked, so emulation speed cannot
 * come to depend on whether the device is draining.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "atarist_internal.h"
#include "audio_sink.h"

static AudioUnit audio_unit;
static bool sink_started;

/*
 * The render callback, on Core Audio's realtime thread.
 *
 * Nothing here may allocate, lock, take an Objective-C message send, or log:
 * this thread has a hard deadline and a single malloc under contention is an
 * audible glitch. AtariSt_BridgeReadAudio is lock-free and pads any shortfall
 * with silence, which is the contract this needs.
 */
static OSStatus render(void *inRefCon,
                       AudioUnitRenderActionFlags *ioActionFlags,
                       const AudioTimeStamp *inTimeStamp,
                       UInt32 inBusNumber,
                       UInt32 inNumberFrames,
                       AudioBufferList *ioData)
{
	(void)inRefCon;
	(void)ioActionFlags;
	(void)inTimeStamp;
	(void)inBusNumber;

	/* Interleaved stereo: one buffer carrying both channels, which is what
	 * the stream format below asks for. A non-interleaved format would give
	 * mNumberBuffers == 2 and this loop would be wrong. */
	if (ioData->mNumberBuffers < 1)
		return noErr;

	int16_t *out = (int16_t *)ioData->mBuffers[0].mData;
	AtariSt_BridgeReadAudio(out, (int)inNumberFrames);
	ioData->mBuffers[0].mDataByteSize =
		inNumberFrames * 2 * (UInt32)sizeof(int16_t);
	return noErr;
}

#if TARGET_OS_IPHONE
/*
 * Ask for a session that plays even with the ringer switch on.
 *
 * Ambient would be the polite choice -- it mixes with other audio and honours
 * the silent switch -- but a game the user has deliberately started is not
 * ambient. Playback with MixWithOthers is the compromise: audible regardless
 * of the switch, and it does not stop whatever podcast they had going.
 */
static bool configure_session(void)
{
	NSError *error = nil;
	AVAudioSession *session = [AVAudioSession sharedInstance];

	if (![session setCategory:AVAudioSessionCategoryPlayback
	              withOptions:AVAudioSessionCategoryOptionMixWithOthers
	                    error:&error]) {
		AtariSt_BridgeSetError("could not set the audio session category: %s",
		                       error.localizedDescription.UTF8String);
		return false;
	}
	if (![session setActive:YES error:&error]) {
		AtariSt_BridgeSetError("could not activate the audio session: %s",
		                       error.localizedDescription.UTF8String);
		return false;
	}
	return true;
}
#endif

bool AtariSt_AudioSinkStart(int sample_rate)
{
	if (sink_started)
		return true;

#if TARGET_OS_IPHONE
	if (!configure_session())
		return false;
#endif

	AudioComponentDescription desc = {
		.componentType = kAudioUnitType_Output,
#if TARGET_OS_IPHONE
		.componentSubType = kAudioUnitSubType_RemoteIO,
#else
		.componentSubType = kAudioUnitSubType_DefaultOutput,
#endif
		.componentManufacturer = kAudioUnitManufacturer_Apple,
	};

	AudioComponent component = AudioComponentFindNext(NULL, &desc);
	if (component == NULL) {
		AtariSt_BridgeSetError("no output audio unit is available");
		return false;
	}

	OSStatus status = AudioComponentInstanceNew(component, &audio_unit);
	if (status != noErr) {
		AtariSt_BridgeSetError("could not create the output audio unit (%d)",
		                       (int)status);
		return false;
	}

	AudioStreamBasicDescription format = {
		.mSampleRate = sample_rate > 0 ? sample_rate : 44100,
		.mFormatID = kAudioFormatLinearPCM,
		.mFormatFlags = kAudioFormatFlagIsSignedInteger |
		                kAudioFormatFlagIsPacked,
		.mFramesPerPacket = 1,
		.mChannelsPerFrame = 2,
		.mBitsPerChannel = 16,
		.mBytesPerFrame = 4,   /* 2 channels * 16 bits, interleaved */
		.mBytesPerPacket = 4,
	};

	/* Bus 0 is the OUTPUT bus of the IO unit, and the scope is Input --
	 * "the input to the output bus", i.e. what we hand it. Getting this pair
	 * the wrong way round is the classic RemoteIO mistake and silently
	 * configures the microphone path instead. */
	status = AudioUnitSetProperty(audio_unit, kAudioUnitProperty_StreamFormat,
	                              kAudioUnitScope_Input, 0, &format,
	                              sizeof(format));
	if (status != noErr) {
		AtariSt_BridgeSetError("audio device refused 16-bit stereo (%d)",
		                       (int)status);
		AudioComponentInstanceDispose(audio_unit);
		audio_unit = NULL;
		return false;
	}

	AURenderCallbackStruct callback = {
		.inputProc = render,
		.inputProcRefCon = NULL,
	};
	status = AudioUnitSetProperty(audio_unit,
	                              kAudioUnitProperty_SetRenderCallback,
	                              kAudioUnitScope_Input, 0, &callback,
	                              sizeof(callback));
	if (status != noErr) {
		AtariSt_BridgeSetError("could not install the render callback (%d)",
		                       (int)status);
		AudioComponentInstanceDispose(audio_unit);
		audio_unit = NULL;
		return false;
	}

	if ((status = AudioUnitInitialize(audio_unit)) != noErr ||
	    (status = AudioOutputUnitStart(audio_unit)) != noErr) {
		AtariSt_BridgeSetError("could not start audio output (%d)",
		                       (int)status);
		AudioComponentInstanceDispose(audio_unit);
		audio_unit = NULL;
		return false;
	}

	sink_started = true;
	return true;
}

void AtariSt_AudioSinkStop(void)
{
	if (!audio_unit)
		return;
	AudioOutputUnitStop(audio_unit);
	AudioUnitUninitialize(audio_unit);
	AudioComponentInstanceDispose(audio_unit);
	audio_unit = NULL;
	sink_started = false;

#if TARGET_OS_IPHONE
	/* Hand the session back so another app can take the device -- and do NOT
	 * treat a failure as an error worth surfacing: deactivation routinely
	 * fails while something else is mid-transition, and it does not matter. */
	[[AVAudioSession sharedInstance] setActive:NO
	                               withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
	                                     error:nil];
#endif
}

const char *AtariSt_AudioSinkName(void)
{
#if TARGET_OS_IPHONE
	return "AudioUnit (iOS)";
#else
	return "AudioUnit (macOS)";
#endif
}
