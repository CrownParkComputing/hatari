/*
 * backend/timing.c - VBL pacing, and the emulation thread's one meeting point
 * with the bridge.
 *
 * Hatari calls Timing_WaitOnVbl once per emulated vertical blank. That is the
 * only moment in the frame at which the ST is quiescent -- no half-executed
 * instruction, no FDC transfer in flight, no snapshot-hostile state -- so it
 * is where the bridge services its mailbox, drains queued keystrokes and parks
 * while paused. Everything that must "run on the emulation thread" runs here.
 *
 * Unlike src/retro/timing.c (which yields to a libretro host that owns the
 * clock) this backend is self-driving: the emulation thread IS the clock, so
 * this function genuinely sleeps.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdatomic.h>
#include <time.h>

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "video.h"

#include "atarist_internal.h"

static int64_t next_frame_us;
static int vbl_slowdown = 1;

int64_t Timing_GetTicks(void)
{
	/* CLOCK_MONOTONIC, not gettimeofday as src/retro/timing.c uses: a
	 * wall-clock jump (NTP step, or the user changing the timezone on a
	 * phone) would otherwise either stall the emulator for the length of
	 * the jump or run it flat out to "catch up". */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void Timing_PrintSpeed(void)
{
}

uint32_t Timing_SetRunVBLs(uint32_t vbls)
{
	static uint32_t nRunVBLs;

	if (!vbls)
		return nRunVBLs;
	nRunVBLs = vbls;
	return 0;
}

const char *Timing_SetVBLSlowdown(int factor)
{
	if (factor < 1 || factor > 8)
		return "VBL slowdown factor must be 1-8";
	vbl_slowdown = factor;
	return NULL;
}

static void sleep_until(int64_t target_us)
{
	for (;;) {
		int64_t now = Timing_GetTicks();
		if (now >= target_us)
			return;

		int64_t remaining = target_us - now;
		struct timespec req = {
			.tv_sec = remaining / 1000000,
			.tv_nsec = (remaining % 1000000) * 1000,
		};
		/* Retried on EINTR: a signal (the profiler, a debugger
		 * attaching) would otherwise shorten the frame and show up as
		 * a one-off speed-up. */
		if (nanosleep(&req, NULL) == 0 || errno != EINTR)
			return;
	}
}

void Timing_WaitOnVbl(void)
{
	/* Everything the UI thread asked for, applied at the safe point. */
	AtariSt_BridgeVblService();

	const int64_t frame_us =
		(int64_t)(1000000.0 / (nScreenRefreshRate > 0 ? nScreenRefreshRate : 50))
		* vbl_slowdown;

	int64_t now = Timing_GetTicks();
	if (next_frame_us == 0)
		next_frame_us = now;

	next_frame_us += frame_us;

	/* If we are more than a frame behind -- the app was backgrounded, or a
	 * disk image was being decompressed on this thread -- resynchronise
	 * rather than sprinting through the backlog at 400% speed, which is
	 * both unwatchable and, in a game, unplayable. */
	if (now > next_frame_us + frame_us)
		next_frame_us = now + frame_us;
	else
		sleep_until(next_frame_us);

	/* Measured FPS, averaged over a second so the readout is legible. */
	static int64_t window_start_us;
	static int frames_in_window;
	if (window_start_us == 0)
		window_start_us = now;
	frames_in_window++;
	if (now - window_start_us >= 1000000) {
		atomic_store(&g_atarist.fps,
		             (int32_t)(frames_in_window * 1000000 /
		                       (now - window_start_us)));
		window_start_us = now;
		frames_in_window = 0;
	}
}

void Timing_CheckForAccurateDelays(void)
{
	/* Hatari uses this to decide whether it can trust short sleeps.
	 * nanosleep on every platform we target is accurate enough, and the
	 * resynchronise-when-behind path above absorbs the rest. */
}
