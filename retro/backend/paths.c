/* Embedded Hatari paths: all writable defaults belong to the active title.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "paths.h"
#include "atarist_internal.h"

/* The host supplies absolute, writable directories and retains ownership of
 * them. Never change the process working directory or HOME environment. */
void Paths_Init(const char *argv0) { (void)argv0; }
void Paths_UnInit(void) {}
const char *Paths_GetWorkingDir(void) { return g_atarist.work_dir; }
const char *Paths_GetDataDir(void) { return g_atarist.tos_dir; }
const char *Paths_GetUserHome(void) { return g_atarist.work_dir; }
const char *Paths_GetHatariHome(void) { return g_atarist.work_dir; }
const char *Paths_GetDefaultScreenShotDir(void) { return g_atarist.work_dir; }
