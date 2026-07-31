#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "binary_profile_contract.h"
#include "module_observer.h"

namespace elysium::capture {

enum class ProfileActivationResult {
    Matched,
    Duplicate,
    Unknown,
    Conflict,
};

enum BinaryProfileMismatch : std::uint32_t {
    ProfileMismatchNone = 0,
    ProfileMismatchHashUnavailable = 1u << 0,
    ProfileMismatchIdentityUnavailable = 1u << 1,
    ProfileMismatchFileSize = 1u << 2,
    ProfileMismatchImageSize = 1u << 3,
    ProfileMismatchPeIdentity = 1u << 4,
    ProfileMismatchSha256 = 1u << 5,
};

struct BinaryProfileMatchDiagnostic {
    const BinaryProfile* Candidate = nullptr;
    std::size_t ProfileIndex = 0;
    std::uint32_t MismatchFlags = ProfileMismatchNone;
};

struct ActiveBinaryProfile {
    const BinaryProfile* Profile;
    std::uintptr_t ImageBase;
    std::uint32_t ImageSize;
};

class BinaryProfileRegistry {
public:
    BinaryProfileRegistry() noexcept;
    BinaryProfileRegistry(
        const BinaryProfile* profiles,
        std::size_t profileCount) noexcept;

    ProfileActivationResult Activate(
        const ProcessedModuleEvent& event,
        BinaryProfileMatchDiagnostic* diagnostic = nullptr) noexcept;
    bool Deactivate(std::uintptr_t imageBase) noexcept;
    bool FindActive(
        std::uintptr_t imageBase,
        ActiveBinaryProfile* active) const noexcept;
    bool FindActive(
        const BinaryProfile& profile,
        ActiveBinaryProfile* active) const noexcept;

    const BinaryProfile* Match(
        const ProcessedModuleEvent& event,
        BinaryProfileMatchDiagnostic* diagnostic = nullptr) const noexcept;
    static const BinaryTargetProfile* FindTarget(
        const BinaryProfile& profile,
        const char* semanticLabel) noexcept;
    static std::uintptr_t ResolveTarget(
        const ActiveBinaryProfile& active,
        const BinaryTargetProfile& target) noexcept;

    std::size_t ProfileCount() const noexcept;
    const BinaryProfile* ProfileAt(std::size_t index) const noexcept;
    std::size_t ActiveCount() const noexcept;
private:
    const BinaryProfile* Profiles_;
    std::size_t ProfileCount_;
    std::unordered_map<std::uintptr_t, const BinaryProfile*> Active_;
};

}  // namespace elysium::capture
