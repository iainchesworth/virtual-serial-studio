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
//   - a port made on demand: only the user who made it. The helper service made
//     it and put that user's SID on the device as the owner property
//     (vsscom_helper/protocol.h); the callback compares it with the user of the
//     token;
//   - a port made by hand: administrators.
//
// The token is at identification level (UmdfImpersonationLevel in the INF),
// which is enough to read its user and groups and no more. Calls that act as the
// user, such as CheckTokenMembership, are refused at that level, so the groups
// are read from the token itself.
//

#include "vsscom_um.h"

typedef struct _VSSCOM_IMPERSONATION
{
    PVSSCOM_DEVICE_CONTEXT Context;
    VSSCOM_UM_OWNER Owner; // of a port made on demand
    BOOLEAN Ran;           // the callback has run
    BOOLEAN Allowed;       // and this is what it decided
} VSSCOM_IMPERSONATION, *PVSSCOM_IMPERSONATION;

static EVT_WDF_REQUEST_IMPERSONATE VssComEvtRequestImpersonate;

// True when what a property query returned is the binary form of a SID.
static BOOLEAN VssComUmOwnerIsSid(_In_ NTSTATUS Status, _In_ DEVPROPTYPE Type,
                                  _In_ ULONG Size,
                                  _In_ const VSSCOM_UM_OWNER* Owner)
{
    return NT_SUCCESS(Status) && Type == DEVPROP_TYPE_BINARY && Size > 0 &&
           Size <= sizeof(Owner->Sid) && IsValidSid((PSID)Owner->Sid) &&
           GetLengthSid((PSID)Owner->Sid) == Size;
}

BOOLEAN VssComUmDeviceInitHasOwner(_In_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_DEVICE_PROPERTY_DATA data;
    VSSCOM_UM_OWNER owner;
    DEVPROPTYPE type = 0;
    ULONG size = 0;
    NTSTATUS status;

    RtlZeroMemory(&owner, sizeof(owner));
    WDF_DEVICE_PROPERTY_DATA_INIT(&data, &VssComUmOwnerKey);
    status = WdfFdoInitQueryPropertyEx(DeviceInit, &data, sizeof(owner.Sid),
                                       owner.Sid, &size, &type);
    owner.Size = size;
    return VssComUmOwnerIsSid(status, type, size, &owner);
}

// Reads the owner of the port from the device. FALSE when there is none that is
// a SID, which refuses the open.
static BOOLEAN VssComUmQueryOwner(_In_ WDFDEVICE Device,
                                  _Out_ PVSSCOM_UM_OWNER Owner)
{
    WDF_DEVICE_PROPERTY_DATA data;
    DEVPROPTYPE type = 0;
    ULONG size = 0;
    NTSTATUS status;

    RtlZeroMemory(Owner, sizeof(*Owner));
    WDF_DEVICE_PROPERTY_DATA_INIT(&data, &VssComUmOwnerKey);
    status = WdfDeviceQueryPropertyEx(Device, &data, sizeof(Owner->Sid),
                                      Owner->Sid, &size, &type);
    Owner->Size = size;
    return VssComUmOwnerIsSid(status, type, size, Owner);
}

// True when the thread's token is of the user in `Owner`.
static BOOLEAN VssComThreadIsOwner(_In_ const VSSCOM_UM_OWNER* Owner)
{
    HANDLE token = NULL;
    DWORD size = 0;
    PTOKEN_USER user;
    BOOLEAN same = FALSE;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        return FALSE;

    (void)GetTokenInformation(token, TokenUser, NULL, 0, &size);
    if (size == 0)
    {
        CloseHandle(token);
        return FALSE;
    }
    user = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), 0, size);
    if (user != NULL && GetTokenInformation(token, TokenUser, user, size, &size))
        same = EqualSid(user->User.Sid, (PSID)Owner->Sid) ? TRUE : FALSE;
    if (user != NULL)
        HeapFree(GetProcessHeap(), 0, user);
    CloseHandle(token);
    return same;
}

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

    // The hub end of a port made on demand opens for the user who asked for the
    // port; one made by hand, for administrators.
    who->Allowed = who->Context->OnDemand ? VssComThreadIsOwner(&who->Owner)
                                          : VssComThreadIsAdmin();
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
    RtlZeroMemory(&who.Owner, sizeof(who.Owner));

    // A port made on demand with no usable owner is closed to everyone.
    if (Context->OnDemand && !VssComUmQueryOwner(Context->Device, &who.Owner))
    {
        VssComHubOpenDecided(Context, Request, FileObject, FALSE);
        return;
    }

    status = WdfRequestImpersonate(Request, SecurityIdentification,
                                   VssComEvtRequestImpersonate, &who);

    // Refused unless the callback ran and said yes.
    VssComHubOpenDecided(Context, Request, FileObject,
                         NT_SUCCESS(status) && who.Ran && who.Allowed);
}
