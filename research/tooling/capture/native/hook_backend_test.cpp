#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "hook_backend.h"

namespace {

using elysium::capture::ActiveBinaryProfile;
using elysium::capture::BinaryProfile;
using elysium::capture::BinaryTargetKind;
using elysium::capture::BinaryTargetProfile;
using elysium::capture::CallingConvention;
using elysium::capture::HookBackendKind;
using elysium::capture::HookBackendResult;
using elysium::capture::HookBackends;
using elysium::capture::HookHandle;

using TestFunction = int(__stdcall*)(int);

int __stdcall Replacement(int value) noexcept {
    return value + 1000;
}

int Fail(const wchar_t* message, int result) {
    std::fwprintf(stderr, L"hook backend test failed: %ls\n", message);
    return result;
}

BinaryTargetProfile InlineDeclaration(
    std::uint32_t rva,
    const std::uint8_t* expected,
    std::uint32_t expectedBytes) {
    return {
        "synthetic.inline",
        BinaryTargetKind::Inline,
        HookBackendKind::InlineDetour,
        CallingConvention::Stdcall,
        rva,
        expected,
        expectedBytes,
        0,
        0,
        0,
        nullptr,
        0,
    };
}

}  // namespace

int wmain() {
    constexpr std::uint32_t imageSize = 0x1000;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        imageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (image == nullptr) {
        return Fail(L"cannot allocate synthetic executable image", 2);
    }
    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(image);
    const BinaryProfile profile{
        "synthetic-hook-backends",
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
        nullptr,
        0,
        nullptr,
        0,
    };
    const ActiveBinaryProfile active{&profile, base, imageSize};

    constexpr std::uint32_t inlineRva = 0x100;
    constexpr std::uint8_t inlineCode[] = {
        0x55,                   // push ebp
        0x8b, 0xec,             // mov ebp, esp
        0x8b, 0x45, 0x08,       // mov eax, [ebp+8]
        0x83, 0xc0, 0x07,       // add eax, 7
        0x5d,                   // pop ebp
        0xc2, 0x04, 0x00,       // ret 4
    };
    std::memcpy(image + inlineRva, inlineCode, sizeof(inlineCode));
    FlushInstructionCache(
        GetCurrentProcess(), image + inlineRva, sizeof(inlineCode));
    const BinaryTargetProfile inlineDeclaration = InlineDeclaration(
        inlineRva,
        inlineCode,
        8);
    const auto inlineTarget = reinterpret_cast<TestFunction>(
        image + inlineRva);
    if (inlineTarget(10) != 17) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"synthetic inline target is invalid", 3);
    }
    HookHandle inlineHandle{};
    if (HookBackends::Install(
            active,
            inlineDeclaration,
            reinterpret_cast<void*>(&Replacement),
            &inlineHandle) != HookBackendResult::Installed ||
        inlineHandle.PatchBytes != 6 ||
        inlineTarget(10) != 1010 ||
        reinterpret_cast<TestFunction>(inlineHandle.Original)(10) != 17) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"whole-instruction inline detour failed", 4);
    }
    if (HookBackends::Disable(&inlineHandle) !=
            HookBackendResult::Disabled ||
        inlineTarget(10) != 17 ||
        reinterpret_cast<TestFunction>(inlineHandle.Original)(10) != 17 ||
        HookBackends::Release(&inlineHandle) !=
            HookBackendResult::Released) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"inline detour disable/release failed", 5);
    }

    constexpr std::uint32_t relativeRva = 0x200;
    constexpr std::uint32_t helperRva = 0x280;
    constexpr std::uint8_t helperCode[] = {
        0xb8, 0x2a, 0x00, 0x00, 0x00,  // mov eax, 42
        0xc3,                          // ret
    };
    std::memcpy(image + helperRva, helperCode, sizeof(helperCode));
    image[relativeRva] = 0xe8;
    const std::uint32_t callDisplacement =
        helperRva - (relativeRva + 5);
    std::memcpy(
        image + relativeRva + 1,
        &callDisplacement,
        sizeof(callDisplacement));
    image[relativeRva + 5] = 0xc2;
    image[relativeRva + 6] = 0x04;
    image[relativeRva + 7] = 0x00;
    FlushInstructionCache(
        GetCurrentProcess(), image + relativeRva, 8);
    std::array<std::uint8_t, 5> relativeExpected{};
    std::memcpy(
        relativeExpected.data(),
        image + relativeRva,
        relativeExpected.size());
    const BinaryTargetProfile relativeDeclaration = InlineDeclaration(
        relativeRva,
        relativeExpected.data(),
        static_cast<std::uint32_t>(relativeExpected.size()));
    HookHandle relativeHandle{};
    const auto relativeTarget = reinterpret_cast<TestFunction>(
        image + relativeRva);
    if (relativeTarget(0) != 42 ||
        HookBackends::Install(
            active,
            relativeDeclaration,
            reinterpret_cast<void*>(&Replacement),
            &relativeHandle) != HookBackendResult::Installed ||
        relativeTarget(9) != 1009 ||
        reinterpret_cast<TestFunction>(relativeHandle.Original)(0) != 42 ||
        HookBackends::Disable(&relativeHandle) !=
            HookBackendResult::Disabled ||
        HookBackends::Release(&relativeHandle) !=
            HookBackendResult::Released) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"relative call relocation failed", 6);
    }

    constexpr std::uint32_t unsupportedRva = 0x300;
    constexpr std::uint8_t unsupportedCode[] = {
        0xe2, 0xfe,              // loop: target itself
        0x90, 0x90, 0x90, 0xc3,
    };
    std::memcpy(
        image + unsupportedRva,
        unsupportedCode,
        sizeof(unsupportedCode));
    const BinaryTargetProfile unsupportedDeclaration = InlineDeclaration(
        unsupportedRva,
        unsupportedCode,
        5);
    HookHandle unsupportedHandle{};
    if (HookBackends::Install(
            active,
            unsupportedDeclaration,
            reinterpret_cast<void*>(&Replacement),
            &unsupportedHandle) !=
            HookBackendResult::UnsupportedInstruction ||
        std::memcmp(
            image + unsupportedRva,
            unsupportedCode,
            sizeof(unsupportedCode)) != 0 ||
        unsupportedHandle.Installed) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"unsupported instruction did not fail closed", 7);
    }

    constexpr std::uint32_t conditionalRva = 0x340;
    constexpr std::uint8_t conditionalCode[] = {
        0x31, 0xc0,                         // xor eax, eax
        0x85, 0xc0,                         // test eax, eax
        0x74, 0x06,                         // jz target+12
        0xb8, 0x01, 0x00, 0x00, 0x00,       // mov eax, 1
        0xc3,                               // ret
        0xb8, 0x2a, 0x00, 0x00, 0x00,       // mov eax, 42
        0xc2, 0x04, 0x00,                   // ret 4
    };
    std::memcpy(
        image + conditionalRva,
        conditionalCode,
        sizeof(conditionalCode));
    FlushInstructionCache(
        GetCurrentProcess(),
        image + conditionalRva,
        sizeof(conditionalCode));
    const BinaryTargetProfile conditionalDeclaration = InlineDeclaration(
        conditionalRva,
        conditionalCode,
        6);
    HookHandle conditionalHandle{};
    const auto conditionalTarget = reinterpret_cast<TestFunction>(
        image + conditionalRva);
    if (conditionalTarget(0) != 42 ||
        HookBackends::Install(
            active,
            conditionalDeclaration,
            reinterpret_cast<void*>(&Replacement),
            &conditionalHandle) != HookBackendResult::Installed ||
        conditionalTarget(4) != 1004 ||
        reinterpret_cast<TestFunction>(conditionalHandle.Original)(0) != 42 ||
        HookBackends::Disable(&conditionalHandle) !=
            HookBackendResult::Disabled ||
        HookBackends::Release(&conditionalHandle) !=
            HookBackendResult::Released) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"short conditional relocation failed", 8);
    }

    constexpr std::uint32_t vtableTargetRva = 0x400;
    constexpr std::uint32_t objectRva = 0x500;
    constexpr std::uint32_t vtableRva = 0x600;
    constexpr std::uint32_t vtableSlot = 3;
    std::memcpy(
        image + vtableTargetRva,
        helperCode,
        sizeof(helperCode));
    const std::uintptr_t vtableAddress = base + vtableRva;
    std::memcpy(
        image + objectRva,
        &vtableAddress,
        sizeof(vtableAddress));
    const std::uintptr_t vtableTargetAddress =
        base + vtableTargetRva;
    std::memcpy(
        image + vtableRva + vtableSlot * sizeof(void*),
        &vtableTargetAddress,
        sizeof(vtableTargetAddress));
    const BinaryTargetProfile vtableDeclaration{
        "synthetic.vtable",
        BinaryTargetKind::Vtable,
        HookBackendKind::VtableReplacement,
        CallingConvention::Stdcall,
        vtableTargetRva,
        helperCode,
        static_cast<std::uint32_t>(sizeof(helperCode)),
        objectRva,
        vtableRva,
        vtableSlot,
        nullptr,
        0,
    };
    HookHandle vtableHandle{};
    auto** vtable = reinterpret_cast<void**>(image + vtableRva);
    if (HookBackends::Install(
            active,
            vtableDeclaration,
            reinterpret_cast<void*>(&Replacement),
            &vtableHandle) != HookBackendResult::Installed ||
        vtable[vtableSlot] != reinterpret_cast<void*>(&Replacement) ||
        vtableHandle.Original !=
            reinterpret_cast<void*>(vtableTargetAddress) ||
        HookBackends::Disable(&vtableHandle) !=
            HookBackendResult::Disabled ||
        vtable[vtableSlot] !=
            reinterpret_cast<void*>(vtableTargetAddress) ||
        HookBackends::Release(&vtableHandle) !=
            HookBackendResult::Released) {
        VirtualFree(image, 0, MEM_RELEASE);
        return Fail(L"vtable replacement backend failed", 9);
    }

    VirtualFree(image, 0, MEM_RELEASE);
    std::wprintf(
        L"hook-backend-test-v1 state=complete inline_patch_bytes=6 "
        L"relative_relocations=1 short_branch_expansions=1 "
        L"unsupported_rejections=1 "
        L"vtable_replacements=1\n");
    return 0;
}
