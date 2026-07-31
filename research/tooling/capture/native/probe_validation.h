#pragma once

#include <cstddef>
#include <cstdint>

#include "binary_profile_registry.h"

namespace elysium::capture {

enum class ProbeDiagnosticReason : std::uint32_t {
    UnknownHash = 1,
    MissingModule = 2,
    UnexpectedPrologue = 3,
    InvalidVtableSlot = 4,
};

struct ProbeValidationDiagnostic {
    ProbeDiagnosticReason Reason;
    std::uint32_t ProfileIndex;
    std::uint32_t TargetIndex;
    std::uintptr_t ImageBase;
    std::uint32_t TargetRva;
    std::uint32_t Expected;
    std::uint32_t Observed;
};

using ProbeDiagnosticSink = void (*)(
    const ProbeValidationDiagnostic& diagnostic,
    void* context) noexcept;
using ProbeInstallCallback = bool (*)(
    const ActiveBinaryProfile& active,
    const BinaryTargetProfile& target,
    void* context) noexcept;

enum class ProbeActivationOutcome {
    Installed,
    Rejected,
    InstallFailed,
};

class ProbeActivationGate {
public:
    ProbeActivationGate(
        ProbeDiagnosticSink sink,
        void* sinkContext) noexcept;

    bool ValidateTarget(
        const ActiveBinaryProfile& active,
        std::uint32_t profileIndex,
        const BinaryTargetProfile& target,
        std::uint32_t targetIndex) const noexcept;
    ProbeActivationOutcome ValidateAndInstall(
        const ActiveBinaryProfile& active,
        std::uint32_t profileIndex,
        const BinaryTargetProfile& target,
        std::uint32_t targetIndex,
        ProbeInstallCallback install,
        void* installContext) const noexcept;

    void RejectUnknownHash(
        std::uint32_t profileIndex,
        std::uintptr_t imageBase,
        std::uint32_t mismatchFlags) const noexcept;
    void RejectMissingModule(std::uint32_t profileIndex) const noexcept;

private:
    void Reject(
        ProbeDiagnosticReason reason,
        std::uint32_t profileIndex,
        std::uint32_t targetIndex,
        std::uintptr_t imageBase,
        std::uint32_t targetRva,
        std::uint32_t expected,
        std::uint32_t observed) const noexcept;

    ProbeDiagnosticSink Sink_;
    void* SinkContext_;
};

}  // namespace elysium::capture
