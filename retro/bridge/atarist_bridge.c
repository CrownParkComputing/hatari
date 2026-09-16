/*
 * atarist_bridge.c - the native frontend-facing implementation.
 *
 * Owns the emulation thread, the request mailbox and the input/status state
 * that crosses between it and the UI thread. Everything Hatari's core calls
 * *into* lives in ../backend/.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "main.h"
#include "configuration.h"
#include "floppy.h"
#include "ikbd.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "change.h"
#include "reset.h"
#include "version.h"
#include "video.h"

#include "atarist_bridge.h"
#include "atarist_internal.h"
#include "audio_sink.h"

/* Implemented in ../backend/keymap.c. */
void Keymap_NoteScanCode(uint8_t scancode, bool pressed);

AtariStBridge g_atarist;

/* Our ATARIST_MACHINE_* values are passed straight through to Hatari's
 * --machine option by name, but the numeric mapping is also asserted so a
 * reordering of MACHINETYPE upstream is a build failure here rather than a
 * launcher that silently boots a Falcon when the user picked an STE. */
_Static_assert(ATARIST_MACHINE_ST == MACHINE_ST, "MACHINETYPE reordered");
_Static_assert(ATARIST_MACHINE_MEGA_ST == MACHINE_MEGA_ST, "MACHINETYPE reordered");
_Static_assert(ATARIST_MACHINE_STE == MACHINE_STE, "MACHINETYPE reordered");
_Static_assert(ATARIST_MACHINE_MEGA_STE == MACHINE_MEGA_STE, "MACHINETYPE reordered");
_Static_assert(ATARIST_MACHINE_TT == MACHINE_TT, "MACHINETYPE reordered");
_Static_assert(ATARIST_MACHINE_FALCON == MACHINE_FALCON, "MACHINETYPE reordered");

_Static_assert(ATARIST_MONITOR_MONO == MONITOR_TYPE_MONO, "MONITORTYPE reordered");
_Static_assert(ATARIST_MONITOR_RGB == MONITOR_TYPE_RGB, "MONITORTYPE reordered");
_Static_assert(ATARIST_MONITOR_VGA == MONITOR_TYPE_VGA, "MONITORTYPE reordered");
_Static_assert(ATARIST_MONITOR_TV == MONITOR_TYPE_TV, "MONITORTYPE reordered");

/* --------------------------------------------------------------- utilities */

void AtariSt_BridgeSetError(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_atarist.last_error, sizeof(g_atarist.last_error), fmt, ap);
	va_end(ap);
}

static void copy_path(char *dst, size_t cap, const char *src)
{
	if (!src || !*src) {
		dst[0] = '\0';
		return;
	}
	snprintf(dst, cap, "%s", src);
}

static bool file_exists(const char *path)
{
	struct stat st;
	return path && *path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Absolute path of slot [slot]'s snapshot, inside the work dir. */
static void slot_path(char *out, size_t cap, int32_t slot)
{
	snprintf(out, cap, "%s/states/slot%d.sav", g_atarist.work_dir, slot);
}

static void ensure_dir(const char *path)
{
	/* mkdir -p, one component at a time. No error handling beyond
	 * "already exists is fine": if the directory genuinely cannot be
	 * created the first write there reports it with a real errno, which
	 * is a better message than anything invented here. */
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(tmp, 0755);
		*p = '/';
	}
	mkdir(tmp, 0755);
}

/* ------------------------------------------------------- argv construction */

/*
 * Hatari's command line, not its CNF_PARAMS struct, is the configuration
 * interface we target. CNF_PARAMS is ~40 nested structs whose layout changes
 * between releases; the options in options.c are documented, stable, and
 * validated by Hatari itself (an out-of-range memsize is rejected with a
 * message rather than booting a broken machine).
 */
#define MAX_ARGS 48

typedef struct {
	char *argv[MAX_ARGS];
	int argc;
} ArgList;

static void arg_add(ArgList *a, const char *value)
{
	if (a->argc >= MAX_ARGS - 1) {
		AtariSt_BridgeSetError("too many Hatari arguments (>%d)", MAX_ARGS);
		return;
	}
	a->argv[a->argc++] = strdup(value);
	a->argv[a->argc] = NULL;
}

static void arg_addf(ArgList *a, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	arg_add(a, buf);
}

static void arg_free(ArgList *a)
{
	for (int i = 0; i < a->argc; i++)
		free(a->argv[i]);
	a->argc = 0;
}

static const char *monitor_name(int32_t monitor)
{
	switch (monitor) {
	case ATARIST_MONITOR_MONO: return "mono";
	case ATARIST_MONITOR_VGA:  return "vga";
	case ATARIST_MONITOR_TV:   return "tv";
	case ATARIST_MONITOR_RGB:
	default:                   return "rgb";
	}
}

static const char *machine_name(int32_t machine)
{
	switch (machine) {
	case ATARIST_MACHINE_ST:       return "st";
	case ATARIST_MACHINE_MEGA_ST:  return "megast";
	case ATARIST_MACHINE_STE:      return "ste";
	case ATARIST_MACHINE_MEGA_STE: return "megaste";
	case ATARIST_MACHINE_TT:       return "tt";
	case ATARIST_MACHINE_FALCON:   return "falcon";
	default:                       return "st";
	}
}

static void build_args(ArgList *a, const AtariStConfig *cfg)
{
	memset(a, 0, sizeof(*a));

	/* argv[0] is not decoration: Paths_Init(argv[0]) derives Hatari's
	 * data directory from it. */
	arg_add(a, "hatari");

	/* FIRST, before any setting option -- and that ordering is load-bearing.
	 *
	 * --configfile does not merely name a file to save to: its handler calls
	 * Configuration_Load(), which overwrites the WHOLE of ConfigureParams
	 * from that file. Emitted last, it therefore undid every option before
	 * it. The visible symptom was Hatari logging
	 *
	 *     Inserted disk '...' to drive A:.
	 *     Floppy A: has been removed from drive.
	 *
	 * one line apart, and a machine that booted to a bare desktop with no
	 * game in the drive -- which reads as "the game does not launch" rather
	 * than as an argument-ordering mistake.
	 *
	 * Kept inside our work dir rather than ~/.config/hatari: a launcher that
	 * silently rewrote the user's standalone Hatari setup would be a
	 * genuinely nasty surprise.
	 *
	 * The file has to EXIST first. --configfile is checked with CHECK_FILE
	 * (options.c), so pointing it at a path that is merely where the config
	 * will live fails parameter parsing outright -- Hatari prints its usage
	 * and Main_Init exits, which from the launcher looks like "the game does
	 * not start" with nothing to say why. An empty file is a valid config;
	 * Hatari fills it in. */
	{
		char cfg_path[1200];
		snprintf(cfg_path, sizeof(cfg_path), "%s/hatari.cfg",
		         g_atarist.work_dir);
		if (!file_exists(cfg_path)) {
			ensure_dir(g_atarist.work_dir);
			FILE *f = fopen(cfg_path, "a");
			if (f)
				fclose(f);
		}
		if (file_exists(cfg_path)) {
			arg_add(a, "--configfile");
			arg_add(a, cfg_path);
		} else {
			/* Could not create it -- run with Hatari's defaults rather
			 * than failing to boot over a config file. */
			AtariSt_BridgeSetError(
				"could not create '%s'; running with default settings",
				cfg_path);
		}
	}

	arg_add(a, "--machine");
	arg_add(a, machine_name(cfg->machine));

	if (cfg->memory_kb > 0) {
		arg_add(a, "--memsize");
		arg_addf(a, "%d", cfg->memory_kb);
	}

	if (g_atarist.cfg_tos[0]) {
		arg_add(a, "--tos");
		arg_add(a, g_atarist.cfg_tos);
	}

	if (g_atarist.cfg_floppy_a[0]) {
		arg_add(a, "--disk-a");
		arg_add(a, g_atarist.cfg_floppy_a);
	}
	if (g_atarist.cfg_floppy_b[0]) {
		arg_add(a, "--disk-b");
		arg_add(a, g_atarist.cfg_floppy_b);
	}
	if (g_atarist.cfg_gemdos[0]) {
		arg_add(a, "--harddrive");
		arg_add(a, g_atarist.cfg_gemdos);
	}
	if (g_atarist.cfg_acsi[0]) {
		/* "<id>=<file>", with the id given explicitly. Hatari's
		 * Opt_DriveValue accepts a bare path too, but being explicit is
		 * what keeps this correct if a second ACSI device is ever added. */
		arg_add(a, "--acsi");
		arg_addf(a, "0=%s", g_atarist.cfg_acsi);
	}
	if (g_atarist.cfg_ide[0]) {
		arg_add(a, "--ide-master");
		arg_add(a, g_atarist.cfg_ide);
	}

	/* Without this the machine comes up on whatever monitor Hatari's
	 * defaults happen to name, and a mono monitor puts TOS into ST-HIGH --
	 * a 640x400 two-colour desktop in which no game will start. It boots,
	 * it draws, it reaches the desktop, and the game never appears, which
	 * is a particularly unhelpful way to fail. */
	arg_add(a, "--monitor");
	arg_add(a, monitor_name(cfg->monitor));

	arg_add(a, "--blitter");
	arg_add(a, cfg->blitter ? "on" : "off");

	/* --fastfdc on is Hatari's default and boots in a couple of seconds.
	 * Off means cycle-accurate FDC timing, which protected originals need
	 * and which makes a load take as long as it did in 1988 -- so it is
	 * opt-in per title, never a global. */
	arg_add(a, "--fastfdc");
	arg_add(a, cfg->accurate_floppy ? "off" : "on");

	arg_add(a, "--sound");
	arg_addf(a, "%d", cfg->sample_rate > 0 ? cfg->sample_rate : 44100);

	/* The launcher owns the window, so Hatari must not try to open one,
	 * scale anything, or draw a statusbar into the ST's framebuffer. */
	arg_add(a, "--statusbar");
	arg_add(a, "off");
	arg_add(a, "--zoom");
	arg_add(a, "1");
	arg_add(a, "--frameskips");
	arg_add(a, "0");

	/* "--joy0", not "--joystick0": Hatari's option table lists this as
	 * "--joy<port>" and the parser takes the port from the LAST CHARACTER of
	 * the option itself (options.c: port = opt[strlen(opt)-1] - '0').
	 * "--joystick0" is rejected outright with "Unrecognized option", which
	 * makes Main_Init exit before a single frame is drawn.
	 *
	 * Both ports are set: ST games read port 1 and the mouse lives on port 0,
	 * but a handful of titles disagree and driving both costs nothing.
	 *
	 * "real" rather than "keys" -- see backend/joy_ui.c: JOYSTICK_KEYBOARD
	 * makes joy.c ignore the stick whenever shift is held. */
	arg_add(a, "--joy0");
	arg_add(a, cfg->joystick_port1 ? "real" : "none");
	arg_add(a, "--joy1");
	arg_add(a, cfg->joystick_port1 ? "real" : "none");

}

/* ----------------------------------------------------------- emu thread */

/* Lift the emulation thread above ordinary background work.
 *
 * Hatari runs in-process beside UIKit and Metal; at equal
 * priority a busy launcher frame starves the audio producer and the sound
 * breaks up -- the exact failure Retro-Amiga's live release reports were
 * about, fixed there with the same call. -2 lifts this thread above default
 * work while leaving the platform's audio callback (higher still) alone.
 *
 * An app may do this to its own threads: Android raises RLIMIT_NICE for app
 * processes precisely so it can. Where it may not (an unprivileged desktop)
 * the call fails and the emulator runs exactly as it did.
 */
static void raise_emulation_thread_priority(void)
{
#if defined(__linux__)
	errno = 0;
	if (setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), -2) != 0 &&
	    errno != 0) {
		fprintf(stderr,
		        "atarist: could not raise emulation thread priority (%s)\n",
		        strerror(errno));
	}
#endif
}

static void *emu_thread_main(void *unused)
{
	raise_emulation_thread_priority();
	(void)unused;

	ArgList args;
	build_args(&args, &g_atarist.cfg);

	/* Main_Init parses the arguments, applies the configuration and brings
	 * up every subsystem -- including ours, since backend/ supplies the UI
	 * half of the seam. */
	Main_Init(args.argc, args.argv);

	atomic_store(&g_atarist.running, true);

	/* Match Hatari's own main(): subsystem initialisation may leave its
	 * internal bEmulationActive flag paused on a headless/mobile target.
	 * Without this call the bridge says "running" but never reaches a VBL,
	 * leaving the frontend permanently at "Booting...". */
	Main_UnPauseEmulation();

	/* M68000_Start does not return until bQuitProgram is set, which is
	 * what atarist_core_stop does through the mailbox. */
	M68000_Start();

	Main_UnInit();
	arg_free(&args);

	atomic_store(&g_atarist.running, false);
	return NULL;
}

/* -------------------------------------------------------------- mailbox */

/* Post a request and block until the emulation thread has serviced it. */
static int32_t mailbox_call(AtariStRequestKind kind, int32_t drive,
                            int32_t slot, const char *path)
{
	AtariStMailbox *mb = &g_atarist.mailbox;

	if (!atomic_load(&g_atarist.running))
		return ATARIST_NOT_RUNNING;

	pthread_mutex_lock(&mb->lock);
	mb->kind = kind;
	mb->drive = drive;
	mb->slot = slot;
	copy_path(mb->path, sizeof(mb->path), path);
	mb->result = ATARIST_ERR;
	mb->pending = true;
	pthread_cond_signal(&mb->posted);

	/* A paused core still services the mailbox (see the pause park in
	 * AtariSt_BridgeVblService), so this does not need to un-pause first
	 * -- but it does need a deadline, because a core wedged in a tight
	 * 68000 loop with interrupts off will never reach a VBL. */
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 10;

	int rc = 0;
	while (mb->pending && rc == 0)
		rc = pthread_cond_timedwait(&mb->completed, &mb->lock, &deadline);

	int32_t result = mb->result;
	if (rc == ETIMEDOUT && mb->pending) {
		mb->pending = false;
		result = ATARIST_TIMEOUT;
		AtariSt_BridgeSetError(
			"emulation thread did not service request %d in 10s", kind);
	}
	pthread_mutex_unlock(&mb->lock);
	return result;
}

static void mailbox_service(void)
{
	AtariStMailbox *mb = &g_atarist.mailbox;

	pthread_mutex_lock(&mb->lock);
	if (!mb->pending) {
		pthread_mutex_unlock(&mb->lock);
		return;
	}

	char state_file[1200];

	switch (mb->kind) {
	case ATARIST_REQ_RESET_COLD:
		mb->result = Reset_Cold() == 0 ? ATARIST_OK : ATARIST_ERR;
		break;

	case ATARIST_REQ_RESET_WARM:
		mb->result = Reset_Warm() == 0 ? ATARIST_OK : ATARIST_ERR;
		break;

	case ATARIST_REQ_SET_FLOPPY:
		if (mb->path[0]) {
			Floppy_SetDiskFileName(mb->drive, mb->path, NULL);
			mb->result = Floppy_InsertDiskIntoDrive(mb->drive)
			                     ? ATARIST_OK : ATARIST_ERR;
		} else {
			Floppy_EjectDiskFromDrive(mb->drive);
			Floppy_SetDiskFileNameNone(mb->drive);
			mb->result = ATARIST_OK;
		}
		if (mb->result == ATARIST_OK) {
			copy_path(g_atarist.floppy_path[mb->drive],
			          sizeof(g_atarist.floppy_path[0]), mb->path);
		}
		break;

	case ATARIST_REQ_SAVE_STATE:
		/* An explicit path wins over the slot. Per-title auto-saves need a
		 * file per game, and slots are a single global set -- with slots
		 * alone, closing one game would overwrite the position of the last
		 * one, which is precisely the thing this is meant to prevent. */
		if (mb->path[0])
			snprintf(state_file, sizeof(state_file), "%s", mb->path);
		else
			slot_path(state_file, sizeof(state_file), mb->slot);
		{
			/* The directory of whatever file we are about to write --
			 * which is not always <work>/states once callers can name
			 * their own path. */
			char dir[1200];
			snprintf(dir, sizeof(dir), "%s", state_file);
			char *slash = strrchr(dir, '/');
			if (slash) {
				*slash = '\0';
				ensure_dir(dir);
			}
		}
		/* _Immediate, not the deferred Capture: we are already on the
		 * emulation thread at a VBL, which is exactly the point the
		 * deferred version schedules itself for. */
		MemorySnapShot_Capture_Immediate(state_file, false);
		mb->result = file_exists(state_file) ? ATARIST_OK : ATARIST_ERR;
		break;

	case ATARIST_REQ_LOAD_STATE:
		if (mb->path[0])
			snprintf(state_file, sizeof(state_file), "%s", mb->path);
		else
			slot_path(state_file, sizeof(state_file), mb->slot);
		if (!file_exists(state_file)) {
			mb->result = ATARIST_ERR;
			break;
		}
		MemorySnapShot_Restore(state_file, false);
		mb->result = ATARIST_OK;
		break;

	case ATARIST_REQ_SWAP_CONFIG: {
		/*
		 * Re-point the RUNNING machine at a different title.
		 *
		 * This exists because Hatari cannot be initialised twice in one
		 * process. Its own main() calls Main_Init once and then exits, so
		 * nothing upstream ever exercises a second init -- and a second
		 * one segfaults immediately:
		 *
		 *   STMemory_Init (stMemory.c:69)  val = IoMem[0xff8001];
		 *   Configuration_Apply (configuration.c:911)
		 *   Main_Init (main.c:325)
		 *
		 * because Main_UnInit has already released IoMem. So the core is
		 * started ONCE and every later "launch" is a configuration change
		 * plus a cold reset, which is exactly how the sibling Retro-C64
		 * app hot-swaps VICE.
		 *
		 * Change_CopyChangedParamsToConfiguration is Hatari's own supported
		 * path for this -- it is what the SDL setup GUI calls -- and it
		 * works out which subsystems need re-initialising (memory, IoMem,
		 * GEMDOS, the screen mode) rather than us guessing.
		 */
		CNF_PARAMS next = ConfigureParams;

		next.System.nMachineType = g_atarist.cfg.machine;
		next.System.bBlitter = g_atarist.cfg.blitter != 0;
		if (g_atarist.cfg.memory_kb > 0)
			next.Memory.STRamSize_KB = g_atarist.cfg.memory_kb;
		next.Screen.nMonitorType = g_atarist.cfg.monitor;
		next.DiskImage.FastFloppy = g_atarist.cfg.accurate_floppy == 0;

		snprintf(next.Rom.szTosImageFileName,
		         sizeof(next.Rom.szTosImageFileName), "%s",
		         g_atarist.cfg_tos);
		snprintf(next.DiskImage.szDiskFileName[0],
		         sizeof(next.DiskImage.szDiskFileName[0]), "%s",
		         g_atarist.cfg_floppy_a);
		snprintf(next.DiskImage.szDiskFileName[1],
		         sizeof(next.DiskImage.szDiskFileName[1]), "%s",
		         g_atarist.cfg_floppy_b);
		/* The zip sub-paths belong to the OLD images; leaving them set
		 * makes Hatari look for a member that is not in the new file. */
		next.DiskImage.szDiskZipPath[0][0] = '\0';
		next.DiskImage.szDiskZipPath[1][0] = '\0';

		next.HardDisk.bUseHardDiskDirectories = g_atarist.cfg_gemdos[0] != '\0';
		snprintf(next.HardDisk.szHardDiskDirectories[0],
		         sizeof(next.HardDisk.szHardDiskDirectories[0]), "%s",
		         g_atarist.cfg_gemdos);

		/* bForceReset: a new title always gets a cold machine. Letting
		 * Change_DoNeedReset decide would leave the previous game's RAM
		 * in place whenever only the disk changed, and a surprising number
		 * of ST loaders check memory they have no business checking. */
		Change_CopyChangedParamsToConfiguration(&ConfigureParams, &next, true);

		copy_path(g_atarist.floppy_path[0], sizeof(g_atarist.floppy_path[0]),
		          g_atarist.cfg_floppy_a);
		copy_path(g_atarist.floppy_path[1], sizeof(g_atarist.floppy_path[1]),
		          g_atarist.cfg_floppy_b);
		mb->result = ATARIST_OK;
		break;
	}

	case ATARIST_REQ_QUIT:
		bQuitProgram = true;
		M68000_SetSpecial(SPCFLAG_BRK);
		mb->result = ATARIST_OK;
		break;

	case ATARIST_REQ_NONE:
	default:
		mb->result = ATARIST_ERR;
		break;
	}

	mb->pending = false;
	pthread_cond_signal(&mb->completed);
	pthread_mutex_unlock(&mb->lock);
}

/* ------------------------------------------------- per-VBL service point */

static void drain_key_ring(void)
{
	uint32_t read = atomic_load(&g_atarist.key_read);
	uint32_t written = atomic_load(&g_atarist.key_written);
	while (read != written) {
		uint32_t idx = read % ATARIST_KEY_RING;
		uint8_t scancode = g_atarist.key_ring[idx].scancode;
		bool pressed = g_atarist.key_ring[idx].pressed != 0;
		/* Told to the keymap first: joy.c consults Keymap_IsShiftPressed
		 * in the same frame, and learning about a shift press one frame
		 * late is what makes the first shifted keystroke after a pause
		 * come out unshifted. */
		Keymap_NoteScanCode(scancode, pressed);
		IKBD_PressSTKey(scancode, pressed);
		read++;
	}
	atomic_store(&g_atarist.key_read, read);
}

void AtariSt_BridgeVblService(void)
{
	drain_key_ring();
	mailbox_service();

	atomic_store(&g_atarist.frame_counter, (int64_t)nVBLs);

	/* Park while paused, waking for each mailbox post so a paused session
	 * can still be reset, snapshotted or torn down. */
	if (atomic_load(&g_atarist.paused)) {
		pthread_mutex_lock(&g_atarist.pause_lock);
		while (atomic_load(&g_atarist.paused) &&
		       !atomic_load(&g_atarist.quit_requested)) {
			struct timespec wake;
			clock_gettime(CLOCK_REALTIME, &wake);
			wake.tv_nsec += 20 * 1000 * 1000; /* 20ms */
			if (wake.tv_nsec >= 1000000000L) {
				wake.tv_nsec -= 1000000000L;
				wake.tv_sec++;
			}
			pthread_cond_timedwait(&g_atarist.pause_cond,
			                       &g_atarist.pause_lock, &wake);
			pthread_mutex_unlock(&g_atarist.pause_lock);
			mailbox_service();
			pthread_mutex_lock(&g_atarist.pause_lock);
		}
		pthread_mutex_unlock(&g_atarist.pause_lock);
	}
}

/* ------------------------------------------------------- backend callbacks */

void AtariSt_BridgePublishFrame(const uint32_t *pixels, int width, int height,
                                int pitch_bytes)
{
	(void)pixels;
	(void)width;
	(void)height;
	(void)pitch_bytes;
	/* The framebuffer is not copied here. backend/screen.c owns the
	 * buffer for the whole life of the core and atarist_core_get_framebuffer
	 * hands out a pointer straight into it, so a copy at this point would
	 * be pure cost -- the native frontend uploads it to a texture and never
	 * retains it. The frame counter is what tells the UI the contents
	 * changed, and that is bumped from the VBL service above. */
}

void AtariSt_BridgePushAudio(const int16_t *frames, int frame_count)
{
	AtariStAudioRing *ring = &g_atarist.audio;
	uint64_t written = atomic_load(&ring->written);
	uint64_t read = atomic_load(&ring->read);

	/* Drop the oldest rather than block: stalling here would stall the
	 * 68000, and a launcher whose emulation speed depends on whether the
	 * host audio sink is draining is a much worse bug than a click. */
	if (written - read + (uint64_t)frame_count > ATARIST_AUDIO_RING_FRAMES) {
		uint64_t overflow =
			written - read + (uint64_t)frame_count - ATARIST_AUDIO_RING_FRAMES;
		atomic_store(&ring->read, read + overflow);
	}

	int32_t peak = 0;
	for (int i = 0; i < frame_count; i++) {
		uint64_t idx = (written + (uint64_t)i) % ATARIST_AUDIO_RING_FRAMES;
		ring->buf[idx][0] = frames[i * 2];
		ring->buf[idx][1] = frames[i * 2 + 1];
		int32_t l = frames[i * 2] < 0 ? -frames[i * 2] : frames[i * 2];
		if (l > peak)
			peak = l;
	}
	atomic_store(&ring->written, written + (uint64_t)frame_count);

	/* Smoothed peak for the level meter: a raw per-buffer peak flickers
	 * far too fast to read at 50 buffers a second. */
	int32_t level = peak * 100 / 32768;
	int32_t prev = atomic_load(&g_atarist.audio_level);
	atomic_store(&g_atarist.audio_level,
	             level > prev ? level : (prev * 7 + level) / 8);
}

int AtariSt_BridgeReadAudio(int16_t *out, int frame_count)
{
	AtariStAudioRing *ring = &g_atarist.audio;
	uint64_t read = atomic_load(&ring->read);
	uint64_t written = atomic_load(&ring->written);
	uint64_t available = written - read;

	int n = (int)(available < (uint64_t)frame_count ? available : (uint64_t)frame_count);
	for (int i = 0; i < n; i++) {
		uint64_t idx = (read + (uint64_t)i) % ATARIST_AUDIO_RING_FRAMES;
		out[i * 2] = ring->buf[idx][0];
		out[i * 2 + 1] = ring->buf[idx][1];
	}
	atomic_store(&ring->read, read + (uint64_t)n);

	/* Underrun: fill the remainder with silence rather than leaving the
	 * caller's buffer holding the previous callback's samples, which is
	 * an audible buzz rather than a gap. */
	if (n < frame_count)
		memset(&out[n * 2], 0, (size_t)(frame_count - n) * 2 * sizeof(int16_t));
	return n;
}

/* ============================== public API ============================== */

void atarist_core_init(const char *work_dir, const char *tos_dir)
{
	if (g_atarist.initialised)
		return;

	memset(&g_atarist, 0, sizeof(g_atarist));
	pthread_mutex_init(&g_atarist.mailbox.lock, NULL);
	pthread_cond_init(&g_atarist.mailbox.posted, NULL);
	pthread_cond_init(&g_atarist.mailbox.completed, NULL);
	pthread_mutex_init(&g_atarist.pause_lock, NULL);
	pthread_cond_init(&g_atarist.pause_cond, NULL);

	copy_path(g_atarist.work_dir, sizeof(g_atarist.work_dir), work_dir);
	copy_path(g_atarist.tos_dir, sizeof(g_atarist.tos_dir), tos_dir);
	if (g_atarist.work_dir[0])
		ensure_dir(g_atarist.work_dir);

	g_atarist.initialised = true;
}

int32_t atarist_core_start(const AtariStConfig *cfg)
{
	if (!cfg)
		return ATARIST_ERR;
	if (!g_atarist.initialised)
		atarist_core_init(NULL, NULL);

	/* A core that is already up is re-pointed, not restarted -- see the
	 * ATARIST_REQ_SWAP_CONFIG handler for why a second Main_Init is not an
	 * option. This is deliberately not reported as ATARIST_ALREADY_STARTED:
	 * from the launcher's side "start this title" means the same thing
	 * whether or not something else was running. */
	const bool hot_swap =
		atomic_load(&g_atarist.running) && g_atarist.emu_thread_valid;

	g_atarist.cfg = *cfg;
	copy_path(g_atarist.cfg_tos, sizeof(g_atarist.cfg_tos), cfg->tos_path);
	copy_path(g_atarist.cfg_floppy_a, sizeof(g_atarist.cfg_floppy_a), cfg->floppy_a);
	copy_path(g_atarist.cfg_floppy_b, sizeof(g_atarist.cfg_floppy_b), cfg->floppy_b);
	copy_path(g_atarist.cfg_gemdos, sizeof(g_atarist.cfg_gemdos), cfg->gemdos_dir);
	copy_path(g_atarist.cfg_acsi, sizeof(g_atarist.cfg_acsi), cfg->acsi_image);
	copy_path(g_atarist.cfg_ide, sizeof(g_atarist.cfg_ide), cfg->ide_image);
	if (cfg->work_dir && *cfg->work_dir)
		copy_path(g_atarist.work_dir, sizeof(g_atarist.work_dir), cfg->work_dir);

	/* Checked here rather than left to Hatari: without a TOS image Hatari
	 * exits the process (Main_ErrorExit), which from the frontend looks
	 * like the app crashing rather than like a missing ROM. */
	if (!file_exists(g_atarist.cfg_tos)) {
		AtariSt_BridgeSetError(
			"no TOS ROM at '%s' -- the ST cannot boot without one",
			g_atarist.cfg_tos[0] ? g_atarist.cfg_tos : "(unset)");
		return ATARIST_NO_TOS;
	}

	copy_path(g_atarist.floppy_path[0], sizeof(g_atarist.floppy_path[0]),
	          g_atarist.cfg_floppy_a);
	copy_path(g_atarist.floppy_path[1], sizeof(g_atarist.floppy_path[1]),
	          g_atarist.cfg_floppy_b);

	atomic_store(&g_atarist.paused, false);

	if (hot_swap) {
		/* Un-pause first: a parked emulation thread never reaches the
		 * mailbox, and the request would time out after ten seconds. */
		atarist_core_set_paused(0);
		return mailbox_call(ATARIST_REQ_SWAP_CONFIG, 0, 0, NULL);
	}

	atomic_store(&g_atarist.quit_requested, false);
	atomic_store(&g_atarist.frame_counter, 0);

	/* Opened before the emulation thread starts, so the very first samples
	 * Hatari generates have somewhere to go. A failure here is reported but
	 * NOT fatal -- a headless machine, a container with no audio device, or
	 * a busy exclusive-mode device should all still run the game. */
	if (!AtariSt_AudioSinkStart(cfg->sample_rate > 0 ? cfg->sample_rate : 44100)) {
		fprintf(stderr, "atarist: no audio output (%s)\n",
		        atarist_core_last_error() ? atarist_core_last_error()
		                                  : "unknown");
	}

	if (pthread_create(&g_atarist.emu_thread, NULL, emu_thread_main, NULL) != 0) {
		AtariSt_BridgeSetError("could not create emulation thread: %s",
		                       strerror(errno));
		return ATARIST_ERR;
	}
	g_atarist.emu_thread_valid = true;
	return ATARIST_OK;
}

int32_t atarist_core_stop(void)
{
	/*
	 * A SOFT stop: the media is ejected and the machine is cold reset and
	 * parked, but Hatari itself stays initialised.
	 *
	 * It cannot be a real teardown. Hatari's own main() calls Main_Init once
	 * and exits, so nothing upstream ever re-initialises it -- and a second
	 * Main_Init segfaults in STMemory_Init reading IoMem, which Main_UnInit
	 * has already released. Tearing down here would therefore make the
	 * FIRST title of a session work and every one after it fail, which is a
	 * particularly confusing shape of bug: the app looks fine until you go
	 * back to the library.
	 *
	 * Use atarist_core_shutdown() at process exit, once.
	 */
	if (!g_atarist.emu_thread_valid || !atomic_load(&g_atarist.running))
		return ATARIST_NOT_RUNNING;

	atarist_core_set_paused(0);

	g_atarist.cfg_floppy_a[0] = '\0';
	g_atarist.cfg_floppy_b[0] = '\0';
	g_atarist.cfg_gemdos[0] = '\0';
	int32_t result = mailbox_call(ATARIST_REQ_SWAP_CONFIG, 0, 0, NULL);

	/* Parked rather than left spinning: an idle machine at the library
	 * screen should cost no CPU, and on a phone that is battery the user
	 * cannot see being spent. */
	atarist_core_set_paused(1);
	return result;
}

int32_t atarist_core_shutdown(void)
{
	/* The real teardown, for process exit. After this no core can be
	 * started again in this process -- see atarist_core_stop. */
	if (!g_atarist.emu_thread_valid)
		return ATARIST_NOT_RUNNING;

	AtariSt_AudioSinkStop();

	atomic_store(&g_atarist.quit_requested, true);
	atarist_core_set_paused(0);
	mailbox_call(ATARIST_REQ_QUIT, 0, 0, NULL);

	int32_t result = ATARIST_OK;
	for (int i = 0; i < 200; i++) {
		if (!atomic_load(&g_atarist.running))
			break;
		usleep(10 * 1000);
	}
	if (atomic_load(&g_atarist.running)) {
		AtariSt_BridgeSetError("emulation thread did not exit within 2s");
		result = ATARIST_TIMEOUT;
	} else {
		pthread_join(g_atarist.emu_thread, NULL);
		g_atarist.emu_thread_valid = false;
	}
	return result;
}

int32_t atarist_core_is_running(void)
{
	return atomic_load(&g_atarist.running) ? 1 : 0;
}

void atarist_core_set_paused(int32_t paused)
{
	pthread_mutex_lock(&g_atarist.pause_lock);
	atomic_store(&g_atarist.paused, paused != 0);
	pthread_cond_broadcast(&g_atarist.pause_cond);
	pthread_mutex_unlock(&g_atarist.pause_lock);
}

int32_t atarist_core_is_paused(void)
{
	return atomic_load(&g_atarist.paused) ? 1 : 0;
}

int32_t atarist_core_reset(int32_t cold)
{
	return mailbox_call(cold ? ATARIST_REQ_RESET_COLD : ATARIST_REQ_RESET_WARM,
	                    0, 0, NULL);
}

int32_t atarist_core_set_floppy(int32_t drive, const char *path)
{
	if (drive < 0 || drive > 1)
		return ATARIST_ERR;
	return mailbox_call(ATARIST_REQ_SET_FLOPPY, drive, 0, path);
}

const char *atarist_core_get_floppy(int32_t drive)
{
	if (drive < 0 || drive > 1)
		return NULL;
	return g_atarist.floppy_path[drive][0] ? g_atarist.floppy_path[drive] : NULL;
}

int64_t atarist_core_frame_counter(void)
{
	return atomic_load(&g_atarist.frame_counter);
}

void atarist_core_key_event(int32_t st_scancode, int32_t pressed)
{
	if (st_scancode < 0 || st_scancode > 0xff)
		return;
	uint32_t written = atomic_load(&g_atarist.key_written);
	uint32_t idx = written % ATARIST_KEY_RING;
	g_atarist.key_ring[idx].scancode = (uint8_t)st_scancode;
	g_atarist.key_ring[idx].pressed = pressed ? 1 : 0;
	atomic_store(&g_atarist.key_written, written + 1);
}

void atarist_core_mouse_motion(int32_t dx, int32_t dy)
{
	/* Accumulated, not replaced: two motions inside one frame are one
	 * longer movement, and taking only the newest would make a fast drag
	 * travel a fraction of the distance. */
	atomic_fetch_add(&g_atarist.mouse_dx, dx);
	atomic_fetch_add(&g_atarist.mouse_dy, dy);
}

void atarist_core_mouse_button(int32_t button, int32_t pressed)
{
	if (button < 0 || button > 1)
		return;
	uint32_t bit = 1u << button;
	if (pressed)
		atomic_fetch_or(&g_atarist.mouse_buttons, bit);
	else
		atomic_fetch_and(&g_atarist.mouse_buttons, ~bit);
}

void atarist_core_joystick(int32_t port, int32_t mask)
{
	if (port < 0 || port > 1)
		return;
	atomic_store(&g_atarist.joy_mask[port], (uint32_t)mask);
}

int32_t atarist_core_save_state(int32_t slot)
{
	if (slot < 0 || slot >= ATARIST_SLOT_COUNT)
		return ATARIST_ERR;
	return mailbox_call(ATARIST_REQ_SAVE_STATE, 0, slot, NULL);
}

int32_t atarist_core_save_state_to(const char *path)
{
	if (!path || !*path)
		return ATARIST_ERR;
	return mailbox_call(ATARIST_REQ_SAVE_STATE, 0, 0, path);
}

int32_t atarist_core_load_state_from(const char *path)
{
	if (!path || !*path)
		return ATARIST_ERR;
	if (!file_exists(path)) {
		AtariSt_BridgeSetError("no saved state at '%s'", path);
		return ATARIST_ERR;
	}
	return mailbox_call(ATARIST_REQ_LOAD_STATE, 0, 0, path);
}

int32_t atarist_core_load_state(int32_t slot)
{
	if (slot < 0 || slot >= ATARIST_SLOT_COUNT)
		return ATARIST_ERR;
	return mailbox_call(ATARIST_REQ_LOAD_STATE, 0, slot, NULL);
}

int32_t atarist_core_state_is_empty(int32_t slot)
{
	if (slot < 0 || slot >= ATARIST_SLOT_COUNT)
		return ATARIST_ERR;
	char path[1200];
	slot_path(path, sizeof(path), slot);
	return file_exists(path) ? 0 : 1;
}

int32_t atarist_core_fps(void)
{
	return atomic_load(&g_atarist.fps);
}

int32_t atarist_core_audio_level(void)
{
	return atomic_load(&g_atarist.audio_level);
}

const char *atarist_core_status_line(void)
{
	return g_atarist.status_line[0] ? g_atarist.status_line : NULL;
}

const char *atarist_core_last_error(void)
{
	return g_atarist.last_error[0] ? g_atarist.last_error : NULL;
}

int32_t atarist_core_abi_version(void)
{
	return ATARIST_BRIDGE_ABI;
}

const char *atarist_core_hatari_version(void)
{
	return HATARI_VERSION;
}

const char *atarist_core_audio_backend(void)
{
	return AtariSt_AudioSinkName();
}
