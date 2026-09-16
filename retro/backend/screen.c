/*
 * backend/screen.c - Hatari's screen seam, rendering into memory.
 *
 * Modelled on src/retro/screen.c, with one deliberate difference: there is no
 * push callback. libretro's host calls retro_run and expects video_refresh_cb
 * inside it; our host is a Metal UI on another thread that samples at its
 * own refresh rate. So the buffer stays put and the launcher reads it, which
 * also means an idle GEM desktop costs nothing at all -- the frame counter
 * does not move and the UI never uploads a texture.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

const char Screen_fileid[] = "Retro-AtariST backend/screen.c";

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "configuration.h"
#include "conv_st.h"
#include "log.h"
#include "screen.h"
#include "statusbar.h"
#include "video.h"

#include "atarist_bridge.h"
#include "atarist_internal.h"

/* Referenced by Hatari's shortcut handling, so they have to exist even though
 * a launcher-owned window makes both meaningless here. */
bool bGrabMouse = false;
bool bInFullScreen = false;

/*
 * The framebuffer is allocated once at the largest size any ST/STE mode can
 * need and never reallocated, unlike src/retro/screen.c which frees and
 * mallocs on every mode change.
 *
 * That matters because the launcher reads this pointer from another thread:
 * a realloc during a resolution change -- which is exactly what a game does on
 * its title screen -- would hand the UI thread a dangling pointer for one
 * frame. A fixed allocation makes the race impossible rather than unlikely.
 */
#define ATARIST_MAX_WIDTH  832   /* 640 + both overscan borders */
#define ATARIST_MAX_HEIGHT 576   /* 400 + both overscan borders */

static uint32_t framebuffer[ATARIST_MAX_WIDTH * ATARIST_MAX_HEIGHT];

/* Written by the emulation thread, read by the UI thread. Atomic so a mode
 * change cannot be observed half-applied (new width, old height), which would
 * make the launcher read past the end of the valid area. */
static _Atomic int32_t screen_width = 320;
static _Atomic int32_t screen_height = 200;

void Screen_GetPixelFormat(uint32_t *rmask, uint32_t *gmask, uint32_t *bmask,
                           int *rshift, int *gshift, int *bshift)
{
	/* Opaque XRGB8888. In little-endian memory this is BGRA8, which both
	 * mobile shells upload directly; the high byte must be 0xff or ImGui's
	 * texture blend makes the otherwise valid framebuffer transparent. */
	if (rmask) *rmask = 0x00FF0000;
	if (gmask) *gmask = 0x0000FF00;
	if (bmask) *bmask = 0x000000FF;
	if (rshift) *rshift = 16;
	if (gshift) *gshift = 8;
	if (bshift) *bshift = 0;
}

uint32_t Screen_MapRGB(uint8_t red, uint8_t green, uint8_t blue)
{
	return 0xff000000u | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

void Screen_GetDimension(uint32_t **pixels, int *width, int *height, int *pitch)
{
	int w = atomic_load(&screen_width);
	if (pixels) *pixels = framebuffer;
	if (width) *width = w;
	if (height) *height = atomic_load(&screen_height);
	/* Pitch is the FIXED row stride, not w*4: the buffer is allocated at
	 * ATARIST_MAX_WIDTH and a narrow mode occupies the left of each row.
	 * Returning w*4 here would make Hatari's converters pack rows tightly
	 * into a buffer the launcher then reads at the wide stride, which is
	 * the classic diagonally-sheared emulator screenshot. */
	if (pitch) *pitch = ATARIST_MAX_WIDTH * 4;
}

int Screen_GetUISocket(void)
{
	return 0;
}

void Screen_GetDesktopSize(int *width, int *height)
{
	/* The launcher scales whatever we produce, so this only has to be big
	 * enough that Hatari never decides a mode "does not fit". */
	if (width) *width = ATARIST_MAX_WIDTH;
	if (height) *height = ATARIST_MAX_HEIGHT;
}

bool Screen_SetVideoSize(int width, int height, bool bForceChange)
{
	if (width > ATARIST_MAX_WIDTH || height > ATARIST_MAX_HEIGHT) {
		/* Clamped rather than refused. A refusal leaves Hatari
		 * convinced the mode was set and drawing at the size it asked
		 * for, which overruns the buffer; clamping loses the outermost
		 * border pixels of an extreme overscan demo and nothing else. */
		Log_Printf(LOG_WARN,
		           "Retro-AtariST: clamping %dx%d to %dx%d\n",
		           width, height, ATARIST_MAX_WIDTH, ATARIST_MAX_HEIGHT);
		if (width > ATARIST_MAX_WIDTH) width = ATARIST_MAX_WIDTH;
		if (height > ATARIST_MAX_HEIGHT) height = ATARIST_MAX_HEIGHT;
	}

	if (width == atomic_load(&screen_width) &&
	    height == atomic_load(&screen_height) && !bForceChange)
		return false;

	/* Clear before publishing the new size: the tail of the old mode's
	 * image is still in the buffer, and a taller new mode would show it as
	 * a band of garbage under the picture for one frame. */
	memset(framebuffer, 0, sizeof(framebuffer));
	atomic_store(&screen_width, width);
	atomic_store(&screen_height, height);
	return true;
}

void Screen_ModeChanged(bool bForceChange)
{
	ConvST_ChangeResolution(bForceChange);
}

void Screen_SetTitle(const char *title)
{
	(void)title;
}

void Screen_Init(void)
{
	/* Scaling, overscan cropping and frameskip all belong to the launcher,
	 * which knows the real window size and the platform's refresh rate.
	 * Hatari doing any of it here would be work thrown away. */
	ConfigureParams.Screen.nZoomFactor = 1.0;
	ConfigureParams.Screen.nMaxWidth = ATARIST_MAX_WIDTH;
	ConfigureParams.Screen.nMaxHeight = ATARIST_MAX_HEIGHT;
	ConfigureParams.Screen.nFrameSkips = 0;

	memset(framebuffer, 0, sizeof(framebuffer));
}

void Screen_UnInit(void)
{
}

void Screen_ClearScreen(void)
{
	memset(framebuffer, 0, sizeof(framebuffer));
}

void Screen_EnterFullScreen(void) { }
void Screen_ReturnFromFullScreen(void) { }
bool Screen_UngrabMouse(void) { return false; }
void Screen_GrabMouseIfNecessary(void) { }
bool Screen_Lock(void) { return true; }
void Screen_UnLock(void) { }

bool Screen_Draw(bool bForceFlip)
{
	bool changed = ConvST_DrawFrame();

	if (changed || bForceFlip) {
		AtariSt_BridgePublishFrame(framebuffer,
		                           atomic_load(&screen_width),
		                           atomic_load(&screen_height),
		                           ATARIST_MAX_WIDTH * 4);
	}
	return changed;
}

void Screen_GenConvUpdate(bool update_statusbar)
{
	(void)update_statusbar;
	AtariSt_BridgePublishFrame(framebuffer,
	                           atomic_load(&screen_width),
	                           atomic_load(&screen_height),
	                           ATARIST_MAX_WIDTH * 4);
}

uint32_t Screen_GetGenConvWidth(void)
{
	return (uint32_t)atomic_load(&screen_width);
}

uint32_t Screen_GetGenConvHeight(void)
{
	return (uint32_t)atomic_load(&screen_height);
}

int Screen_SaveBMP(const char *filename)
{
	/* Screenshots are the launcher's job: it already has the frame, and it
	 * can write a PNG into the platform's pictures directory with the
	 * permissions to match. */
	(void)filename;
	return -1;
}

void Screen_StatusbarMessage(const char *msg, uint32_t msecs)
{
	Statusbar_AddMessage(msg, msecs);
}

void Screen_MinimizeWindow(void) { }

uint32_t Screen_GetMouseState(int *mx, int *my)
{
	if (mx) *mx = 0;
	if (my) *my = 0;
	return 0;
}

bool Screen_ShowCursor(bool show)
{
	(void)show;
	return false;
}

/* ========================= public C ABI video API ========================= */

const uint32_t *atarist_core_get_framebuffer(int32_t *out_width,
                                             int32_t *out_height,
                                             int32_t *out_pitch)
{
	/* Before the first frame the buffer is all zeroes, which would show as
	 * a black screen indistinguishable from a title that has not drawn
	 * yet. NULL lets the launcher say "booting" instead. */
	if (atomic_load(&g_atarist.frame_counter) == 0)
		return NULL;

	if (out_width) *out_width = atomic_load(&screen_width);
	if (out_height) *out_height = atomic_load(&screen_height);
	if (out_pitch) *out_pitch = ATARIST_MAX_WIDTH * 4;
	return framebuffer;
}

double atarist_core_pixel_aspect(void)
{
	int w = atomic_load(&screen_width);
	int h = atomic_load(&screen_height);
	if (w <= 0 || h <= 0)
		return 0.0;

	/*
	 * Every ST display mode was shown on a 4:3 monitor, so the pixels are
	 * not square and width/height is the wrong answer:
	 *
	 *   320x200 low res    - pixels twice as wide as tall
	 *   640x200 medium res - pixels four times as wide as tall
	 *   640x400 mono       - square pixels
	 *
	 * Returning the DISPLAY aspect and letting the launcher stretch to it
	 * is what keeps a circle round in all three, which naive
	 * width-over-height does not.
	 */
	return 4.0 / 3.0;
}
