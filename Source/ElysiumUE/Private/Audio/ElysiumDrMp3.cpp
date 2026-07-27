// The single translation unit that compiles dr_mp3's implementation. Kept isolated from the
// decode logic (ElysiumSoundCache.cpp includes the header declarations-only), so the large
// public-domain C header is emitted exactly once and a unity build never merges two copies of
// DR_MP3_IMPLEMENTATION -- the same convention ElysiumDrWav.cpp establishes for dr_wav. dr_mp3
// decodes VtMB's loose dialogue/music/radio MP3s to int16 PCM at runtime -- no offline transcode
// (MP3 patents expired; docs/audio_pipeline.md). The runtime has no other MP3 route: Unreal only
// decodes cooked USoundWave assets, not loose .mp3 on disk.
#include "CoreMinimal.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO   // memory-only: bytes come from FFileHelper, never a C FILE*

THIRD_PARTY_INCLUDES_START
#include "dr_mp3.h"
THIRD_PARTY_INCLUDES_END
