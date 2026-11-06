//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/process_control.hpp>

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace vss
{

namespace
{

struct handle_closer
{
    void operator()(HANDLE handle) const noexcept
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle);
    }
};
using handle_owner = std::unique_ptr<void, handle_closer>;

std::string to_utf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int size =
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                              static_cast<int>(text.size()), nullptr, 0, nullptr,
                              nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), size, nullptr, nullptr);
    return out;
}

/// The user of a token, as the bytes of a TOKEN_USER; empty when unreadable.
std::vector<unsigned char> token_user(HANDLE token)
{
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0)
        return {};
    std::vector<unsigned char> buffer(size);
    if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size) == 0)
        return {};
    return buffer;
}

bool same_user(HANDLE process)
{
    HANDLE raw_other = nullptr;
    if (::OpenProcessToken(process, TOKEN_QUERY, &raw_other) == 0)
        return false;
    const handle_owner other(raw_other);
    HANDLE raw_mine = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_mine) == 0)
        return false;
    const handle_owner mine(raw_mine);

    const auto theirs = token_user(other.get());
    const auto ours = token_user(mine.get());
    if (theirs.empty() || ours.empty())
        return false;
    return ::EqualSid(
               reinterpret_cast<const TOKEN_USER*>(theirs.data())->User.Sid,
               reinterpret_cast<const TOKEN_USER*>(ours.data())->User.Sid) != 0;
}

bool elevated()
{
    HANDLE raw = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw) == 0)
        return false;
    const handle_owner token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return ::GetTokenInformation(token.get(), TokenElevation, &elevation,
                                 sizeof(elevation), &size) != 0 &&
           elevation.TokenIsElevated != 0;
}

std::string image_name(HANDLE process)
{
    std::wstring path(1024, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (::QueryFullProcessImageNameW(process, 0, path.data(), &size) == 0)
        return {};
    path.resize(size);
    const auto slash = path.find_last_of(L"/\\");
    return to_utf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
}

} // namespace

std::optional<process_summary> describe_process(std::uint32_t id)
{
    if (id == 0)
        return std::nullopt;
    const handle_owner process(
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id));
    if (!process)
        return std::nullopt;

    // A process that has ended can still be opened while something holds a
    // handle to it.
    DWORD code = 0;
    if (::GetExitCodeProcess(process.get(), &code) != 0 && code != STILL_ACTIVE)
        return std::nullopt;

    process_summary out;
    out.id = id;
    out.name = image_name(process.get());
    out.own_user = same_user(process.get());
    out.can_end = out.own_user || elevated();
    return out;
}

result<void> terminate_process(std::uint32_t id)
{
    if (id == 0 || id == ::GetCurrentProcessId())
        return make_error_code(errc::invalid_argument);

    const handle_owner process(::OpenProcess(
        PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id));
    if (!process)
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_INVALID_PARAMETER)
            return make_error_code(errc::not_found);
        return make_error_code(errc::permission_denied);
    }
    // The user's own programs are theirs to end.  Another user's are ended
    // only by an elevated process; this is checked here, and not left to the
    // operating system, so that the rule is the same everywhere it is asked.
    if (!same_user(process.get()) && !elevated())
        return make_error_code(errc::permission_denied);

    if (::TerminateProcess(process.get(), 1) == 0)
        return make_error_code(errc::permission_denied);
    return {};
}

} // namespace vss
