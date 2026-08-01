#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "hook_backend.h"

namespace elysium::capture {
namespace {

constexpr std::size_t MaximumInstructionBytes = 15;
constexpr std::size_t MaximumInstructions = 32;
constexpr std::size_t TrampolineCapacity = 128;

enum class RelativeKind {
    None,
    Rel32,
    ShortJump,
    ShortCondition,
};

struct DecodedInstruction {
    std::uint8_t Length = 0;
    std::uint8_t PrefixBytes = 0;
    std::uint8_t RelativeOffset = 0;
    std::uint8_t RelativeBytes = 0;
    std::uint8_t Condition = 0;
    RelativeKind Relative = RelativeKind::None;
};

struct RelocatedInstruction {
    DecodedInstruction Decoded{};
    std::uint8_t SourceOffset = 0;
    std::uint8_t TrampolineOffset = 0;
    std::uint8_t TrampolineBytes = 0;
};

bool HasOneByteModRm(std::uint8_t opcode) noexcept {
    if ((opcode <= 0x3b && (opcode & 0x07) <= 0x03) ||
        (opcode >= 0x80 && opcode <= 0x8f) ||
        (opcode >= 0xd8 && opcode <= 0xdf)) {
        return true;
    }
    switch (opcode) {
        case 0x63:
        case 0x69:
        case 0x6b:
        case 0xc0:
        case 0xc1:
        case 0xc6:
        case 0xc7:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3:
        case 0xf6:
        case 0xf7:
        case 0xfe:
        case 0xff:
            return true;
        default:
            return false;
    }
}

bool HasTwoByteModRm(std::uint8_t opcode) noexcept {
    if ((opcode >= 0x10 && opcode <= 0x1f) ||
        (opcode >= 0x28 && opcode <= 0x2f) ||
        (opcode >= 0x40 && opcode <= 0x6f) ||
        (opcode >= 0x90 && opcode <= 0x9f) ||
        (opcode >= 0xb0 && opcode <= 0xbf) ||
        (opcode >= 0xc0 && opcode <= 0xc7)) {
        return true;
    }
    switch (opcode) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0xa3:
        case 0xa4:
        case 0xa5:
        case 0xab:
        case 0xac:
        case 0xad:
        case 0xaf:
            return true;
        default:
            return false;
    }
}

bool IsSupportedOneByteNoOperand(std::uint8_t opcode) noexcept {
    if ((opcode >= 0x40 && opcode <= 0x61) ||
        (opcode >= 0x90 && opcode <= 0x99) ||
        (opcode >= 0xec && opcode <= 0xef) ||
        (opcode >= 0xf8 && opcode <= 0xfd)) {
        return true;
    }
    switch (opcode) {
        case 0x06:
        case 0x07:
        case 0x0e:
        case 0x16:
        case 0x17:
        case 0x1e:
        case 0x1f:
        case 0x27:
        case 0x2f:
        case 0x37:
        case 0x3f:
        case 0x9b:
        case 0x9c:
        case 0x9d:
        case 0x9e:
        case 0x9f:
        case 0xc3:
        case 0xc9:
        case 0xcb:
        case 0xcc:
        case 0xce:
        case 0xcf:
        case 0xd6:
        case 0xd7:
        case 0xf1:
        case 0xf4:
        case 0xf5:
            return true;
        default:
            return false;
    }
}

bool IsSupportedTwoByteNoOperand(std::uint8_t opcode) noexcept {
    switch (opcode) {
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0b:
        case 0x31:
        case 0x34:
        case 0x35:
        case 0x37:
        case 0x77:
        case 0xa0:
        case 0xa1:
        case 0xa2:
        case 0xa8:
        case 0xa9:
        case 0xaa:
            return true;
        default:
            return false;
    }
}

bool ConsumeModRm(
    const std::uint8_t* code,
    std::size_t available,
    std::size_t* cursor,
    std::uint8_t* reg) noexcept {
    if (*cursor >= available) {
        return false;
    }
    const std::uint8_t modRm = code[(*cursor)++];
    const std::uint8_t mod = modRm >> 6;
    const std::uint8_t rm = modRm & 7;
    *reg = (modRm >> 3) & 7;
    if (mod != 3 && rm == 4) {
        if (*cursor >= available) {
            return false;
        }
        const std::uint8_t sib = code[(*cursor)++];
        if (mod == 0 && (sib & 7) == 5) {
            *cursor += 4;
        }
    } else if (mod == 0 && rm == 5) {
        *cursor += 4;
    }
    if (mod == 1) {
        ++*cursor;
    } else if (mod == 2) {
        *cursor += 4;
    }
    return *cursor <= available;
}

bool DecodeInstruction(
    const std::uint8_t* code,
    std::size_t available,
    DecodedInstruction* decoded) noexcept {
    if (code == nullptr || decoded == nullptr || available == 0) {
        return false;
    }
    available = (std::min)(available, MaximumInstructionBytes);
    std::size_t cursor = 0;
    bool operand16 = false;
    bool address16 = false;
    while (cursor < available) {
        const std::uint8_t prefix = code[cursor];
        if (prefix == 0x66) {
            operand16 = true;
        } else if (prefix == 0x67) {
            address16 = true;
        } else if (prefix != 0xf0 && prefix != 0xf2 && prefix != 0xf3 &&
                   prefix != 0x2e && prefix != 0x36 && prefix != 0x3e &&
                   prefix != 0x26 && prefix != 0x64 && prefix != 0x65) {
            break;
        }
        ++cursor;
    }
    if (cursor >= available || address16) {
        return false;
    }
    decoded->PrefixBytes = static_cast<std::uint8_t>(cursor);
    const std::uint8_t opcode = code[cursor++];
    const std::size_t operandBytes = operand16 ? 2 : 4;
    std::size_t immediateBytes = 0;
    bool hasModRm = false;
    bool supported = false;

    if (opcode == 0x0f) {
        if (cursor >= available) {
            return false;
        }
        const std::uint8_t second = code[cursor++];
        if (second >= 0x80 && second <= 0x8f) {
            if (operand16) {
                return false;
            }
            decoded->Relative = RelativeKind::Rel32;
            decoded->RelativeOffset = static_cast<std::uint8_t>(cursor);
            decoded->RelativeBytes = 4;
            decoded->Condition = second & 0x0f;
            immediateBytes = 4;
            supported = true;
        } else if (second == 0x70 ||
                   (second >= 0x71 && second <= 0x73) ||
                   second == 0xa4 || second == 0xac ||
                   second == 0xba || second == 0xc2 ||
                   second == 0xc4 || second == 0xc5 ||
                   second == 0xc6) {
            hasModRm = true;
            immediateBytes = 1;
            supported = true;
        } else if (HasTwoByteModRm(second)) {
            hasModRm = true;
            supported = true;
        } else {
            supported = IsSupportedTwoByteNoOperand(second);
        }
    } else if (opcode == 0xe8 || opcode == 0xe9) {
        if (operand16) {
            return false;
        }
        decoded->Relative = RelativeKind::Rel32;
        decoded->RelativeOffset = static_cast<std::uint8_t>(cursor);
        decoded->RelativeBytes = 4;
        immediateBytes = 4;
        supported = true;
    } else if (opcode == 0xeb) {
        if (decoded->PrefixBytes != 0) {
            return false;
        }
        decoded->Relative = RelativeKind::ShortJump;
        decoded->RelativeOffset = static_cast<std::uint8_t>(cursor);
        decoded->RelativeBytes = 1;
        immediateBytes = 1;
        supported = true;
    } else if (opcode >= 0x70 && opcode <= 0x7f) {
        if (decoded->PrefixBytes != 0) {
            return false;
        }
        decoded->Relative = RelativeKind::ShortCondition;
        decoded->RelativeOffset = static_cast<std::uint8_t>(cursor);
        decoded->RelativeBytes = 1;
        decoded->Condition = opcode & 0x0f;
        immediateBytes = 1;
        supported = true;
    } else if (opcode >= 0xe0 && opcode <= 0xe3) {
        return false;
    } else if (HasOneByteModRm(opcode)) {
        hasModRm = true;
        supported = true;
        if (opcode == 0x69 || opcode == 0x81 || opcode == 0xc7) {
            immediateBytes = operandBytes;
        } else if (opcode == 0x6b || opcode == 0x80 ||
                   opcode == 0x82 || opcode == 0x83 ||
                   opcode == 0xc0 || opcode == 0xc1 ||
                   opcode == 0xc6) {
            immediateBytes = 1;
        }
    } else if ((opcode & 0xf8) == 0xb0) {
        immediateBytes = 1;
        supported = true;
    } else if ((opcode & 0xf8) == 0xb8) {
        immediateBytes = operandBytes;
        supported = true;
    } else if ((opcode <= 0x3d && (opcode & 0x07) == 0x04) ||
               opcode == 0x6a || opcode == 0xa8 ||
               (opcode >= 0xe4 && opcode <= 0xe7) ||
               opcode == 0xcd || opcode == 0xd4 || opcode == 0xd5) {
        immediateBytes = 1;
        supported = true;
    } else if ((opcode <= 0x3d && (opcode & 0x07) == 0x05) ||
               opcode == 0x68 || opcode == 0xa9) {
        immediateBytes = operandBytes;
        supported = true;
    } else if (opcode >= 0xa0 && opcode <= 0xa3) {
        immediateBytes = 4;
        supported = true;
    } else if (opcode == 0xc2 || opcode == 0xca) {
        immediateBytes = 2;
        supported = true;
    } else if (opcode == 0xc8) {
        immediateBytes = 3;
        supported = true;
    } else if (opcode == 0x9a || opcode == 0xea) {
        immediateBytes = operand16 ? 4 : 6;
        supported = true;
    } else {
        supported = IsSupportedOneByteNoOperand(opcode);
    }
    if (!supported) {
        return false;
    }

    std::uint8_t modRmReg = 0;
    if (hasModRm &&
        !ConsumeModRm(code, available, &cursor, &modRmReg)) {
        return false;
    }
    if (opcode == 0xf6 && modRmReg <= 1) {
        immediateBytes = 1;
    } else if (opcode == 0xf7 && modRmReg <= 1) {
        immediateBytes = operandBytes;
    }
    if (cursor + immediateBytes > available ||
        cursor + immediateBytes > MaximumInstructionBytes) {
        return false;
    }
    cursor += immediateBytes;
    decoded->Length = static_cast<std::uint8_t>(cursor);
    return true;
}

std::uint32_t RelativeDestination(
    const std::uint8_t* instruction,
    std::uintptr_t instructionAddress,
    const DecodedInstruction& decoded) noexcept {
    std::int32_t displacement = 0;
    if (decoded.RelativeBytes == 1) {
        std::int8_t shortDisplacement = 0;
        std::memcpy(
            &shortDisplacement,
            instruction + decoded.RelativeOffset,
            sizeof(shortDisplacement));
        displacement = shortDisplacement;
    } else {
        std::memcpy(
            &displacement,
            instruction + decoded.RelativeOffset,
            sizeof(displacement));
    }
    return static_cast<std::uint32_t>(
        instructionAddress + decoded.Length + displacement);
}

void WriteRelative32(
    std::uint8_t* output,
    std::uint32_t destination,
    std::uintptr_t nextInstruction) noexcept {
    const std::uint32_t displacement =
        destination - static_cast<std::uint32_t>(nextInstruction);
    std::memcpy(output, &displacement, sizeof(displacement));
}

bool IsImageRangeValid(
    const ActiveBinaryProfile& active,
    std::uint32_t rva,
    std::size_t bytes) noexcept {
    return rva < active.ImageSize &&
        bytes <= static_cast<std::size_t>(active.ImageSize - rva);
}

// Compare the declared prologue against memory, rebasing the one operand the
// loader is allowed to have rewritten.
//
// A declaration records the bytes as they appear in the file. When the module
// is not at its preferred base, an absolute address encoded in the prologue is
// relocated, so those bytes cannot match and a plain comparison would reject a
// target that is exactly what was declared. Adding the load delta to the
// declared operand keeps the check exact rather than masking the bytes away.
bool MatchesDeclaredPrologue(
    const ActiveBinaryProfile& active,
    const BinaryTargetProfile& declaration,
    const std::uint8_t* target) noexcept {
    const std::uint32_t size = declaration.RelocatedOperandSize;
    if (size == 0) {
        return std::memcmp(
            target,
            declaration.ExpectedBytes,
            declaration.ExpectedByteCount) == 0;
    }
    const std::uint32_t offset = declaration.RelocatedOperandOffset;
    if (size != 2 && size != 4) {
        return false;
    }
    if (offset > declaration.ExpectedByteCount ||
        declaration.ExpectedByteCount - offset < size) {
        return false;
    }
    if (offset != 0 &&
        std::memcmp(target, declaration.ExpectedBytes, offset) != 0) {
        return false;
    }
    const std::uint32_t tail = offset + size;
    if (tail < declaration.ExpectedByteCount &&
        std::memcmp(
            target + tail,
            declaration.ExpectedBytes + tail,
            declaration.ExpectedByteCount - tail) != 0) {
        return false;
    }
    std::uint32_t declared = 0;
    std::uint32_t observed = 0;
    std::memcpy(&declared, declaration.ExpectedBytes + offset, size);
    std::memcpy(&observed, target + offset, size);
    const auto delta = static_cast<std::uint32_t>(
        active.ImageBase - active.Profile->Pe.PreferredImageBase);
    const std::uint32_t mask =
        size == 4 ? 0xffffffffu : 0x0000ffffu;
    return ((declared + delta) & mask) == (observed & mask);
}

bool WriteProtected(
    void* destination,
    const void* source,
    std::size_t bytes) noexcept {
    DWORD previousProtection = 0;
    if (!VirtualProtect(
            destination,
            bytes,
            PAGE_EXECUTE_READWRITE,
            &previousProtection)) {
        return false;
    }
    std::memcpy(destination, source, bytes);
    FlushInstructionCache(
        GetCurrentProcess(),
        destination,
        bytes);
    DWORD ignored = 0;
    VirtualProtect(
        destination,
        bytes,
        previousProtection,
        &ignored);
    // Once bytes are written the caller must receive ownership of the live
    // patch even if restoring page protection unexpectedly fails.
    return true;
}

HookBackendResult InstallVtable(
    const ActiveBinaryProfile& active,
    const BinaryTargetProfile& declaration,
    void* replacement,
    HookHandle* handle) noexcept {
    const std::uint64_t slotRva =
        static_cast<std::uint64_t>(declaration.ExpectedVtableRva) +
        static_cast<std::uint64_t>(declaration.VtableSlot) * sizeof(void*);
    if (!IsImageRangeValid(
            active,
            declaration.ObjectRva,
            sizeof(void*)) ||
        slotRva > (std::numeric_limits<std::uint32_t>::max)() ||
        !IsImageRangeValid(
            active,
            static_cast<std::uint32_t>(slotRva),
            sizeof(void*))) {
        return HookBackendResult::InvalidDeclaration;
    }
    auto*** object = reinterpret_cast<void***>(
        active.ImageBase + declaration.ObjectRva);
    void** vtable = *object;
    void** expectedVtable = reinterpret_cast<void**>(
        active.ImageBase + declaration.ExpectedVtableRva);
    if (vtable != expectedVtable) {
        return HookBackendResult::TargetChanged;
    }
    void** slot = &vtable[declaration.VtableSlot];
    void* expectedTarget = reinterpret_cast<void*>(
        active.ImageBase + declaration.Rva);
    if (*slot != expectedTarget) {
        return HookBackendResult::TargetChanged;
    }
    DWORD previousProtection = 0;
    if (!VirtualProtect(
            slot,
            sizeof(void*),
            PAGE_EXECUTE_READWRITE,
            &previousProtection)) {
        return HookBackendResult::ProtectionFailed;
    }
    void* original = InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot),
        replacement,
        expectedTarget);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(
        slot,
        sizeof(void*),
        previousProtection,
        &ignored);
    if (original != expectedTarget) {
        return HookBackendResult::TargetChanged;
    }
    static_cast<void>(restored);
    handle->Backend = declaration.Backend;
    handle->Target = expectedTarget;
    handle->Replacement = replacement;
    handle->Original = original;
    handle->VtableSlot = slot;
    handle->Installed = true;
    handle->Enabled = true;
    return HookBackendResult::Installed;
}

HookBackendResult InstallInline(
    const ActiveBinaryProfile& active,
    const BinaryTargetProfile& declaration,
    void* replacement,
    HookHandle* handle) noexcept {
    if (declaration.ExpectedBytes == nullptr ||
        declaration.ExpectedByteCount == 0 ||
        !IsImageRangeValid(
            active,
            declaration.Rva,
            declaration.ExpectedByteCount)) {
        return HookBackendResult::InvalidDeclaration;
    }
    auto* target = reinterpret_cast<std::uint8_t*>(
        active.ImageBase + declaration.Rva);
    if (!MatchesDeclaredPrologue(active, declaration, target)) {
        return HookBackendResult::TargetChanged;
    }

    std::array<RelocatedInstruction, MaximumInstructions> instructions{};
    std::size_t instructionCount = 0;
    std::size_t sourceBytes = 0;
    std::size_t trampolineBytes = 0;
    while (sourceBytes < 5) {
        if (instructionCount >= instructions.size() ||
            sourceBytes >= HookHandle::MaximumPatchBytes ||
            !IsImageRangeValid(active, declaration.Rva, sourceBytes + 1)) {
            return HookBackendResult::UnsupportedInstruction;
        }
        DecodedInstruction decoded{};
        const std::size_t available = (std::min)(
            MaximumInstructionBytes,
            static_cast<std::size_t>(active.ImageSize - declaration.Rva) -
                sourceBytes);
        if (!DecodeInstruction(
                target + sourceBytes,
                available,
                &decoded) ||
            sourceBytes + decoded.Length >
                HookHandle::MaximumPatchBytes) {
            return HookBackendResult::UnsupportedInstruction;
        }
        std::size_t relocatedBytes = decoded.Length;
        if (decoded.Relative == RelativeKind::ShortJump) {
            relocatedBytes = 5;
        } else if (decoded.Relative == RelativeKind::ShortCondition) {
            relocatedBytes = 6;
        }
        if (trampolineBytes + relocatedBytes + 5 >
            TrampolineCapacity) {
            return HookBackendResult::UnsupportedInstruction;
        }
        instructions[instructionCount] = {
            decoded,
            static_cast<std::uint8_t>(sourceBytes),
            static_cast<std::uint8_t>(trampolineBytes),
            static_cast<std::uint8_t>(relocatedBytes),
        };
        ++instructionCount;
        sourceBytes += decoded.Length;
        trampolineBytes += relocatedBytes;
    }
    if (sourceBytes > declaration.ExpectedByteCount) {
        return HookBackendResult::InvalidDeclaration;
    }

    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        TrampolineCapacity,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    if (trampoline == nullptr) {
        return HookBackendResult::AllocationFailed;
    }
    const std::uint32_t sourceStart =
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(target));
    for (std::size_t index = 0; index < instructionCount; ++index) {
        const RelocatedInstruction& item = instructions[index];
        const auto* source = target + item.SourceOffset;
        auto* output = trampoline + item.TrampolineOffset;
        if (item.Decoded.Relative == RelativeKind::None) {
            std::memcpy(output, source, item.Decoded.Length);
            continue;
        }
        std::uint32_t destination = RelativeDestination(
            source,
            reinterpret_cast<std::uintptr_t>(source),
            item.Decoded);
        if (destination >= sourceStart &&
            destination < sourceStart + sourceBytes) {
            const std::uint32_t destinationOffset =
                destination - sourceStart;
            bool found = false;
            for (std::size_t targetIndex = 0;
                 targetIndex < instructionCount;
                 ++targetIndex) {
                if (instructions[targetIndex].SourceOffset ==
                    destinationOffset) {
                    destination = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(trampoline) +
                        instructions[targetIndex].TrampolineOffset);
                    found = true;
                    break;
                }
            }
            if (!found) {
                VirtualFree(trampoline, 0, MEM_RELEASE);
                return HookBackendResult::UnsupportedInstruction;
            }
        }
        if (item.Decoded.Relative == RelativeKind::Rel32) {
            std::memcpy(output, source, item.Decoded.Length);
            WriteRelative32(
                output + item.Decoded.RelativeOffset,
                destination,
                reinterpret_cast<std::uintptr_t>(output) +
                    item.Decoded.Length);
        } else if (item.Decoded.Relative == RelativeKind::ShortJump) {
            output[0] = 0xe9;
            WriteRelative32(
                output + 1,
                destination,
                reinterpret_cast<std::uintptr_t>(output) + 5);
        } else {
            output[0] = 0x0f;
            output[1] = static_cast<std::uint8_t>(
                0x80 | item.Decoded.Condition);
            WriteRelative32(
                output + 2,
                destination,
                reinterpret_cast<std::uintptr_t>(output) + 6);
        }
    }
    auto* returnJump = trampoline + trampolineBytes;
    returnJump[0] = 0xe9;
    WriteRelative32(
        returnJump + 1,
        sourceStart + static_cast<std::uint32_t>(sourceBytes),
        reinterpret_cast<std::uintptr_t>(returnJump) + 5);
    DWORD previousProtection = 0;
    if (!VirtualProtect(
            trampoline,
            TrampolineCapacity,
            PAGE_EXECUTE_READ,
            &previousProtection) ||
        !FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            trampolineBytes + 5)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return HookBackendResult::ProtectionFailed;
    }

    std::array<std::uint8_t, HookHandle::MaximumPatchBytes> patch{};
    patch.fill(0x90);
    patch[0] = 0xe9;
    WriteRelative32(
        patch.data() + 1,
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(replacement)),
        reinterpret_cast<std::uintptr_t>(target) + 5);
    std::memcpy(handle->OriginalBytes, target, sourceBytes);
    if (!WriteProtected(target, patch.data(), sourceBytes)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return HookBackendResult::ProtectionFailed;
    }
    handle->Backend = declaration.Backend;
    handle->Target = target;
    handle->Replacement = replacement;
    handle->Original = trampoline;
    handle->Trampoline = trampoline;
    handle->PatchBytes = static_cast<std::uint32_t>(sourceBytes);
    handle->Installed = true;
    handle->Enabled = true;
    return HookBackendResult::Installed;
}

}  // namespace

HookBackendResult HookBackends::Install(
    const ActiveBinaryProfile& active,
    const BinaryTargetProfile& declaration,
    void* replacement,
    HookHandle* handle) noexcept {
    if (replacement == nullptr || handle == nullptr ||
        handle->Installed || handle->Enabled ||
        declaration.Backend == HookBackendKind::None) {
        return HookBackendResult::InvalidDeclaration;
    }
    if (declaration.ExpectedBytes == nullptr ||
        declaration.ExpectedByteCount == 0 ||
        !IsImageRangeValid(
            active,
            declaration.Rva,
            declaration.ExpectedByteCount)) {
        return HookBackendResult::InvalidDeclaration;
    }
    const auto* target = reinterpret_cast<const std::uint8_t*>(
        active.ImageBase + declaration.Rva);
    if (!MatchesDeclaredPrologue(active, declaration, target)) {
        return HookBackendResult::TargetChanged;
    }
    if (declaration.Backend == HookBackendKind::InlineDetour &&
        declaration.Kind == BinaryTargetKind::Inline) {
        return InstallInline(active, declaration, replacement, handle);
    }
    if (declaration.Backend == HookBackendKind::VtableReplacement &&
        declaration.Kind == BinaryTargetKind::Vtable) {
        return InstallVtable(active, declaration, replacement, handle);
    }
    return HookBackendResult::InvalidDeclaration;
}

HookBackendResult HookBackends::Disable(HookHandle* handle) noexcept {
    if (handle == nullptr || !handle->Installed) {
        return HookBackendResult::NotInstalled;
    }
    if (!handle->Enabled) {
        return HookBackendResult::Disabled;
    }
    if (handle->Backend == HookBackendKind::InlineDetour) {
        std::array<std::uint8_t, HookHandle::MaximumPatchBytes> patch{};
        patch.fill(0x90);
        patch[0] = 0xe9;
        WriteRelative32(
            patch.data() + 1,
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(handle->Replacement)),
            reinterpret_cast<std::uintptr_t>(handle->Target) + 5);
        if (std::memcmp(
                handle->Target,
                patch.data(),
                handle->PatchBytes) != 0) {
            return HookBackendResult::TargetChanged;
        }
        if (!WriteProtected(
                handle->Target,
                handle->OriginalBytes,
                handle->PatchBytes)) {
            return HookBackendResult::ProtectionFailed;
        }
    } else if (handle->Backend == HookBackendKind::VtableReplacement) {
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                handle->VtableSlot,
                sizeof(void*),
                PAGE_EXECUTE_READWRITE,
                &previousProtection)) {
            return HookBackendResult::ProtectionFailed;
        }
        void* replaced = InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(handle->VtableSlot),
            handle->Original,
            handle->Replacement);
        DWORD ignored = 0;
        const BOOL restored = VirtualProtect(
            handle->VtableSlot,
            sizeof(void*),
            previousProtection,
            &ignored);
        if (replaced != handle->Replacement) {
            return HookBackendResult::TargetChanged;
        }
        static_cast<void>(restored);
    } else {
        return HookBackendResult::InvalidDeclaration;
    }
    handle->Enabled = false;
    return HookBackendResult::Disabled;
}

HookBackendResult HookBackends::Release(HookHandle* handle) noexcept {
    if (handle == nullptr || !handle->Installed) {
        return HookBackendResult::NotInstalled;
    }
    if (handle->Enabled) {
        return HookBackendResult::StillEnabled;
    }
    if (handle->Trampoline != nullptr &&
        !VirtualFree(handle->Trampoline, 0, MEM_RELEASE)) {
        return HookBackendResult::ProtectionFailed;
    }
    *handle = {};
    return HookBackendResult::Released;
}

}  // namespace elysium::capture
