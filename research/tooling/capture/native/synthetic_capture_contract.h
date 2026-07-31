#pragma once

#include <windows.h>

#include <string>

namespace elysium::capture {

inline constexpr const char* SyntheticTraceContract =
    "elysium.synthetic-capture-trace";
inline constexpr DWORD SyntheticTraceVersion = 1;

inline std::wstring SyntheticCaptureReadyName(DWORD processId) {
    return L"Local\\ElysiumSyntheticCapture.v1." +
        std::to_wstring(processId) + L".ready";
}

inline std::wstring SyntheticCaptureStopName(DWORD processId) {
    return L"Local\\ElysiumSyntheticCapture.v1." +
        std::to_wstring(processId) + L".stop";
}

}  // namespace elysium::capture
