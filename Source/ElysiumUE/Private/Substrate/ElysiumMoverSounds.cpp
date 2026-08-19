// Mover sounds (P6.4) — the soundgroup manifest loader.
//
// Reference: `docs/vtmb/audio_pipeline.md` + the decompiled CBaseDoor::Spawn (FUN_100ef060, reads the
// subkeys "close"/"open"/"swing"/"locked") and CBaseButton::Spawn (FUN_100c8810, reads "on"/"off").
// VtMB has no soundgroup *data file*: a `soundgroup` token resolves by directory convention to
// sound/usable/<category>/<token>/<subkey>.wav (openable=doors, switches=buttons). The offline
// UE_extract_sounds.py mirrors those WAVs and writes out/sound/usable/soundgroups.json; this loads
// it once. Per-mover playback (FElysiumMoverBase's sound methods) lives in ElysiumMover.cpp.

#include "Substrate/ElysiumMoverSounds.h"

#include "ElysiumContentPaths.h"
#include "Substrate/ElysiumMover.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// cat -> group(lower) -> subkey -> sound-relative WAV. Loaded once from the offline manifest.
using FMoverSoundTable = TMap<FString, TMap<FString, TMap<FName, FString>>>;

const FMoverSoundTable& ElysiumMoverSoundManifest()
{
	static FMoverSoundTable Table;
	static bool bLoaded = false;
	if (bLoaded)
	{
		return Table;
	}
	bLoaded = true;   // load-once, even on failure (missing manifest = movers stay silent)

	const FString Path = FElysiumContentPaths::SoundDir() / TEXT("usable/soundgroups.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogElysiumMover, Log, TEXT("mover sounds: no manifest at %s (movers silent)"), *Path);
		return Table;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogElysiumMover, Warning, TEXT("mover sounds: manifest parse failed (%s)"), *Path);
		return Table;
	}

	// { category: { group: { subkey: relpath } } }
	for (const auto& CatPair : Root->Values)
	{
		const TSharedPtr<FJsonObject>* CatObj;
		if (!CatPair.Value.IsValid() || !CatPair.Value->TryGetObject(CatObj))
		{
			continue;
		}
		TMap<FString, TMap<FName, FString>>& Groups = Table.FindOrAdd(FString(CatPair.Key).ToLower());
		for (const auto& GroupPair : (*CatObj)->Values)
		{
			const TSharedPtr<FJsonObject>* GroupObj;
			if (!GroupPair.Value.IsValid() || !GroupPair.Value->TryGetObject(GroupObj))
			{
				continue;
			}
			TMap<FName, FString>& Subs = Groups.FindOrAdd(FString(GroupPair.Key).ToLower());
			for (const auto& SubPair : (*GroupObj)->Values)
			{
				FString Rel;
				if (SubPair.Value.IsValid() && SubPair.Value->TryGetString(Rel))
				{
					Subs.Add(FName(FString(SubPair.Key)), Rel);
				}
			}
		}
	}
	return Table;
}
