// Small Qt-free helpers for the on-disk cache and state files.
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <windows.h>
#include <sddl.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <unistd.h>
#endif

namespace fugue::platform {
// Preserve UTF-8 interpretation on Windows; path(std::string) uses the native
// narrow encoding there. C++20 replaces u8path with char8_t-based constructors.
inline std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// Keep CRT spelling differences here, without disabling deprecation warnings.
#ifdef _WIN32
inline int close(int fd) { return ::_close(fd); }
inline int write(int fd, const void* data, unsigned count) { return ::_write(fd, data, count); }
inline int unlink(const char* path) { return ::_unlink(path); }
inline int chmod(const char* path, int mode) { return ::_chmod(path, mode); }
inline int fileno(FILE* file) { return ::_fileno(file); }
inline FILE* fdopen(int fd, const char* mode) { return ::_fdopen(fd, mode); }
#else
using ::chmod;
using ::close;
using ::fdopen;
using ::fileno;
using ::unlink;
using ::write;
#endif

inline std::string homeDir() {
    const char* h = std::getenv("HOME");
#ifdef _WIN32
    if (!h || !*h) h = std::getenv("USERPROFILE");
#endif
    std::string result = h ? h : "";
#ifdef _WIN32
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    return result;
}
#ifdef _WIN32
// Owner and SYSTEM only, not inherited from the parent; OI/CI pass it on to what is created inside.
constexpr const wchar_t* kPrivateDirSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;OW)";
#endif
// A mode without group or other bits is a private directory: on Windows it gets kPrivateDirSddl.
inline int mkdir(const char* path, int mode) {
#ifdef _WIN32
    if (mode & 077) return _mkdir(path);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kPrivateDirSddl, SDDL_REVISION_1, &sd, nullptr)) {
        errno = EACCES;
        return -1;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    const BOOL ok = CreateDirectoryW(pathFromUtf8(path).c_str(), &sa);
    const DWORD e = ok ? 0 : GetLastError();
    LocalFree(sd);
    if (ok) return 0;
    errno = e == ERROR_ALREADY_EXISTS ? EEXIST : e == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
    return -1;
#else
    return ::mkdir(path, mode);
#endif
}
inline int rename(const char* from, const char* to) {
#ifdef _WIN32
    if (MoveFileExW(pathFromUtf8(from).c_str(), pathFromUtf8(to).c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 0;
    errno = EIO;
    return -1;
#else
    return ::rename(from, to);
#endif
}
inline int open(const char* path, int flags, int mode) {
#ifdef _WIN32
    return _wopen(pathFromUtf8(path).c_str(), flags | _O_BINARY | _O_NOINHERIT, mode);
#else
    return ::open(path, flags | O_CLOEXEC, mode);
#endif
}
inline int fchmod(int fd, int mode) {
#ifdef _WIN32
    // CRT permission bits do not implement Unix ACLs. Secret files use privateWrite below.
    (void)fd;
    (void)mode;
    return 0;
#else
    return ::fchmod(fd, mode);
#endif
}
#ifdef _WIN32
// Create a new temporary secret with a protected ACL: owner and SYSTEM only.
// The protected ACL travels with the file when it atomically replaces its destination.
inline bool privateWrite(const std::string& path, const std::string& data, std::string* err) {
    auto dest = pathFromUtf8(path);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;OW)", SDDL_REVISION_1, &sd,
                                                              nullptr)) {
        if (err) *err = "cannot create private file security descriptor";
        return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path tmp;
    for (unsigned i = 0; i < 100 && file == INVALID_HANDLE_VALUE; ++i) {
        tmp = dest;
        tmp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" +
               std::to_wstring(i);
        file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
    }
    LocalFree(sd);
    if (file == INVALID_HANDLE_VALUE) {
        if (err) *err = "cannot create private temporary file";
        return false;
    }
    bool ok = true;
    size_t off = 0;
    while (off < data.size()) {
        DWORD written = 0;
        if (!WriteFile(file, data.data() + off, DWORD(std::min(data.size() - off, size_t(1 << 30))), &written,
                       nullptr) ||
            !written) {
            ok = false;
            break;
        }
        off += written;
    }
    ok = FlushFileBuffers(file) && ok;
    ok = CloseHandle(file) && ok;
    if (ok) ok = MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        DeleteFileW(tmp.c_str());
        if (err) *err = "cannot commit private file";
    }
    return ok;
}
#endif
}  // namespace fugue::platform
