/*
 * backend/joy_ui.c - joystick seam.
 *
 * The launcher's joystick mask is presented to Hatari as a "real" joystick.
 * That is the mode with the fewest surprises: JOYSTICK_KEYBOARD makes joy.c
 * ignore the stick whenever shift is held, and JOYSTICK_DISABLED makes it
 * return centred regardless of what we set.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdatomic.h>

#include "main.h"
#include "configuration.h"
#include "joy.h"
#include "joy_ui.h"

#include "atarist_bridge.h"
#include "atarist_internal.h"

const char *JoyUI_GetName(int id)
{
	(void)id;
	return "Retro-AtariST virtual joystick";
}

int JoyUI_GetMaxId(void)
{
	return 1;
}

int JoyUI_NumJoysticks(void)
{
	/* Always two, whatever the host has plugged in: these are virtual
	 * sticks fed by the launcher, which may be driving them from a
	 * gamepad, the on-screen stick, or the keyboard. */
	return 2;
}

bool JoyUI_ValidateJoyId(int i)
{
	return i >= 0 && i <= 1;
}

void JoyUI_Init(void) { }
void JoyUI_UnInit(void) { }

void JoyUI_SetDefaultKeys(int id)
{
	if (id >= 0 && id < JOYSTICK_COUNT)
		ConfigureParams.Joysticks.Joy[id].nJoystickMode = JOYSTICK_REALSTICK;
}

bool JoyUI_ReadJoystick(int id, JOYREADING *joyread)
{
	if (id < 0 || id > 1 || !joyread)
		return false;

	uint32_t mask = atomic_load(&g_atarist.joy_mask[id]);

	/*
	 * Hatari wants analogue axis values and re-derives the digital
	 * directions from them against JOYRANGE_*. Feeding it the extremes is
	 * what makes a digital ST stick read as fully deflected; anything less
	 * than JOYRANGE_UP_VALUE/-16384 reads as centred and the game sees no
	 * input at all.
	 */
	joyread->XPos = 0;
	joyread->YPos = 0;
	if (mask & ATARIST_JOY_LEFT)  joyread->XPos = -32768;
	if (mask & ATARIST_JOY_RIGHT) joyread->XPos = 32767;
	if (mask & ATARIST_JOY_UP)    joyread->YPos = -32768;
	if (mask & ATARIST_JOY_DOWN)  joyread->YPos = 32767;

	joyread->Buttons = (mask & ATARIST_JOY_FIRE) ? JOYREADING_BUTTON1 : 0;
	return true;
}

int JoyUI_GetRealFireButtons(int nStJoyId)
{
	/* One button. The ST's DE-9 port only ever had one, and reporting more
	 * makes Hatari offer autofire/jump mappings for buttons that no ST
	 * game can read. */
	(void)nStJoyId;
	return 1;
}
