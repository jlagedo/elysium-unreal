#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>

#include "probe_validation.h"

namespace {

using elysium::capture::ActiveBinaryProfile;
using elysium::capture::BinaryProfile;
using elysium::capture::BinaryTargetKind;
using elysium::capture::BinaryTargetProfile;
using elysium::capture::CallingConvention;
using elysium::capture::ProbeActivationGate;
using elysium::capture::ProbeActivationOutcome;
using elysium::capture::ProbeDiagnosticReason;
using elysium::capture::ProbeValidationDiagnostic;
constexpr std::uint8_t ExpectedBytes[] = {
    0x83, 0xec, 0x08, 0x53,
};

struct DiagnosticLog {
    std::array<ProbeValidationDiagnostic, 8> Records{};
    std::size_t Count = 0;
};

void CaptureDiagnostic(
    const ProbeValidationDiagnostic& diagnostic,
    void* context) noexcept {
    auto* log = static_cast<DiagnosticLog*>(context);
    if (log->Count < log->Records.size()) {
        log->Records[log->Count] = diagnostic;
    }
    ++log->Count;
}

bool CountInstall(
    const ActiveBinaryProfile&,
    const BinaryTargetProfile&,
    void* context) noexcept {
    auto* installCount = static_cast<std::uint32_t*>(context);
    ++*installCount;
    return true;
}

int Fail(const wchar_t* message, int result) {
    std::fwprintf(
        stderr,
        L"probe validation test failed: %ls\n",
        message);
    return result;
}

}  // namespace

int wmain() {
    constexpr std::uint32_t imageSize = 0x4000;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        imageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    if (image == nullptr) {
        return Fail(L"cannot allocate synthetic image", 2);
    }
    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(image);
    constexpr std::uint32_t targetRva = 0x100;
    constexpr std::uint32_t objectRva = 0x200;
    constexpr std::uint32_t vtableRva = 0x300;
    constexpr std::uint32_t vtableSlot = 2;
    std::memcpy(
        image + targetRva,
        ExpectedBytes,
        sizeof(ExpectedBytes));
    const std::uintptr_t vtable = base + vtableRva;
    std::memcpy(
        image + objectRva,
        &vtable,
        sizeof(vtable));
    const std::uintptr_t targetAddress = base + targetRva;
    std::memcpy(
        image + vtableRva +
            vtableSlot * sizeof(std::uintptr_t),
        &targetAddress,
        sizeof(targetAddress));

    const BinaryTargetProfile target{
        "synthetic.draw_model",
        BinaryTargetKind::Vtable,
        elysium::capture::HookBackendKind::VtableReplacement,
        CallingConvention::Thiscall,
        targetRva,
        ExpectedBytes,
        sizeof(ExpectedBytes),
        objectRva,
        vtableRva,
        vtableSlot,
    };
    const BinaryProfile profile{
        "synthetic",
        L"synthetic.dll",
        0,
        {},
        {
            IMAGE_FILE_MACHINE_I386,
            IMAGE_NT_OPTIONAL_HDR32_MAGIC,
            1,
            0,
            0,
            0,
            imageSize,
            0,
        },
        &target,
        1,
    };
    const ActiveBinaryProfile active{
        &profile,
        base,
        imageSize,
    };

    DiagnosticLog diagnostics{};
    const ProbeActivationGate gate(
        CaptureDiagnostic,
        &diagnostics);
    std::uint32_t installCount = 0;
    if (gate.ValidateAndInstall(
            active,
            3,
            target,
            0,
            CountInstall,
            &installCount) != ProbeActivationOutcome::Installed ||
        installCount != 1 ||
        diagnostics.Count != 0) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"valid target did not pass the activation gate", 3);
    }

    image[targetRva] ^= 0xff;
    const ProbeActivationOutcome prologueResult =
        gate.ValidateAndInstall(
            active,
            3,
            target,
            0,
            CountInstall,
            &installCount);
    image[targetRva] ^= 0xff;
    if (prologueResult != ProbeActivationOutcome::Rejected ||
        installCount != 1 ||
        diagnostics.Count != 1 ||
        diagnostics.Records[0].Reason !=
            ProbeDiagnosticReason::UnexpectedPrologue) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"unexpected prologue was not fail-closed", 4);
    }

    const std::uintptr_t invalidTarget = base + targetRva + 1;
    std::memcpy(
        image + vtableRva +
            vtableSlot * sizeof(std::uintptr_t),
        &invalidTarget,
        sizeof(invalidTarget));
    const ProbeActivationOutcome vtableResult =
        gate.ValidateAndInstall(
            active,
            3,
            target,
            0,
            CountInstall,
            &installCount);
    std::memcpy(
        image + vtableRva +
            vtableSlot * sizeof(std::uintptr_t),
        &targetAddress,
        sizeof(targetAddress));
    if (vtableResult != ProbeActivationOutcome::Rejected ||
        installCount != 1 ||
        diagnostics.Count != 2 ||
        diagnostics.Records[1].Reason !=
            ProbeDiagnosticReason::InvalidVtableSlot) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"invalid vtable slot was not fail-closed", 5);
    }

    gate.RejectUnknownHash(1, base, 0x20);
    gate.RejectMissingModule(3);
    if (installCount != 1 ||
        diagnostics.Count != 4 ||
        diagnostics.Records[2].Reason !=
            ProbeDiagnosticReason::UnknownHash ||
        diagnostics.Records[3].Reason !=
            ProbeDiagnosticReason::MissingModule ||
        diagnostics.Records[3].TargetIndex !=
            std::numeric_limits<std::uint32_t>::max()) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(
            L"hash/module rejection diagnostics are incomplete",
            6);
    }

    VirtualFree(image, 0, MEM_RELEASE);
    std::wprintf(
        L"probe-validation-test-v1 state=complete "
        L"valid_installs=%u rejected_installs=0 diagnostics=%zu\n",
        installCount,
        diagnostics.Count);
    return 0;
}
