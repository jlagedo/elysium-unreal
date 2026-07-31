#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "probe_validation.h"

namespace elysium::capture {
namespace {

constexpr std::uint32_t NoTarget = std::numeric_limits<std::uint32_t>::max();

bool IsImageRangeValid(
    const ActiveBinaryProfile& active,
    std::uint32_t rva,
    std::size_t bytes) noexcept {
    return rva < active.ImageSize &&
        bytes <= static_cast<std::size_t>(active.ImageSize - rva);
}

bool ReadLocalMemory(
    std::uintptr_t address,
    void* destination,
    std::size_t bytes) noexcept {
    SIZE_T read = 0;
    return bytes != 0 &&
        ReadProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<const void*>(address),
            destination,
            bytes,
            &read) &&
        read == bytes;
}

std::uint32_t FirstWord(
    const std::uint8_t* bytes,
    std::size_t byteCount) noexcept {
    std::uint32_t word = 0;
    if (bytes != nullptr) {
        std::memcpy(
            &word,
            bytes,
            (std::min)(byteCount, sizeof(word)));
    }
    return word;
}

}  // namespace

ProbeActivationGate::ProbeActivationGate(
    const CaptureRecordSchemaIdentity* compiledSchemas,
    std::size_t compiledSchemaCount,
    ProbeDiagnosticSink sink,
    void* sinkContext) noexcept
    : CompiledSchemas_(compiledSchemas),
      CompiledSchemaCount_(compiledSchemaCount),
      Sink_(sink),
      SinkContext_(sinkContext) {}

bool ProbeActivationGate::ValidateTarget(
    const ActiveBinaryProfile& active,
    std::uint32_t profileIndex,
    const BinaryTargetProfile& target,
    std::uint32_t targetIndex) const noexcept {
    for (std::uint32_t index = 0;
         index < target.SupportedSchemaCount;
         ++index) {
        const RecordSchemaSupport& schema = target.SupportedSchemas[index];
        if (!SupportsSchema(schema)) {
            Reject(
                ProbeDiagnosticReason::UnsupportedSchema,
                profileIndex,
                targetIndex,
                active.ImageBase,
                target.Rva,
                schema.RecordId,
                schema.SchemaVersion);
            return false;
        }
    }

    if (target.ExpectedBytes == nullptr ||
        target.ExpectedByteCount == 0 ||
        !IsImageRangeValid(
            active,
            target.Rva,
            target.ExpectedByteCount)) {
        Reject(
            ProbeDiagnosticReason::UnexpectedPrologue,
            profileIndex,
            targetIndex,
            active.ImageBase,
            target.Rva,
            FirstWord(
                target.ExpectedBytes,
                target.ExpectedByteCount),
            0);
        return false;
    }
    std::array<std::uint8_t, 32> observedBytes{};
    std::size_t offset = 0;
    bool bytesMatch = true;
    std::uint32_t observedWord = 0;
    while (offset < target.ExpectedByteCount) {
        const std::size_t chunk = (std::min)(
            observedBytes.size(),
            static_cast<std::size_t>(
                target.ExpectedByteCount) - offset);
        if (!ReadLocalMemory(
                active.ImageBase + target.Rva + offset,
                observedBytes.data(),
                chunk)) {
            bytesMatch = false;
            break;
        }
        if (offset == 0) {
            observedWord = FirstWord(
                observedBytes.data(),
                chunk);
        }
        if (std::memcmp(
                observedBytes.data(),
                target.ExpectedBytes + offset,
                chunk) != 0) {
            bytesMatch = false;
            break;
        }
        offset += chunk;
    }
    if (!bytesMatch) {
        Reject(
            ProbeDiagnosticReason::UnexpectedPrologue,
            profileIndex,
            targetIndex,
            active.ImageBase,
            target.Rva,
            FirstWord(
                target.ExpectedBytes,
                target.ExpectedByteCount),
            observedWord);
        return false;
    }

    if (target.Kind != BinaryTargetKind::Vtable) {
        return true;
    }

    const std::uint64_t slotRva =
        static_cast<std::uint64_t>(target.ExpectedVtableRva) +
        static_cast<std::uint64_t>(target.VtableSlot) *
            sizeof(std::uintptr_t);
    if (!IsImageRangeValid(
            active,
            target.ObjectRva,
            sizeof(std::uintptr_t)) ||
        slotRva > std::numeric_limits<std::uint32_t>::max() ||
        !IsImageRangeValid(
            active,
            static_cast<std::uint32_t>(slotRva),
            sizeof(std::uintptr_t))) {
        Reject(
            ProbeDiagnosticReason::InvalidVtableSlot,
            profileIndex,
            targetIndex,
            active.ImageBase,
            target.Rva,
            static_cast<std::uint32_t>(
                active.ImageBase + target.ExpectedVtableRva),
            0);
        return false;
    }

    std::uintptr_t actualVtable = 0;
    const std::uintptr_t expectedVtable =
        active.ImageBase + target.ExpectedVtableRva;
    if (!ReadLocalMemory(
            active.ImageBase + target.ObjectRva,
            &actualVtable,
            sizeof(actualVtable)) ||
        actualVtable != expectedVtable) {
        Reject(
            ProbeDiagnosticReason::InvalidVtableSlot,
            profileIndex,
            targetIndex,
            active.ImageBase,
            target.Rva,
            static_cast<std::uint32_t>(expectedVtable),
            static_cast<std::uint32_t>(actualVtable));
        return false;
    }

    std::uintptr_t actualTarget = 0;
    const std::uintptr_t expectedTarget = active.ImageBase + target.Rva;
    if (!ReadLocalMemory(
            actualVtable +
                static_cast<std::uintptr_t>(target.VtableSlot) *
                    sizeof(std::uintptr_t),
            &actualTarget,
            sizeof(actualTarget)) ||
        actualTarget != expectedTarget) {
        Reject(
            ProbeDiagnosticReason::InvalidVtableSlot,
            profileIndex,
            targetIndex,
            active.ImageBase,
            target.Rva,
            static_cast<std::uint32_t>(expectedTarget),
            static_cast<std::uint32_t>(actualTarget));
        return false;
    }
    return true;
}

ProbeActivationOutcome ProbeActivationGate::ValidateAndInstall(
    const ActiveBinaryProfile& active,
    std::uint32_t profileIndex,
    const BinaryTargetProfile& target,
    std::uint32_t targetIndex,
    ProbeInstallCallback install,
    void* installContext) const noexcept {
    if (!ValidateTarget(
            active,
            profileIndex,
            target,
            targetIndex)) {
        return ProbeActivationOutcome::Rejected;
    }
    if (install == nullptr ||
        !install(active, target, installContext)) {
        return ProbeActivationOutcome::InstallFailed;
    }
    return ProbeActivationOutcome::Installed;
}

void ProbeActivationGate::RejectUnknownHash(
    std::uint32_t profileIndex,
    std::uintptr_t imageBase,
    std::uint32_t mismatchFlags) const noexcept {
    Reject(
        ProbeDiagnosticReason::UnknownHash,
        profileIndex,
        NoTarget,
        imageBase,
        0,
        0,
        mismatchFlags);
}

void ProbeActivationGate::RejectMissingModule(
    std::uint32_t profileIndex) const noexcept {
    Reject(
        ProbeDiagnosticReason::MissingModule,
        profileIndex,
        NoTarget,
        0,
        0,
        1,
        0);
}

bool ProbeActivationGate::SupportsSchema(
    const RecordSchemaSupport& schema) const noexcept {
    for (std::size_t index = 0;
         index < CompiledSchemaCount_;
         ++index) {
        if (CompiledSchemas_[index].RecordId == schema.RecordId &&
            CompiledSchemas_[index].SchemaVersion ==
                schema.SchemaVersion) {
            return true;
        }
    }
    return false;
}

void ProbeActivationGate::Reject(
    ProbeDiagnosticReason reason,
    std::uint32_t profileIndex,
    std::uint32_t targetIndex,
    std::uintptr_t imageBase,
    std::uint32_t targetRva,
    std::uint32_t expected,
    std::uint32_t observed) const noexcept {
    if (Sink_ == nullptr) {
        return;
    }
    const ProbeValidationDiagnostic diagnostic{
        reason,
        profileIndex,
        targetIndex,
        imageBase,
        targetRva,
        expected,
        observed,
    };
    Sink_(diagnostic, SinkContext_);
}

}  // namespace elysium::capture
