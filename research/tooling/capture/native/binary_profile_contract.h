#pragma once

#include <cstddef>
#include <cstdint>

namespace elysium::capture {

enum class BinaryTargetKind : std::uint32_t {
    Symbol = 1,
    Inline = 2,
    Vtable = 3,
};

enum class CallingConvention : std::uint32_t {
    Cdecl = 1,
    Stdcall = 2,
    Thiscall = 3,
    Fastcall = 4,
};

enum class HookBackendKind : std::uint32_t {
    None = 0,
    InlineDetour = 1,
    VtableReplacement = 2,
};

struct PeIdentity {
    std::uint16_t Machine;
    std::uint16_t OptionalMagic;
    std::uint16_t SectionCount;
    std::uint16_t Characteristics;
    std::uint32_t Timestamp;
    std::uint32_t PreferredImageBase;
    std::uint32_t SizeOfImage;
    std::uint32_t Checksum;
};

struct BinaryTargetProfile {
    const char* SemanticLabel;
    BinaryTargetKind Kind;
    HookBackendKind Backend;
    CallingConvention Convention;
    std::uint32_t Rva;
    // The declared prologue as it appears in the file on disk.
    const std::uint8_t* ExpectedBytes;
    std::uint32_t ExpectedByteCount;
    // A prologue may encode an absolute address the loader rewrites when the
    // module is not at its preferred base, in which case the declared bytes
    // never match memory. The operand is compared after adding the load delta
    // rather than being masked out, so the check still verifies every byte.
    // RelocatedOperandSize of zero means the prologue carries no such operand.
    std::uint32_t RelocatedOperandOffset;
    std::uint32_t RelocatedOperandSize;
    std::uint32_t ObjectRva;
    std::uint32_t ExpectedVtableRva;
    std::uint32_t VtableSlot;
};

struct BinaryProfile {
    const char* Id;
    const wchar_t* ModuleName;
    std::uint64_t FileSize;
    std::uint8_t Sha256[32];
    PeIdentity Pe;
    const BinaryTargetProfile* Targets;
    std::uint32_t TargetCount;
};

}  // namespace elysium::capture
