#pragma once

#if defined(_WIN32)

#include <windows.h>

#include <string>

namespace psycle::loader {

HMODULE loadWindowsMachineModule(
    const std::wstring& path,
    DWORD& errorCode,
    std::string& dependencyDiagnostic
);

} // namespace psycle::loader

#endif