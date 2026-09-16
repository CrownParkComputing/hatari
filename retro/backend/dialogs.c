/*
 * backend/dialogs.c - the process-level and dialog entry points a Hatari
 * front end must supply.
 *
 * These live in the front end, not the core, because each one is a question
 * about the host: how does this program exit, and how does it ask the user
 * something? For src/sdl/ the answers are exit(3) and a modal SDL dialog. For
 * a launcher they are "unwind the emulation thread" and "you cannot".
 *
 * The second half of that is the important one. Hatari calls DlgAlert_Query
 * from deep inside the emulation thread -- from the FDC when an image is
 * unreadable, from memory setup when a TOS image does not match the machine.
 * Blocking there to show something is not an option: the UI thread would have
 * to render a dialog while the thread holding the answer is the one that is
 * blocked. So every query declines and every notice is recorded for
 * atarist_core_last_error, which the launcher surfaces in its own error banner
 * once the failed operation has returned.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdatomic.h>

#include "main.h"
#include "dialog.h"
#include "log.h"
#include "m68000.h"

#include "atarist_internal.h"

void Main_RequestQuit(int exitval)
{
	/* Reached when the emulated machine asks to be switched off -- TOS's
	 * own shutdown, or a "quit" shortcut. Treated exactly like a stop
	 * request from the launcher, so the thread unwinds through
	 * M68000_Start's return and Main_UnInit rather than calling exit()
	 * and taking the whole application process with it. */
	Main_SetQuitValue(exitval);
}

void Main_SetQuitValue(int exitval)
{
	(void)exitval;
	bQuitProgram = true;
	atomic_store(&g_atarist.quit_requested, true);
	M68000_SetSpecial(SPCFLAG_BRK);
}

void Main_ErrorExit(const char *msg1, const char *msg2, int errval)
{
	if (msg1) {
		if (msg2)
			AtariSt_BridgeSetError("%s - %s", msg1, msg2);
		else
			AtariSt_BridgeSetError("%s", msg1);
		Log_Printf(LOG_ERROR, "%s%s%s\n", msg1, msg2 ? " - " : "",
		           msg2 ? msg2 : "");
	}
	/* Deliberately NOT exit(): in a shared library that would kill the
	 * host app. The launcher sees the core stop with last_error set. */
	Main_SetQuitValue(errval);
}

bool DlgAlert_Query(const char *text)
{
	/* false = "no"/"cancel". Declining is the safe answer for every query
	 * Hatari asks: they are all of the form "this looks wrong, carry on
	 * anyway?" and carrying on unattended is how a bad image ends up
	 * written back over a good one. */
	AtariSt_BridgeSetError("%s", text ? text : "(unknown Hatari query)");
	Log_Printf(LOG_WARN, "Hatari query declined: %s\n", text ? text : "?");
	return false;
}

bool DlgAlert_Notice(const char *text)
{
	AtariSt_BridgeSetError("%s", text ? text : "(unknown Hatari notice)");
	Log_Printf(LOG_WARN, "Hatari notice: %s\n", text ? text : "?");
	return false;
}

void Dialog_HaltDlg(void)
{
	/* Shown when the CPU halts (a double bus fault, usually a bad TOS for
	 * the selected machine). Reported and then treated as fatal for this
	 * session -- there is nothing the user can do from here that a reset
	 * from the launcher would not do better. */
	AtariSt_BridgeSetError(
		"the emulated CPU halted -- usually a TOS image that does not "
		"match the selected machine");
	Main_SetQuitValue(1);
}

int Dialog_MainDlg(bool *bReset, bool *bLoadedSnapshot)
{
	/* Hatari's own setup GUI. The launcher IS the setup GUI. */
	if (bReset) *bReset = false;
	if (bLoadedSnapshot) *bLoadedSnapshot = false;
	return 0;
}

char *DlgFloppy_ShortCutSel(const char *path_and_name, char **zip_path)
{
	/* "Pick a disk image" during emulation. The launcher does this with a
	 * real platform file picker and then calls atarist_core_set_floppy. */
	(void)path_and_name;
	if (zip_path) *zip_path = NULL;
	return NULL;
}
