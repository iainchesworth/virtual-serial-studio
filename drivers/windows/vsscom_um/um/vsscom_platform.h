//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the user-mode driver includes and what it supplies to the code that the
// kernel-mode and the user-mode driver share (see vsscom/vsscom.h).
//

#pragma once

// The status codes come from ntstatus.h and not from windows.h.
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winioctl.h>
#include <wdf.h>

// The time in 100 ns units since the system started, for the write deadline.
// Time spent asleep does not count, which is what a timeout wants.
static FORCEINLINE ULONGLONG VssComInterruptTime(VOID)
{
    ULONGLONG now = 0;
    (void)QueryUnbiasedInterruptTime(&now);
    return now;
}

// winioctl.h of the Windows SDK and ntddser.h both define the SERIAL_LSRMST_*
// and SERIAL_IOC_* macros, with the same values and different spelling. The
// serial control codes and structures come from ntddser.h (vsscom.h), so the
// repeat is harmless.
#pragma warning(disable : 4005)
