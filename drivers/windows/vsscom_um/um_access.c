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
// The token is at identification level (UmdfImpersonationLevel in the INF),
// which is enough to read its user and groups and no more. Calls that act as the
// user, such as CheckTokenMembership, are refused at that level, so the groups
// are read from the token itself.
//

#include "vsscom.h"

typedef struct _VSSCOM_IMPERSONATION
{
    PVSSCOM_DEVICE_CONTEXT Context;
    BOOLEAN Ran;     // the callback has run
    BOOLEAN Allowed; // and this is what it decided
} VSSCOM_IMPERSONATION, *PVSSCOM_IMPERSONATION;

static EVT_WDF_REQUEST_IMPERSONATE VssComEvtRequestImpersonate;

// True when the thread's token has the Administrators group switched on: the
// group is in the token, and it is not there only to deny. A user who runs
// without elevation has it in the second state.
static BOOLEAN VssComThreadIsAdmin(VOID)
{
    BYTE adminSid[SECURITY_MAX_SID_SIZE];
    DWORD adminSidSize = sizeof(adminSid);
    HANDLE token = NULL;
    DWORD size = 0;
    PTOKEN_GROUPS groups;
    BOOLEAN admin = FALSE;
    DWORD i;

    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, (PSID)adminSid,
                            &adminSidSize))
        return FALSE;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        return FALSE;

    (void)GetTokenInformation(token, TokenGroups, NULL, 0, &size);
    if (size == 0)
    {
        CloseHandle(token);
        return FALSE;
    }
    groups = (PTOKEN_GROUPS)HeapAlloc(GetProcessHeap(), 0, size);
    if (groups != NULL &&
        GetTokenInformation(token, TokenGroups, groups, size, &size))
    {
        for (i = 0; i < groups->GroupCount; ++i)
        {
            const DWORD attributes = groups->Groups[i].Attributes;
            if (EqualSid(groups->Groups[i].Sid, (PSID)adminSid) &&
                (attributes & SE_GROUP_ENABLED) != 0 &&
                (attributes & SE_GROUP_USE_FOR_DENY_ONLY) == 0)
            {
                admin = TRUE;
                break;
            }
        }
    }
    if (groups != NULL)
        HeapFree(GetProcessHeap(), 0, groups);
    CloseHandle(token);
    return admin;
}

// Runs with the requestor's token on the thread. It only decides: the request
// is completed after WdfRequestImpersonate returns, with the thread back on the
// host's own token.
static VOID NTAPI VssComEvtRequestImpersonate(_In_ WDFREQUEST Request,
                                              _In_opt_ PVOID Context)
{
    PVSSCOM_IMPERSONATION who = (PVSSCOM_IMPERSONATION)Context;
    UNREFERENCED_PARAMETER(Request);

    if (who == NULL)
        return;

    // The hub end of a port made on demand would be compared with its maker
    // here. There is no such port in this driver yet, so every port is one made
    // by hand.
    who->Allowed = who->Context->OnDemand ? FALSE : VssComThreadIsAdmin();
    who->Ran = TRUE;
}

VOID VssComAuthorizeHubOpen(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                            _In_ WDFREQUEST Request,
                            _In_ WDFFILEOBJECT FileObject)
{
    // The framework calls the callback before WdfRequestImpersonate returns, so
    // this state may live on the stack.
    VSSCOM_IMPERSONATION who;
    NTSTATUS status;

    who.Context = Context;
    who.Ran = FALSE;
    who.Allowed = FALSE;

    status = WdfRequestImpersonate(Request, SecurityIdentification,
                                   VssComEvtRequestImpersonate, &who);

    // Refused unless the callback ran and said yes.
    VssComHubOpenDecided(Context, Request, FileObject,
                         NT_SUCCESS(status) && who.Ran && who.Allowed);
}
