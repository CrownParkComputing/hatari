/*
 * atarist_bridge.h - Plain-C-ABI header for the embedded Hatari core.
 *
 * No SDL types, no C++, no JNI, no Atari-side structs leak across this line:
 * everything here is int32_t, const char* and one raw pixel pointer. This is
 * the complete public surface used by the native C++ frontend.
 *
 * The same header is used by every target -- Linux/Windows/macOS load the
 * resulting shared library by path, Android by bare name, and iOS links the
 * same sources statically into the application.
 *
 * Threading model
 * ---------------
 * Hatari's emulation is a blocking loop (M68000_Start), so atarist_core_start
 * spawns it on its own thread and returns immediately. Everything else on this
 * header is callable from the UI thread, and is either
 *   - lock-free and atomic (input, pause, status getters), or
 *   - marshalled to the emulation thread through the bridge mailbox and
 *     awaited (reset, media swap, snapshots).
 * Nothing here may be called from a signal handler.
 *
 * This file is distributed under the GNU General Public License, version 2 or
 * (at your option) any later version, because it is linked into Hatari.
 */
#ifndef RETRO_ATARIST_BRIDGE_H
#define RETRO_ATARIST_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The library is built with -fvisibility=hidden so that Hatari's several
 * thousand internal symbols -- including names as generic as `main` and
 * `select` -- cannot collide with another dependency inside
 * the single symbol namespace an Android process gives them all. That means
 * OUR entry points have to be re-exported explicitly; without this push,
 * dlsym finds nothing and every binding fails with "symbol not found".
 */
#if defined(_WIN32)
#  define ATARIST_API __declspec(dllexport)
#else
#  define ATARIST_API
#  pragma GCC visibility push(default)
#endif

/* ---------------------------------------------------------------- results */

#define ATARIST_OK              0
#define ATARIST_ERR            -1

/* The emulation thread did not service a mailbox request in time. */
#define ATARIST_TIMEOUT        -2

/* No core is running, so the request has nothing to act on. */
#define ATARIST_NOT_RUNNING    -3

/* A core is already up. Stop it before starting another. */
#define ATARIST_ALREADY_STARTED -4

/* No usable TOS image. Hatari refuses to boot without one and the built-in
 * EmuTOS fallback was not bundled -- this is the single most common launch
 * failure, so it gets its own code rather than folding into ATARIST_ERR. */
#define ATARIST_NO_TOS         -5

/* ---------------------------------------------------------------- machines */

/* Mirrors Hatari's MACHINETYPE (configuration.h). Kept as our own #defines so
 * the frontend never has to parse a Hatari header, and so a reordering
 * upstream is caught here by the static asserts in atarist_bridge.c rather
 * than silently booting the wrong machine. */
#define ATARIST_MACHINE_ST       0
#define ATARIST_MACHINE_MEGA_ST  1
#define ATARIST_MACHINE_STE      2
#define ATARIST_MACHINE_MEGA_STE 3
#define ATARIST_MACHINE_TT       4
#define ATARIST_MACHINE_FALCON   5

/* Monitor types, mirroring Hatari's MONITOR_TYPE_*. RGB is the colour monitor
 * nearly every ST game expects; TV is the same picture through the modulator.
 * MONO is the high-resolution monochrome monitor -- correct for desktop
 * applications, wrong for almost every game. */
#define ATARIST_MONITOR_MONO 0
#define ATARIST_MONITOR_RGB  1
#define ATARIST_MONITOR_VGA  2
#define ATARIST_MONITOR_TV   3

/* Floppy drives. Hatari's MAX_FLOPPYDRIVES is 2. */
#define ATARIST_DRIVE_A 0
#define ATARIST_DRIVE_B 1

/* Emulated ST joystick bits, matching Hatari's ATARIJOY_BITMASK_*. */
#define ATARIST_JOY_UP    0x01
#define ATARIST_JOY_DOWN  0x02
#define ATARIST_JOY_LEFT  0x04
#define ATARIST_JOY_RIGHT 0x08
#define ATARIST_JOY_FIRE  0x80

/* Snapshot slots. Hatari snapshots are path-based; the bridge layers slots on
 * top so the native save-state UI matches the rest of the Retro-* family. */
#define ATARIST_SLOT_COUNT 10

/* ------------------------------------------------------------------ config */

/*
 * The machine to boot, as the launcher describes it.
 *
 * Deliberately a flat struct of scalars and paths rather than a mirror of
 * Hatari's CNF_PARAMS: CNF_PARAMS is ~40 nested structs that change shape
 * between Hatari releases, and pinning our ABI to it would mean re-generating
 * the frontend ABI on every vendor bump. The bridge translates this into
 * Hatari command-line arguments (which are a stable, documented interface)
 * before calling Main_Init.
 *
 * Every path may be NULL, meaning "not fitted". A NULL tos_path is only valid
 * when the build bundles EmuTOS.
 */
typedef struct {
	/* One of ATARIST_MACHINE_*. */
	int32_t machine;

	/* ST RAM in kilobytes: 512, 1024, 2048, 4096, 8192, 14336. 0 = Hatari's
	 * default for the chosen machine. */
	int32_t memory_kb;

	/* TOS ROM image. Copyrighted by Atari and never bundled -- the user
	 * supplies it, exactly as with the C64 KERNAL in Retro-C64. */
	const char *tos_path;

	/* Floppy images (.st, .msa, .dim, .stx, .ipf, or a .zip containing one).
	 * NULL leaves the drive empty. */
	const char *floppy_a;
	const char *floppy_b;

	/* A host directory mapped as an ST hard disk through GEMDOS emulation.
	 * This is how most modern ST software is run: no image file, just a
	 * folder. NULL disables it. */
	const char *gemdos_dir;

	/* Raw ACSI / IDE disk images, for titles that need a real partitioned
	 * disk rather than GEMDOS emulation. */
	const char *acsi_image;
	const char *ide_image;

	/* 0 off, 1 on. The Blitter is Mega ST / STE hardware; enabling it on a
	 * plain ST is a Hatari extension some demos want. */
	int32_t blitter;

	/* 0 = fast (Hatari's default, boots in seconds), 1 = accurate FDC
	 * timing (needed by protected originals, much slower to load). */
	int32_t accurate_floppy;

	/* Audio sample rate. 0 = 44100. */
	int32_t sample_rate;

	/* 0 mono (ST), 1 stereo (STE DMA sound). */
	int32_t stereo;

	/* Monitor fitted, one of ATARIST_MONITOR_*.
	 *
	 * Not cosmetic and not a preference: TOS reads the monitor type at boot
	 * and chooses its screen mode from it. A high-resolution mono monitor
	 * puts the machine in ST-HIGH (640x400, two colours), where essentially
	 * no game runs -- so the default has to be a colour monitor. */
	int32_t monitor;

	/* Joystick fitted to ST port 1 (the port games use). 0 disabled,
	 * 1 driven by atarist_core_joystick. */
	int32_t joystick_port1;

	/* Where Hatari may write: its own hatari.cfg, NVRAM, printer output and
	 * snapshot files. Must exist and be writable. */
	const char *work_dir;
} AtariStConfig;

/* ------------------------------------------------------------------- lifecycle */

/*
 * One-time process init. [work_dir] is where the bridge keeps Hatari's config
 * and NVRAM; [tos_dir] is scanned for bundled EmuTOS. Safe to call more than
 * once; only the first call does anything.
 */
void atarist_core_init(const char *work_dir, const char *tos_dir);

/*
 * Boot the machine described by [cfg] on a new emulation thread.
 *
 * Asynchronous: the ST is still executing TOS's startup when this returns, so
 * the caller must not expect a framebuffer immediately. Returns ATARIST_OK,
 * ATARIST_ALREADY_STARTED, or ATARIST_NO_TOS.
 */
int32_t atarist_core_start(const AtariStConfig *cfg);
/*
 * Note: if a core is ALREADY running, this re-points it at the new
 * configuration and cold resets, rather than failing. "Start this title"
 * means the same thing to the launcher whether or not something else was
 * running, and a second Main_Init is not possible (see atarist_core_stop).
 */

/*
 * Stop the running title: eject its media, cold reset, and park the machine.
 *
 * This is a SOFT stop -- Hatari stays initialised. It has to be, because
 * Hatari cannot be initialised twice in one process: its own main() calls
 * Main_Init once and exits, and a second Main_Init segfaults in STMemory_Init
 * reading an IoMem that Main_UnInit has already released. A launcher that
 * tore down here would work for the first title of a session and fail for
 * every one after it.
 *
 * Returns ATARIST_NOT_RUNNING if nothing is running.
 */
int32_t atarist_core_stop(void);

/*
 * The real teardown, for process exit. Call at most ONCE: no core can be
 * started again afterwards.
 */
int32_t atarist_core_shutdown(void);

int32_t atarist_core_is_running(void);

void atarist_core_set_paused(int32_t paused);
int32_t atarist_core_is_paused(void);

/* Cold reset (power cycle) / warm reset (the ST's reset button). */
int32_t atarist_core_reset(int32_t cold);

/* --------------------------------------------------------------------- media */

/*
 * Insert or eject a floppy while the machine is running -- exactly what a
 * multi-disk game's "insert disk 2" prompt needs. [path] NULL ejects.
 * [drive] is ATARIST_DRIVE_A or _B. Marshalled to the emulation thread.
 */
int32_t atarist_core_set_floppy(int32_t drive, const char *path);

/* Path of the image currently in [drive], or NULL when empty. The returned
 * pointer is owned by the bridge and valid until the next call. */
const char *atarist_core_get_floppy(int32_t drive);

/* ---------------------------------------------------------------------- video */

/*
 * Pointer to Hatari's current 32-bit framebuffer, or NULL before the first
 * frame. Owned by the core; do not free, and do not hold across a call to
 * atarist_core_stop.
 *
 * [out_pitch] is in BYTES, not pixels: ST modes are 320/640 wide but Hatari's
 * surface is padded, and treating pitch as a pixel count is what produces the
 * classic diagonally-sheared emulator screenshot.
 */
const uint32_t *atarist_core_get_framebuffer(int32_t *out_width,
                                             int32_t *out_height,
                                             int32_t *out_pitch);

/*
 * Bumped once per completed frame. The UI compares this against the last
 * value it drew and skips the texture upload when nothing changed -- a GEM
 * desktop sitting idle is byte-identical for minutes at a time.
 */
int64_t atarist_core_frame_counter(void);

/* Display aspect ratio for the current mode, or 0 when unknown. Not
 * width/height: ST low resolution is 320x200 pixels on a 4:3 monitor. */
double atarist_core_pixel_aspect(void);

/* --------------------------------------------------------------------- input */

/*
 * Keyboard by ATARI ST scan code, not by
 * host keycode and not by character: ST games read the IKBD's make/break
 * codes directly, so anything higher-level loses the key-up half and leaves
 * a game running with a direction held down forever.
 */
void atarist_core_key_event(int32_t st_scancode, int32_t pressed);

/* Relative mouse motion, in ST pixels. */
void atarist_core_mouse_motion(int32_t dx, int32_t dy);

/* button: 0 left, 1 right. */
void atarist_core_mouse_button(int32_t button, int32_t pressed);

/*
 * ST joystick state for [port] (0 or 1) as a mask of ATARIST_JOY_*.
 *
 * Requires the Joy_SetHostState patch (patches/0001-joy-host-state.patch);
 * without it this call is a no-op and the joystick reads as centred. See
 * docs/NATIVE_BUILD.md.
 */
void atarist_core_joystick(int32_t port, int32_t mask);

/* ---------------------------------------------------------------- save states */

/*
 * Hatari's own machine snapshots (MemorySnapShot_Capture/_Restore), keyed by
 * slot within the currently running title's directory.
 *
 * Synchronous to the caller but executed on the emulation thread through the
 * mailbox -- ST machine state must never be touched from another thread.
 * Returns ATARIST_TIMEOUT if the core does not service the request.
 */
int32_t atarist_core_save_state(int32_t slot);
int32_t atarist_core_load_state(int32_t slot);

/*
 * The same thing, to a caller-chosen file.
 *
 * Slots are a single global set, which is wrong for per-title auto-saves:
 * closing one game would overwrite the position of the last one. These let
 * the launcher keep a state file per title instead.
 */
int32_t atarist_core_save_state_to(const char *path);
int32_t atarist_core_load_state_from(const char *path);

/* 1 empty, 0 occupied, <0 on error (including "no core running"). */
int32_t atarist_core_state_is_empty(int32_t slot);

/* -------------------------------------------------------------------- status */

int32_t atarist_core_fps(void);

/* Smoothed 0..100 output peak, for the level meter in the status bar. */
int32_t atarist_core_audio_level(void);

/* Hatari's statusbar text (drive lights, FPS), or NULL. Bridge-owned. */
const char *atarist_core_status_line(void);

/* Last error the bridge or Hatari reported, or NULL. Bridge-owned, valid
 * until the next bridge call that can fail. */
const char *atarist_core_last_error(void);

/* Version of THIS bridge ABI, not of Hatari. The frontend refuses to use a
 * library whose value it does not know, which is what stops a stale .so from
 * a previous build being silently loaded against new bindings. */
#define ATARIST_BRIDGE_ABI 1
int32_t atarist_core_abi_version(void);

/* Hatari's own version string, e.g. "2.6.0". Bridge-owned. */
const char *atarist_core_hatari_version(void);

/* Which audio backend was compiled in and opened: "SDL3", "SDL2", or "none".
 * Reported on the About screen so silence is diagnosable -- "no sound" and
 * "no sink on this platform" look identical from the speakers. */
const char *atarist_core_audio_backend(void);

#if !defined(_WIN32)
#  pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* RETRO_ATARIST_BRIDGE_H */
