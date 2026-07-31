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
    const std::uint8_t* ExpectedBytes;
    std::uint32_t ExpectedByteCount;
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
