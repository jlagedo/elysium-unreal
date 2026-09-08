#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

// Test harness for the baked sound family. Type (or pick from the map's ambient_generic
// references) a logical path under sound/, Play it 2D, and inspect what the resolver made of every
// key this session — the asset it named, whether that asset is loaded, retained by a prefetch,
// still loading or missing from the bake, plus its channels/rate/duration and whether the bake
// split it into an intro and a loop body. Drives + reads UElysiumAudioSubsystem, the WorldViz
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
	ImGuiTextFilter DecodeFilter;        // filters the resolved-asset table
	bool bRefsDirty = true;              // recollect the reference list on map change / first open
	TArray<FString> MapRefs;             // cached ambient_generic WAV/MP3 references
	FString LastMap;                     // map name the refs were collected for
};

#endif // ENABLE_COG
