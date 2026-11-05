//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Who may open the hub end, for the user-mode driver.
//
// The kernel-mode driver reads the user from the token of the create request
// (vsscom/access.c). A user-mode driver learns the requestor only through the
// framework: WdfRequestImpersonate runs a callback with the requestor's token on
// the thread, and the callback reads it there. The rules are the same ones:
//
//   - a port made on demand: only the user who made it. The user-mode driver
//     makes no port on demand yet, so this case does not arise;
//   - a port made by hand: administrators.
//
// The token is at identification level, which is enough to ask which groups the
// user is in, and no more than that.
//

#include "vsscom.h"

typedef struct _VSSCOM_IMPERSONATION
{
    PVSSCOM_DEVICE_CONTEXT Context;
    WDFFILEOBJECT File;
} VSSCOM_IMPERSONATION, *PVSSCOM_IMPERSONATION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VSSCOM_IMPERSONATION, VssComImpersonation)

static EVT_WDF_REQUEST_IMPERSONATE VssComEvtRequestImpersonate;

// True when the thread's token is in the Administrators group.
static BOOLEAN VssComThreadIsAdmin(VOID)
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(sid);
    BOOL member = FALSE;

    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, (PSID)sid, &size))
        return FALSE;
    if (!CheckTokenMembership(NULL, (PSID)sid, &member))
        return FALSE;
    return member ? TRUE : FALSE;
}

// Runs with the requestor's token on the thread.
static VOID NTAPI VssComEvtRequestImpersonate(_In_ WDFREQUEST Request,
                                              _In_opt_ PVOID Context)
{
    PVSSCOM_IMPERSONATION who = (PVSSCOM_IMPERSONATION)Context;
    BOOLEAN allowed;

    if (who == NULL)
    {
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    // The hub end of a port made on demand would be compared with its maker
    // here. There is no such port in this driver yet, so every port is one made
    // by hand.
    allowed = who->Context->OnDemand ? FALSE : VssComThreadIsAdmin();
    VssComHubOpenDecided(who->Context, Request, who->File, allowed);
}

VOID VssComAuthorizeHubOpen(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                            _In_ WDFREQUEST Request,
                            _In_ WDFFILEOBJECT FileObject)
{
    PVSSCOM_IMPERSONATION who = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VSSCOM_IMPERSONATION);
    status = WdfObjectAllocateContext(Request, &attributes, (PVOID*)&who);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }
    who->Context = Context;
    who->File = FileObject;

    // On success the callback decides and completes the request. On failure it
    // has not run, so the request is refused here.
    status = WdfRequestImpersonate(Request, SecurityIdentification,
                                   VssComEvtRequestImpersonate, who);
    if (!NT_SUCCESS(status))
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
}
