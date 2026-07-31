#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef ELYSIUM_SYNTHETIC_IDENTITY
#error ELYSIUM_SYNTHETIC_IDENTITY must name the synthetic module
#endif

#ifndef ELYSIUM_SYNTHETIC_OFFSET
#error ELYSIUM_SYNTHETIC_OFFSET must distinguish the synthetic module
#endif

extern "C" __declspec(dllexport) const wchar_t* __stdcall
ElysiumSyntheticModuleName() noexcept {
    return ELYSIUM_SYNTHETIC_IDENTITY;
}

extern "C" __declspec(dllexport) __declspec(noinline) int __stdcall
ElysiumSyntheticHookTarget(int value) noexcept {
    volatile int stableValue = value + ELYSIUM_SYNTHETIC_OFFSET;
    return stableValue;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
