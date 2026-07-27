// The single translation unit that compiles dr_wav's implementation. Kept isolated from
// the decode logic (ElysiumSoundCache.cpp includes the header declarations-only) so the
// large public-domain C header is emitted exactly once, and so a unity build never merges
// two copies of DR_WAV_IMPLEMENTATION. dr_wav decodes VtMB's Microsoft ADPCM (tag 0x02),
// IMA/DVI ADPCM (0x11) and PCM16 WAVs natively -- no offline transcode (docs/audio_pipeline.md).
#include "CoreMinimal.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO   // memory-only: bytes come from FFileHelper, never a C FILE*
#define DR_WAV_NO_WCHAR   // no wide-char file APIs are used

THIRD_PARTY_INCLUDES_START
#include "dr_wav.h"
THIRD_PARTY_INCLUDES_END
