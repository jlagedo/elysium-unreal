#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

// P6.1/6.2 audio debug window — the test harness for the runtime WAV (dr_wav) + MP3 (dr_mp3)
// decoders. Type (or pick from the map's ambient_generic references) a path under out/sound/, Play
// it 2D, and inspect every decode's format/metadata (codec/on-disk tag, channels, sample rate, bits,
// frames, duration, decode ms, errors) in a live table over the audio subsystem's shared registry —
// plus a summary of the MS-ADPCM/IMA/PCM/MP3 mix. Drives + reads UElysiumAudioSubsystem, the WorldViz
// window<->subsystem shared-state pattern.
class FElysiumCogWindow_Audio : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// Gather the distinct WAV/MP3 paths this map's ambient_generic entities reference (from the
	// .ents `message` keys), so the window offers real, one-click test material.
	TArray<FString> CollectMapAudioRefs() const;

	FString PendingPath;                 // the path in the input box
	ImGuiTextFilter RefFilter;           // filters the map's ambient_generic reference list
	ImGuiTextFilter DecodeFilter;        // filters the decode-results table
	bool bRefsDirty = true;              // recollect the reference list on map change / first open
	TArray<FString> MapRefs;             // cached ambient_generic WAV/MP3 references
	FString LastMap;                     // map name the refs were collected for
};

#endif // ENABLE_COG
