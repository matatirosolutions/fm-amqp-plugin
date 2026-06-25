#include "CACert.h"
#include <sys/stat.h>

static bool FileExists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

#if defined(_WIN32)

#include <windows.h>

std::string GetBundledCACertPath() {
    char path[MAX_PATH] = {};
    HMODULE hm = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetBundledCACertPath), &hm);
    GetModuleFileNameA(hm, path, MAX_PATH);

    std::string p(path);
    auto pos = p.find_last_of("\\/");
    std::string dir = (pos != std::string::npos) ? p.substr(0, pos) : p;
    std::string candidate = dir + "\\cacert.pem";
    return FileExists(candidate) ? candidate : std::string{};
}

#elif defined(__APPLE__)

#include <dlfcn.h>

std::string GetBundledCACertPath() {
    Dl_info info{};
    dladdr(reinterpret_cast<void*>(&GetBundledCACertPath), &info);

    // Plugin binary: *.fmplugin/Contents/MacOS/AMQPFMPlugin
    // CA bundle at: *.fmplugin/Contents/Resources/cacert.pem
    std::string p(info.dli_fname ? info.dli_fname : "");
    auto pos = p.find_last_of('/');
    if (pos != std::string::npos) p = p.substr(0, pos); // strip binary name
    std::string candidate = p + "/../Resources/cacert.pem";
    return FileExists(candidate) ? candidate : std::string{};
}

#else // Linux

#include <dlfcn.h>

std::string GetBundledCACertPath() {
    Dl_info info{};
    dladdr(reinterpret_cast<void*>(&GetBundledCACertPath), &info);

    std::string p(info.dli_fname ? info.dli_fname : "");
    auto pos = p.find_last_of('/');
    std::string dir = (pos != std::string::npos) ? p.substr(0, pos) : p;
    std::string candidate = dir + "/cacert.pem";
    return FileExists(candidate) ? candidate : std::string{};
}

#endif
