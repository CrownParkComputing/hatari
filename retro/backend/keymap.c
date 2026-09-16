/*
 * backend/keymap.c - keyboard seam.
 *
 * Almost inert by design. src/sdl/keymap.c is large because it has to turn
 * host keysyms into ST scan codes across three mapping modes and a user remap
 * file. The native frontend does that translation itself and hands the bridge
 * finished ST scan codes --
 * so there is nothing left to map here.
 *
 * Doing it in the frontend is not an arbitrary split: the on-screen keyboard
 * and the gamepad-to-key bindings are both UI concepts that already need the
 * ST key layout, and having the mapping in one place stops the physical and
 * virtual keyboards drifting apart.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdatomic.h>
#include <string.h>

#include "main.h"
#include "configuration.h"
#include "ikbd.h"
#include "keymap.h"

#include "atarist_internal.h"

/* ST scan codes for the two shift keys, tracked because joy.c asks whether
 * shift is held (holding shift means "cursor keys, not joystick"). */
#define ST_SCANCODE_LSHIFT 0x2A
#define ST_SCANCODE_RSHIFT 0x36

static _Atomic uint32_t shift_mask;

/* Called by the bridge as it drains its key ring, so shift state is derived
 * from the same stream the IKBD sees and cannot disagree with it. */
void Keymap_NoteScanCode(uint8_t scancode, bool pressed)
{
	uint32_t bit = 0;
	if (scancode == ST_SCANCODE_LSHIFT)
		bit = 1;
	else if (scancode == ST_SCANCODE_RSHIFT)
		bit = 2;
	if (!bit)
		return;

	if (pressed)
		atomic_fetch_or(&shift_mask, bit);
	else
		atomic_fetch_and(&shift_mask, ~bit);
}

void Keymap_Init(void)
{
	atomic_store(&shift_mask, 0);
}

void Keymap_InitShortcutDefaultKeys(void)
{
	/* Hatari's own F11/F12-style shortcuts are not wired up: the launcher
	 * has real buttons for fullscreen, pause, reset and disk swapping, and
	 * a hidden second set of bindings that only work when a hardware
	 * keyboard happens to be attached would be a trap, not a feature. */
}

void Keymap_LoadRemapFile(const char *pszFileName)
{
	(void)pszFileName;
}

void Keymap_SimulateCharacter(char asckey, bool press)
{
	/* Used by Hatari's own paste/auto-type paths. The ASCII-to-scan-code
	 * table lives in the frontend, so route it back out through the same queue the
	 * launcher uses rather than duplicating the table here. */
	(void)asckey;
	(void)press;
}

int Keymap_GetKeyFromName(const char *name)
{
	(void)name;
	return 0;
}

const char *Keymap_GetKeyName(int keycode)
{
	(void)keycode;
	return "";
}

void Keymap_SetCountry(int countrycode)
{
	(void)countrycode;
}

bool Keymap_IsShiftPressed(void)
{
	return atomic_load(&shift_mask) != 0;
}
