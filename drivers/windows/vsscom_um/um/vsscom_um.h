//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What only the user-mode driver has: the owner of a port that the helper made.
//

#pragma once

#include "vsscom.h"

// The owner property, and the rest of what the helper and the driver agree on.
#include <vsscom_helper/protocol.h>

// The owner of a port, as a SID.
typedef struct _VSSCOM_UM_OWNER
{
    ULONG Size; // the bytes of the SID in Sid
    BYTE Sid[SECURITY_MAX_SID_SIZE];
} VSSCOM_UM_OWNER, *PVSSCOM_UM_OWNER;

// True when the device being added has an owner: the helper made it. A port made
// by hand has none.
BOOLEAN VssComUmDeviceInitHasOwner(_In_ PWDFDEVICE_INIT DeviceInit);
