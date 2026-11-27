// Check the actual security boundary, not Windows' emulated CRT mode bits.
#pragma once
#include "platform/file.h"
#ifdef _WIN32
#include <aclapi.h>
#endif

inline bool isPrivateFile(const std::string& path) {
#ifdef _WIN32
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    auto wide = fugue::platform::pathFromUtf8(path).wstring();
    if (GetNamedSecurityInfoW(wide.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                              &owner, nullptr, &acl, nullptr, &sd) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool ok = acl && GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PROTECTED);
    BYTE system[SECURITY_MAX_SID_SIZE], rights[SECURITY_MAX_SID_SIZE];
    DWORD a = sizeof(system), b = sizeof(rights);
    ok = ok && CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &a) &&
         CreateWellKnownSid(WinCreatorOwnerRightsSid, nullptr, rights, &b);
    bool allowed = false;
    for (DWORD i = 0; ok && i < acl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(acl, i, &raw)) {
            ok = false;
            break;
        }
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) {
            ok = false;
            break;
        }
        PSID sid = &ace->SidStart;
        if (!EqualSid(sid, owner) && !EqualSid(sid, system) && !EqualSid(sid, rights)) ok = false;
        if (EqualSid(sid, owner) || EqualSid(sid, rights)) allowed = true;
    }
    LocalFree(sd);
    return ok && allowed;
#else
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600;
#endif
}
