/*
 * backend/gui_event.c - the launcher's pointer state, pushed into the IKBD.
 *
 * Called from ikbd.c on the emulation thread. Mirrors src/retro/gui_event.c,
 * reading the bridge's atomics instead of libretro's input callback.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdatomic.h>

#include "main.h"
/* Before video.h: its prototypes use MACHINETYPE and VIDEOTIMINGMODE, which
 * are declared in configuration.h and which video.h does not include itself. */
#include "configuration.h"
#include "gui_event.h"
#include "ikbd.h"
#include "screen.h"
#include "video.h"

#include "atarist_internal.h"

void GuiEvent_WarpMouse(int x, int y, bool restore)
{
	/* Nothing to warp: the launcher owns the host pointer, and on a touch
	 * device there is no host pointer at all. */
	(void)x; (void)y; (void)restore;
}

static void handle_mouse_motion(void)
{
	/* Ignored for the first few frames after a reset. TOS -- 4.04 in
	 * particular -- misreads IKBD packets that arrive while it is still
	 * initialising the keyboard, and the audible symptom is a burst of key
	 * clicks on every boot. Same guard as src/retro/gui_event.c. */
	if (nVBLs < 10) {
		atomic_store(&g_atarist.mouse_dx, 0);
		atomic_store(&g_atarist.mouse_dy, 0);
		return;
	}

	/* Exchanged, not read-then-cleared: motion accumulated between the
	 * read and the clear would otherwise be silently dropped, which on a
	 * fast drag is a visibly short pointer movement. */
	int dx = atomic_exchange(&g_atarist.mouse_dx, 0);
	int dy = atomic_exchange(&g_atarist.mouse_dy, 0);

	KeyboardProcessor.Mouse.dx += dx;
	KeyboardProcessor.Mouse.dy += dy;
}

static void handle_mouse_buttons(void)
{
	uint32_t buttons = atomic_load(&g_atarist.mouse_buttons);

	if (buttons & 0x1) {
		if (Keyboard.LButtonDblClk == 0)
			Keyboard.bLButtonDown |= BUTTON_MOUSE;
	} else {
		Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
	}

	if (buttons & 0x2)
		Keyboard.bRButtonDown |= BUTTON_MOUSE;
	else
		Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
}

void GuiEvent_EventHandler(void)
{
	handle_mouse_motion();
	handle_mouse_buttons();
}
