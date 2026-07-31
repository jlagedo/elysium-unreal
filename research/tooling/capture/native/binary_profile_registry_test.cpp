#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "binary_profile_registry.h"

namespace {

using elysium::capture::ActiveBinaryProfile;
using elysium::capture::BinaryProfile;
using elysium::capture::BinaryProfileMatchDiagnostic;
using elysium::capture::BinaryProfileRegistry;
using elysium::capture::BinaryTargetKind;
using elysium::capture::BinaryTargetProfile;
using elysium::capture::CallingConvention;
using elysium::capture::PeIdentity;
using elysium::capture::ProcessedModuleEvent;
using elysium::capture::ProfileActivationResult;
using elysium::capture::RecordSchemaSupport;

constexpr RecordSchemaSupport Schemas[] = {
    {4, 2},
};
constexpr std::uint8_t ExpectedBytes[] = {
    0x83, 0xec, 0x34, 0x53, 0x55,
};
constexpr BinaryTargetProfile Targets[] = {
    {
        "client.resolve_virtual_model_pose",
        BinaryTargetKind::Inline,
        elysium::capture::HookBackendKind::InlineDetour,
        CallingConvention::Thiscall,
        0x1000,
        ExpectedBytes,
        static_cast<std::uint32_t>(std::size(ExpectedBytes)),
        0,
        0,
        0,
        Schemas,
        static_cast<std::uint32_t>(std::size(Schemas)),
    },
};
constexpr BinaryProfile Profiles[] = {
    {
        "synthetic-client",
        L"client.dll",
        123456,
        {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        },
        {
            IMAGE_FILE_MACHINE_I386,
            IMAGE_NT_OPTIONAL_HDR32_MAGIC,
            4,
            0x210e,
            0x12345678,
            0x10000000,
            0x20000,
            0,
        },
        Schemas,
        static_cast<std::uint32_t>(std::size(Schemas)),
        Targets,
        static_cast<std::uint32_t>(std::size(Targets)),
    },
};

int Fail(const wchar_t* message, int result) {
    std::fwprintf(stderr, L"binary profile registry test failed: %ls\n", message);
    return result;
}

ProcessedModuleEvent MatchingEvent() {
    ProcessedModuleEvent event{};
    event.Kind = elysium::capture::ModuleEventKind::Loaded;
    event.ImageBase = 0x50000000;
    event.ImageSize = 0x20000;
    event.FileSize = 123456;
    event.Pe = Profiles[0].Pe;
    event.ModuleName = L"client.dll";
    event.ModuleNameCharacters = std::wcslen(event.ModuleName);
    event.Path = L"C:\\retail\\client.dll";
    event.PathCharacters = std::wcslen(event.Path);
    event.HashSucceeded = true;
    event.IdentitySucceeded = true;
    std::memcpy(
        event.Sha256.data(),
        Profiles[0].Sha256,
        event.Sha256.size());
    return event;
}

}  // namespace

int wmain() {
    BinaryProfileRegistry registry(Profiles, std::size(Profiles));
    ProcessedModuleEvent event = MatchingEvent();
    if (registry.ProfileCount() != 1 ||
        registry.Match(event) != &Profiles[0] ||
        registry.Activate(event) != ProfileActivationResult::Matched ||
        registry.ActiveCount() != 1 ||
        registry.Activate(event) != ProfileActivationResult::Duplicate) {
        return Fail(L"exact profile did not activate once", 2);
    }

    ActiveBinaryProfile active{};
    if (!registry.FindActive(event.ImageBase, &active) ||
        active.Profile != &Profiles[0] ||
        active.ImageBase != event.ImageBase ||
        active.ImageSize != event.ImageSize) {
        return Fail(L"active profile lost its runtime image identity", 3);
    }
    const BinaryTargetProfile* target =
        BinaryProfileRegistry::FindTarget(
            *active.Profile,
            "client.resolve_virtual_model_pose");
    if (target == nullptr ||
        target->Convention != CallingConvention::Thiscall ||
        target->SupportedSchemas[0].RecordId != 4 ||
        BinaryProfileRegistry::ResolveTarget(active, *target) !=
            event.ImageBase + 0x1000) {
        return Fail(L"target metadata or runtime RVA resolution is invalid", 4);
    }

    ProcessedModuleEvent wrongHash = MatchingEvent();
    wrongHash.Sha256[0] ^= 0xff;
    BinaryProfileMatchDiagnostic diagnostic{};
    if (registry.Match(wrongHash, &diagnostic) != nullptr ||
        diagnostic.Candidate != &Profiles[0] ||
        diagnostic.ProfileIndex != 0 ||
        diagnostic.MismatchFlags !=
            elysium::capture::ProfileMismatchSha256) {
        return Fail(L"SHA-256 mismatch selected a profile", 5);
    }
    ProcessedModuleEvent wrongPe = MatchingEvent();
    ++wrongPe.Pe.Timestamp;
    if (registry.Match(wrongPe, &diagnostic) != nullptr ||
        diagnostic.MismatchFlags !=
            elysium::capture::ProfileMismatchPeIdentity) {
        return Fail(L"PE identity mismatch selected a profile", 6);
    }
    ProcessedModuleEvent wrongSize = MatchingEvent();
    ++wrongSize.FileSize;
    if (registry.Match(wrongSize, &diagnostic) != nullptr ||
        diagnostic.MismatchFlags !=
            elysium::capture::ProfileMismatchFileSize) {
        return Fail(L"file-size mismatch selected a profile", 7);
    }
    ProcessedModuleEvent renamed = MatchingEvent();
    renamed.ModuleName = L"client-short-name.dll";
    renamed.ModuleNameCharacters = std::wcslen(renamed.ModuleName);
    renamed.Path = L"C:\\retail\\CLIENT~1.DLL";
    renamed.PathCharacters = std::wcslen(renamed.Path);
    if (registry.Match(renamed, &diagnostic) != &Profiles[0] ||
        diagnostic.Candidate != &Profiles[0] ||
        diagnostic.MismatchFlags !=
            elysium::capture::ProfileMismatchNone) {
        return Fail(L"exact content identity depended on a loader alias", 8);
    }

    if (!registry.Deactivate(event.ImageBase) ||
        registry.ActiveCount() != 0 ||
        registry.Deactivate(event.ImageBase)) {
        return Fail(L"profile unload state is invalid", 9);
    }
    std::wprintf(
        L"binary-profile-registry-test-v1 state=complete profiles=%zu "
        L"targets=%u schemas=%u\n",
        registry.ProfileCount(),
        Profiles[0].TargetCount,
        Profiles[0].SupportedSchemaCount);
    return 0;
}
