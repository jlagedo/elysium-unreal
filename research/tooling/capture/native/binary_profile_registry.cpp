#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>
#include <iterator>

#include "binary_profile_registry.h"
#include "generated_binary_profiles.h"

namespace elysium::capture {
namespace {

const wchar_t* Filename(const wchar_t* path) noexcept {
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    const wchar_t* forwardSlash = std::wcsrchr(path, L'/');
    const wchar_t* separator = slash;
    if (forwardSlash != nullptr &&
        (separator == nullptr || forwardSlash > separator)) {
        separator = forwardSlash;
    }
    return separator == nullptr ? path : separator + 1;
}

bool EqualPeIdentity(
    const PeIdentity& left,
    const PeIdentity& right) noexcept {
    return left.Machine == right.Machine &&
        left.OptionalMagic == right.OptionalMagic &&
        left.SectionCount == right.SectionCount &&
        left.Characteristics == right.Characteristics &&
        left.Timestamp == right.Timestamp &&
        left.PreferredImageBase == right.PreferredImageBase &&
        left.SizeOfImage == right.SizeOfImage &&
        left.Checksum == right.Checksum;
}

}  // namespace

BinaryProfileRegistry::BinaryProfileRegistry() noexcept
    : BinaryProfileRegistry(
          profiles::Registry,
          std::size(profiles::Registry)) {}

BinaryProfileRegistry::BinaryProfileRegistry(
    const BinaryProfile* profiles,
    std::size_t profileCount) noexcept
    : Profiles_(profiles),
      ProfileCount_(profileCount) {}

ProfileActivationResult BinaryProfileRegistry::Activate(
    const ProcessedModuleEvent& event,
    BinaryProfileMatchDiagnostic* diagnostic) noexcept {
    const auto existing = Active_.find(event.ImageBase);
    const BinaryProfile* profile = Match(event, diagnostic);
    if (existing != Active_.end()) {
        return existing->second == profile
            ? ProfileActivationResult::Duplicate
            : ProfileActivationResult::Conflict;
    }
    if (profile == nullptr) {
        return ProfileActivationResult::Unknown;
    }
    Active_.emplace(event.ImageBase, profile);
    return ProfileActivationResult::Matched;
}

bool BinaryProfileRegistry::Deactivate(
    std::uintptr_t imageBase) noexcept {
    return Active_.erase(imageBase) != 0;
}

bool BinaryProfileRegistry::FindActive(
    std::uintptr_t imageBase,
    ActiveBinaryProfile* active) const noexcept {
    if (active == nullptr) {
        return false;
    }
    const auto found = Active_.find(imageBase);
    if (found == Active_.end()) {
        return false;
    }
    *active = {
        found->second,
        imageBase,
        found->second->Pe.SizeOfImage,
    };
    return true;
}

bool BinaryProfileRegistry::FindActive(
    const BinaryProfile& profile,
    ActiveBinaryProfile* active) const noexcept {
    if (active == nullptr) {
        return false;
    }
    for (const auto& entry : Active_) {
        if (entry.second == &profile) {
            *active = {
                entry.second,
                entry.first,
                entry.second->Pe.SizeOfImage,
            };
            return true;
        }
    }
    return false;
}

const BinaryProfile* BinaryProfileRegistry::Match(
    const ProcessedModuleEvent& event,
    BinaryProfileMatchDiagnostic* diagnostic) const noexcept {
    if (diagnostic != nullptr) {
        *diagnostic = {};
    }
    if (event.Path == nullptr) {
        return nullptr;
    }
    const wchar_t* moduleName =
        event.ModuleName != nullptr && event.ModuleNameCharacters != 0
            ? event.ModuleName
            : Filename(event.Path);
    for (std::size_t index = 0; index < ProfileCount_; ++index) {
        const BinaryProfile& profile = Profiles_[index];
        const bool moduleNameMatches =
            _wcsicmp(moduleName, profile.ModuleName) == 0;
        std::uint32_t mismatches = ProfileMismatchNone;
        if (!event.HashSucceeded) {
            mismatches |= ProfileMismatchHashUnavailable;
        }
        if (!event.IdentitySucceeded) {
            mismatches |= ProfileMismatchIdentityUnavailable;
        }
        if (event.FileSize != profile.FileSize) {
            mismatches |= ProfileMismatchFileSize;
        }
        if (event.ImageSize != profile.Pe.SizeOfImage) {
            mismatches |= ProfileMismatchImageSize;
        }
        if (!EqualPeIdentity(event.Pe, profile.Pe)) {
            mismatches |= ProfileMismatchPeIdentity;
        }
        if (std::memcmp(
                event.Sha256.data(),
                profile.Sha256,
                event.Sha256.size()) != 0) {
            mismatches |= ProfileMismatchSha256;
        }
        const bool likelyCandidate =
            moduleNameMatches ||
            event.ImageSize == profile.Pe.SizeOfImage ||
            (event.FileSize != 0 &&
             event.FileSize == profile.FileSize) ||
            mismatches == ProfileMismatchNone;
        if (diagnostic != nullptr && likelyCandidate) {
            diagnostic->Candidate = &profile;
            diagnostic->ProfileIndex = index;
            diagnostic->MismatchFlags = mismatches;
        }
        if (mismatches != ProfileMismatchNone) {
            continue;
        }
        return &profile;
    }
    return nullptr;
}

const BinaryTargetProfile* BinaryProfileRegistry::FindTarget(
    const BinaryProfile& profile,
    const char* semanticLabel) noexcept {
    if (semanticLabel == nullptr) {
        return nullptr;
    }
    for (std::uint32_t index = 0; index < profile.TargetCount; ++index) {
        const BinaryTargetProfile& target = profile.Targets[index];
        if (std::strcmp(target.SemanticLabel, semanticLabel) == 0) {
            return &target;
        }
    }
    return nullptr;
}

std::uintptr_t BinaryProfileRegistry::ResolveTarget(
    const ActiveBinaryProfile& active,
    const BinaryTargetProfile& target) noexcept {
    if (target.Rva >= active.ImageSize) {
        return 0;
    }
    return active.ImageBase + target.Rva;
}

std::size_t BinaryProfileRegistry::ProfileCount() const noexcept {
    return ProfileCount_;
}

const BinaryProfile* BinaryProfileRegistry::ProfileAt(
    std::size_t index) const noexcept {
    return index < ProfileCount_ ? &Profiles_[index] : nullptr;
}

std::size_t BinaryProfileRegistry::ActiveCount() const noexcept {
    return Active_.size();
}

std::uint32_t BinaryProfileRegistry::CompiledRegistryVersion() noexcept {
    return profiles::RegistryVersion;
}

}  // namespace elysium::capture
