//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Who may open what.
//
// The COM port is open to anyone the Ports class lets in, as any serial port
// is. The hub end is the device side of the port: whoever holds it supplies the
// bytes the application reads and sees the ones it writes. It is therefore not
// open to everyone who can see the port.
//
//   - a port made on demand: only the user who made it, so another user cannot
//     take the hub end in the second between the port appearing and its maker
//     opening it, or later;
//   - a port made by hand (install-driver.ps1): administrators.
//
// Kernel-mode callers are trusted.
//

#include "vsscom.h"

NTSTATUS VssComCaptureOpener(_In_ WDFREQUEST Request,
                             _Out_ PVSSCOM_OPENER Opener)
{
    PIRP irp = WdfRequestWdmGetIrp(Request);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PSECURITY_SUBJECT_CONTEXT subject;
    PACCESS_TOKEN token;
    PTOKEN_USER user = NULL;
    NTSTATUS status;

    RtlZeroMemory(Opener, sizeof(*Opener));
    if (WdfRequestGetRequestorMode(Request) == KernelMode)
    {
        Opener->Kernel = TRUE;
        return STATUS_SUCCESS;
    }

    // The access state of a create request holds the token that is opening,
    // with impersonation applied.
    if (stack->Parameters.Create.SecurityContext == NULL ||
        stack->Parameters.Create.SecurityContext->AccessState == NULL)
        return STATUS_ACCESS_DENIED;
    subject = &stack->Parameters.Create.SecurityContext->AccessState
                   ->SubjectSecurityContext;

    SeLockSubjectContext(subject);
    token = SeQuerySubjectContextToken(subject);
    if (token == NULL)
    {
        SeUnlockSubjectContext(subject);
        return STATUS_ACCESS_DENIED;
    }
    Opener->Admin = SeTokenIsAdmin(token) ? TRUE : FALSE;
    status = SeQueryInformationToken(token, TokenUser, (PVOID*)&user);
    if (NT_SUCCESS(status))
    {
        const ULONG length = RtlLengthSid(user->User.Sid);
        if (length <= sizeof(Opener->Sid))
        {
            status = RtlCopySid(sizeof(Opener->Sid), (PSID)Opener->Sid,
                                user->User.Sid);
            Opener->Valid = NT_SUCCESS(status);
        }
        else
        {
            status = STATUS_BUFFER_OVERFLOW;
        }
        ExFreePool(user);
    }
    SeUnlockSubjectContext(subject);
    return status;
}

BOOLEAN VssComHubOpenAllowed(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                             _In_ const VSSCOM_OPENER* Opener)
{
    VSSCOM_OPENER owner;

    if (Opener->Kernel)
        return TRUE;

    if (!Context->OnDemand)
        return Opener->Admin;

    if (!Opener->Valid || !VssComControlOwnerOf(Context->Slot, &owner) ||
        !owner.Valid)
        return FALSE;
    return RtlEqualSid((PSID)owner.Sid, (PSID)Opener->Sid) ? TRUE : FALSE;
}

VOID VssComAuthorizeHubOpen(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                            _In_ WDFREQUEST Request,
                            _In_ WDFFILEOBJECT FileObject)
{
    VSSCOM_OPENER opener;
    NTSTATUS status = VssComCaptureOpener(Request, &opener);

    VssComHubOpenDecided(Context, Request, FileObject,
                         NT_SUCCESS(status) &&
                             VssComHubOpenAllowed(Context, &opener));
}
