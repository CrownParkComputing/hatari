/*
 * atarist_internal.h - state shared between the bridge and the UI backend.
 *
 * Private to native/atarist_core. Nothing here crosses the public C ABI;
 * atarist_bridge.h is the only public surface.
 *
 * Why this file exists: Hatari's UI is a *seam*, not a library. The core calls
 * into Screen_*, Audio_*, Keymap_*, JoyUI_*, Statusbar_* and Timing_* and
 * expects someone to provide them -- upstream provides src/sdl/ (a real
 * window) and src/retro/ (libretro callbacks). We provide a third
 * implementation, backend/, which renders into memory and takes its input from
 * the launcher. That is why no Hatari source file is patched anywhere in this
 * project: we are a peer of src/sdl/, not a modification of it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef RETRO_ATARIST_INTERNAL_H
#define RETRO_ATARIST_INTERNAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "atarist_bridge.h"

/* Ring of decoded audio, filled by backend/audio.c from Hatari's
 * AudioMixBuffer and drained by whatever host sink the platform provides
 * (Oboe on Android, AudioUnit on iOS, PulseAudio/PipeWire on Linux).
 *
 * Sized to a quarter second at 48 kHz stereo: long enough that a scheduling
 * hiccup on either side does not tear, short enough that the ST's own timing
 * still drives perceived latency. */
#define ATARIST_AUDIO_RING_FRAMES 12288

typedef struct {
	int16_t buf[ATARIST_AUDIO_RING_FRAMES][2];
	/* Both indices are frame counts that only ever increase; the ring
	 * position is index % ATARIST_AUDIO_RING_FRAMES. Free-running
	 * counters rather than wrapped indices so "how far behind is the
	 * reader" is a subtraction and never an ambiguous equality -- with
	 * wrapped indices, full and empty look identical. */
	_Atomic uint64_t written;
	_Atomic uint64_t read;
} AtariStAudioRing;

/*
 * Requests that must run ON the emulation thread.
 *
 * ST machine state is not thread-safe in any sense: Hatari assumes a single
 * thread owns the CPU, the FDC and the memory snapshot code. So the UI thread
 * posts one of these, signals, and waits; backend/timing.c services the
 * mailbox once per VBL, which is a point where the machine is quiescent.
 */
typedef enum {
	ATARIST_REQ_NONE = 0,
	ATARIST_REQ_RESET_COLD,
	ATARIST_REQ_RESET_WARM,
	ATARIST_REQ_SET_FLOPPY,
	ATARIST_REQ_SWAP_CONFIG,
	ATARIST_REQ_SAVE_STATE,
	ATARIST_REQ_LOAD_STATE,
	ATARIST_REQ_QUIT,
} AtariStRequestKind;

typedef struct {
	pthread_mutex_t lock;
	pthread_cond_t posted;    /* UI -> emu: a request is waiting */
	pthread_cond_t completed; /* emu -> UI: it has been serviced */

	AtariStRequestKind kind;
	bool pending;

	/* Arguments. Union'ing these would save nothing worth the confusion. */
	int32_t drive;
	int32_t slot;
	char path[1024];

	int32_t result;
} AtariStMailbox;

/* The whole bridge's state. One instance, file-scope in atarist_bridge.c. */
typedef struct {
	bool initialised;

	_Atomic bool running;
	_Atomic bool paused;
	_Atomic bool quit_requested;

	pthread_t emu_thread;
	bool emu_thread_valid;

	/* Gate the emulation thread parks on while paused, so a paused core
	 * costs no CPU at all. Signalled by atarist_core_set_paused. */
	pthread_mutex_t pause_lock;
	pthread_cond_t pause_cond;

	AtariStMailbox mailbox;
	AtariStAudioRing audio;

	/* Input, written by the UI thread and read by the emulation thread on
	 * its own schedule. Plain atomics rather than a queue: these are
	 * level-triggered states (which way is the stick held), not events,
	 * so the newest value is always the correct one and a dropped
	 * intermediate value is not a lost input. */
	_Atomic uint32_t joy_mask[2];
	_Atomic int32_t mouse_dx;
	_Atomic int32_t mouse_dy;
	_Atomic uint32_t mouse_buttons;

	/* Keyboard IS event-triggered -- a make with no break leaves a key
	 * stuck down forever -- so it goes through a small lock-free ring
	 * instead of a level. */
#define ATARIST_KEY_RING 256
	struct {
		uint8_t scancode;
		uint8_t pressed;
	} key_ring[ATARIST_KEY_RING];
	_Atomic uint32_t key_written;
	_Atomic uint32_t key_read;

	/* Status, published by the emulation thread for the UI to poll. */
	_Atomic int32_t fps;
	_Atomic int32_t audio_level;
	_Atomic int64_t frame_counter;

	char status_line[128];
	char last_error[512];
	char work_dir[1024];
	char tos_dir[1024];
	char floppy_path[2][1024];

	AtariStConfig cfg;
	/* Owned copies of every path in cfg: the caller strings behind the
	 * pointers passed to atarist_core_start are freed as soon as that call
	 * returns, and the emulation thread reads them long afterwards. */
	char cfg_tos[1024];
	char cfg_floppy_a[1024];
	char cfg_floppy_b[1024];
	char cfg_gemdos[1024];
	char cfg_acsi[1024];
	char cfg_ide[1024];
} AtariStBridge;

extern AtariStBridge g_atarist;

/* Called by backend/timing.c once per VBL, on the emulation thread. Services
 * the mailbox, applies pending input, parks while paused. */
void AtariSt_BridgeVblService(void);

/* Called by backend/screen.c when a frame is complete. */
void AtariSt_BridgePublishFrame(const uint32_t *pixels, int width, int height,
                                int pitch_bytes);

/* Called by backend/audio.c with freshly generated stereo frames. */
void AtariSt_BridgePushAudio(const int16_t *frames, int frame_count);

/* Records a message for atarist_core_last_error. Safe from either thread. */
void AtariSt_BridgeSetError(const char *fmt, ...);

/* Drain decoded audio into a host sink. Returns frames actually written.
 * Exposed here (not in the public header) because the platform audio glue
 * lives in this directory too. */
int AtariSt_BridgeReadAudio(int16_t *out, int frame_count);

#endif /* RETRO_ATARIST_INTERNAL_H */
