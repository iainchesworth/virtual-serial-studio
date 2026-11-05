//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The helper service that makes the ports of the user-mode driver: the rules it
// keeps (which slot, how many for one user, how many in all) and the protocol
// that the studio, the helper and the driver share. The service itself needs a
// machine with the driver; the guest scripts run it.
//

#include "test_framework.hpp"

#include <cstddef>
#include <string>

// windows.h first: devpropdef.h needs its types.
// clang-format off
#include <windows.h>
#include <devpropdef.h>
// clang-format on

#include <vsscom_helper/lease_table.hpp>
#include <vsscom_helper/protocol.h>

using vss::helper::lease_status;
using vss::helper::lease_table;

TEST_CASE("helper: the first slot is zero and the lowest free slot is next")
{
    lease_table table(4, 4);

    const auto first = table.acquire("alice");
    REQUIRE(first.status == lease_status::granted);
    REQUIRE_EQ(first.slot, std::size_t{0});

    const auto second = table.acquire("alice");
    REQUIRE_EQ(second.slot, std::size_t{1});
    REQUIRE_EQ(table.count(), std::size_t{2});

    // A slot that is given back is the next one given, so a user who makes a
    // port again gets the same COM name.
    REQUIRE(table.release("alice", 0));
    const auto again = table.acquire("alice");
    REQUIRE(again.status == lease_status::granted);
    REQUIRE_EQ(again.slot, std::size_t{0});
}

TEST_CASE("helper: one user cannot hold more than their share")
{
    lease_table table(8, 2);

    REQUIRE(table.acquire("alice").status == lease_status::granted);
    REQUIRE(table.acquire("alice").status == lease_status::granted);
    REQUIRE(table.acquire("alice").status == lease_status::owner_quota);
    REQUIRE_EQ(table.count_of("alice"), std::size_t{2});

    // Another user is not held up by it.
    REQUIRE(table.acquire("bob").status == lease_status::granted);
    REQUIRE_EQ(table.count_of("bob"), std::size_t{1});
}

TEST_CASE("helper: all users together cannot take more slots than there are")
{
    lease_table table(3, 2);

    REQUIRE(table.acquire("alice").status == lease_status::granted);
    REQUIRE(table.acquire("alice").status == lease_status::granted);
    REQUIRE(table.acquire("bob").status == lease_status::granted);

    // Full for bob, who is under his share: the machine is out of slots.
    REQUIRE(table.acquire("bob").status == lease_status::no_slots);
    // Alice is at her share, and hears that first.
    REQUIRE(table.acquire("alice").status == lease_status::owner_quota);
}

TEST_CASE("helper: only the owner gives a slot back")
{
    lease_table table(4, 4);
    const auto grant = table.acquire("alice");

    REQUIRE(!table.release("bob", grant.slot));
    REQUIRE_EQ(table.count(), std::size_t{1});

    REQUIRE(table.release("alice", grant.slot));
    REQUIRE_EQ(table.count(), std::size_t{0});

    // A slot that is free, or that does not exist, is not released.
    REQUIRE(!table.release("alice", grant.slot));
    REQUIRE(!table.release("alice", 99));
}

TEST_CASE("helper: an owner is any string, and binary ones differ")
{
    // The helper uses the bytes of a SID, which can hold zeros.
    lease_table table(4, 1);
    const std::string first("\x01\x00\x05", 3);
    const std::string second("\x01\x00\x06", 3);

    REQUIRE(table.acquire(first).status == lease_status::granted);
    REQUIRE(table.acquire(first).status == lease_status::owner_quota);
    REQUIRE(table.acquire(second).status == lease_status::granted);
}

TEST_CASE("helper: the protocol has the layout the studio and the helper share")
{
    // The client and the helper are built apart; a change here is a new
    // version.
    REQUIRE_EQ(sizeof(VSSCOM_HELPER_REQUEST), std::size_t{16});
    REQUIRE_EQ(sizeof(VSSCOM_HELPER_REPLY), std::size_t{16});
    REQUIRE_EQ(offsetof(VSSCOM_HELPER_REQUEST, Op), std::size_t{4});
    REQUIRE_EQ(offsetof(VSSCOM_HELPER_REQUEST, Slot), std::size_t{8});
    REQUIRE_EQ(offsetof(VSSCOM_HELPER_REPLY, Status), std::size_t{4});
    REQUIRE_EQ(offsetof(VSSCOM_HELPER_REPLY, Slot), std::size_t{8});
    REQUIRE_EQ(static_cast<int>(VSSCOM_HELPER_VERSION), 1);
    REQUIRE_EQ(static_cast<int>(VSSCOM_HELPER_OP_CREATE), 1);
    REQUIRE_EQ(static_cast<int>(VSSCOM_HELPER_OP_DESTROY), 2);

    // The limits are the control device's, so the two drivers behave alike.
    REQUIRE_EQ(static_cast<int>(VSSCOM_HELPER_MAX_PORTS), 32);
    REQUIRE_EQ(static_cast<int>(VSSCOM_HELPER_MAX_PORTS_PER_USER), 16);
    REQUIRE_EQ(static_cast<std::size_t>(VSSCOM_HELPER_MAX_SID_BYTES),
               static_cast<std::size_t>(SECURITY_MAX_SID_SIZE));
}

TEST_CASE("helper: the names the guest scripts and the service use")
{
    // install-helper.ps1 and test-driver.ps1 spell these out.
    REQUIRE(std::wstring(VssComHelperPipeName) ==
            L"\\\\.\\pipe\\vsscom_helper");
    REQUIRE(std::wstring(VssComHelperServiceName) == L"vsscom_helper");
    REQUIRE(std::wstring(VssComUmPortHardwareId) == L"VSSCOMUM\\PORT");
    REQUIRE(std::wstring(VssComUmPortInstancePrefix) == L"SWD\\VSSCOMUM\\");
    REQUIRE_EQ(VssComUmOwnerKey.pid, static_cast<ULONG>(2));
}
