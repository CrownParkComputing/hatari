/*
 * backend/statusbar.c - drive lights and messages, forwarded to the launcher.
 *
 * No statusbar is drawn. src/sdl/statusbar.c renders one INTO the ST
 * framebuffer, below the picture, which would mean the launcher either
 * displays Hatari's chrome inside the emulated screen or crops it back off
 * again. Instead the same information is published as text and LED state for
 * the native status bar to draw in the app's own style.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "configuration.h"
#include "statusbar.h"

#include "atarist_internal.h"

static drive_led_t floppy_led[2];
static drive_led_t hd_led;
static char message[64];

int Statusbar_GetHeightForSize(int width, int height)
{
	(void)width; (void)height;
	return 0;
}

int Statusbar_SetHeight(int width, int height)
{
	(void)width; (void)height;
	return 0;
}

int Statusbar_GetHeight(void)
{
	/* Zero, and it matters: Hatari subtracts this from the window height
	 * to find the ST picture area. A non-zero value here would leave a
	 * black band across the bottom of every frame the launcher shows. */
	return 0;
}

void Statusbar_EnableHDLed(drive_led_t state)
{
	hd_led = state;
	Statusbar_UpdateInfo();
}

void Statusbar_SetFloppyLed(drive_index_t drive, drive_led_t state)
{
	if (drive == DRIVE_LED_A)
		floppy_led[0] = state;
	else if (drive == DRIVE_LED_B)
		floppy_led[1] = state;
	Statusbar_UpdateInfo();
}

void Statusbar_InitialSetup(void)
{
	memset(floppy_led, 0, sizeof(floppy_led));
	hd_led = LED_STATE_OFF;
	message[0] = '\0';
	Statusbar_UpdateInfo();
}

void Statusbar_AddMessage(const char *msg, uint32_t msecs)
{
	/* The duration is dropped: the launcher decides how long its own
	 * status text lingers, and two competing timeouts would make messages
	 * vanish mid-read. */
	(void)msecs;
	snprintf(message, sizeof(message), "%s", msg ? msg : "");
	Statusbar_UpdateInfo();
}

void Statusbar_UpdateInfo(void)
{
	snprintf(g_atarist.status_line, sizeof(g_atarist.status_line),
	         "%s%s%s%s%s",
	         floppy_led[0] != LED_STATE_OFF ? "A " : "",
	         floppy_led[1] != LED_STATE_OFF ? "B " : "",
	         hd_led != LED_STATE_OFF ? "HD " : "",
	         message[0] ? "| " : "",
	         message);
}
