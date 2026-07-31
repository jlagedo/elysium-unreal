#pragma once

#include <cstddef>
#include <cstdint>

#include "binary_profile_registry.h"

namespace elysium::capture {

enum class HookBackendResult {
    Installed,
    Disabled,
    Released,
    InvalidDeclaration,
    TargetChanged,
    UnsupportedInstruction,
    AllocationFailed,
    ProtectionFailed,
    NotInstalled,
    StillEnabled,
};

struct HookHandle {
    static constexpr std::size_t MaximumPatchBytes = 32;

    HookBackendKind Backend = HookBackendKind::None;
    void* Target = nullptr;
    void* Replacement = nullptr;
    void* Original = nullptr;
    void* Trampoline = nullptr;
    void** VtableSlot = nullptr;
    std::uint32_t PatchBytes = 0;
    std::uint8_t OriginalBytes[MaximumPatchBytes]{};
    bool Installed = false;
    bool Enabled = false;
};

class HookBackends {
public:
    static HookBackendResult Install(
        const ActiveBinaryProfile& active,
        const BinaryTargetProfile& declaration,
        void* replacement,
        HookHandle* handle) noexcept;

    // Disable restores the target but deliberately retains any trampoline.
    // CAP2.5 owns the active-call drain between Disable and Release.
    static HookBackendResult Disable(HookHandle* handle) noexcept;
    static HookBackendResult Release(HookHandle* handle) noexcept;
};

}  // namespace elysium::capture
