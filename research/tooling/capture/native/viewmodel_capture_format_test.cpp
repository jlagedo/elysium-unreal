#include "viewmodel_capture_format.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using elysium::capture::viewmodel::FileHeader;
using elysium::capture::viewmodel::Record;

bool Validate(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < sizeof(FileHeader)) {
        return false;
    }
    FileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (std::memcmp(header.Magic, "ELGVM1", 6) != 0 ||
        header.FormatVersion != elysium::capture::viewmodel::Version ||
        header.HeaderBytes != sizeof(FileHeader) ||
        header.RecordBytes != sizeof(Record)) {
        return false;
    }
    const std::size_t payload = bytes.size() - sizeof(FileHeader);
    if (payload % sizeof(Record) != 0) {
        return false;
    }
    for (std::size_t offset = sizeof(FileHeader); offset < bytes.size();
         offset += sizeof(Record)) {
        Record record{};
        std::memcpy(&record, bytes.data() + offset, sizeof(record));
        if (std::memcmp(record.Magic, "VMR1", 4) != 0 ||
            record.RecordBytes != sizeof(Record)) {
            return false;
        }
    }
    return true;
}

std::vector<unsigned char> Synthetic() {
    FileHeader header{};
    std::memcpy(header.Magic, "ELGVM1", 6);
    header.FormatVersion = elysium::capture::viewmodel::Version;
    header.HeaderBytes = sizeof(header);
    header.RecordBytes = sizeof(Record);
    Record first{};
    std::memcpy(first.Magic, "VMR1", 4);
    first.RecordBytes = sizeof(first);
    first.Ordinal = 1;
    Record second = first;
    second.Ordinal = 2;
    std::vector<unsigned char> bytes(
        sizeof(header) + sizeof(first) + sizeof(second));
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), &first, sizeof(first));
    std::memcpy(
        bytes.data() + sizeof(header) + sizeof(first), &second,
        sizeof(second));
    return bytes;
}

}  // namespace

int main() {
    auto bytes = Synthetic();
    if (!Validate(bytes)) {
        std::fprintf(stderr, "synthetic ELGVM1 stream was rejected\n");
        return 1;
    }
    auto truncated = bytes;
    truncated.pop_back();
    if (Validate(truncated)) {
        std::fprintf(stderr, "truncated ELGVM1 stream was accepted\n");
        return 2;
    }
    auto malformed = bytes;
    const std::uint32_t wrong = sizeof(Record) - 1;
    std::memcpy(
        malformed.data() + sizeof(FileHeader) + offsetof(Record, RecordBytes),
        &wrong, sizeof(wrong));
    if (Validate(malformed)) {
        std::fprintf(stderr, "malformed ELGVM1 record was accepted\n");
        return 3;
    }
    std::printf(
        "viewmodel-capture-format-test-v1 state=complete records=2 "
        "malformed_rejected=2\n");
    return 0;
}
