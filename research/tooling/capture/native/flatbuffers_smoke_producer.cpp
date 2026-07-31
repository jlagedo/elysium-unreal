#include "flatbuffers_smoke_generated.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: flatbuffers_smoke_producer <output>\n";
        return 2;
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto label = builder.CreateString("capture-smoke");
    constexpr std::array<float, 3> values = {1.25F, -2.5F, 3.75F};
    const auto value_vector = builder.CreateVector(values.data(), values.size());
    const auto record = Elysium::Capture::Smoke::CreateSmokeRecord(
        builder,
        UINT64_C(0x1020304050607080),
        label,
        value_vector
    );
    Elysium::Capture::Smoke::FinishSmokeRecordBuffer(builder, record);

    flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
    if (!Elysium::Capture::Smoke::VerifySmokeRecordBuffer(verifier)) {
        std::cerr << "generated smoke buffer failed C++ verification\n";
        return 3;
    }

    std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "could not open smoke output\n";
        return 4;
    }
    output.write(
        reinterpret_cast<const char*>(builder.GetBufferPointer()),
        static_cast<std::streamsize>(builder.GetSize())
    );
    if (!output) {
        std::cerr << "could not write smoke output\n";
        return 5;
    }
    std::cout << "flatbuffers-smoke-producer-v1 bytes=" << builder.GetSize() << '\n';
    return 0;
}
