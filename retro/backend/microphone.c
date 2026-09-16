/*
 * backend/microphone.c - Falcon DSP microphone input.
 *
 * Stubbed. It is Falcon-only, and capturing audio would put a microphone
 * permission on the Android and iOS manifests of a launcher that otherwise
 * needs none -- a real cost to every user for a feature no ST title can use.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "main.h"
#include "microphone.h"

bool Microphone_Start(int sampleRate)
{
	(void)sampleRate;
	return false;
}

void Microphone_Stop(void)
{
}
