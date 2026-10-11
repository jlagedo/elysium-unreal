#pragma once

#include "Audio/ElysiumSoundScriptTable.h"

namespace ElysiumKeyValues { struct FKvNode; }
namespace ElysiumKeyValuesLoader { struct IKvFileSystem; }

namespace ElysiumSoundScript
{
	// The whole-source producer: BaseInit 101b2c60 -> AddSoundsFromFile 101b4240.
	// LoadFile receives the ORIGINAL full filename, before the file record's 127-byte copy.
	struct FSourceFile
	{
		FString Filename;
		bool bPrecache = false;
		bool bChanged = false;
		FString OriginalFilename; // metadata binding never uses the retail bounded record copy
	};
	struct FSourceEntry
	{
		int32 Node = INDEX_NONE;
		int32 File = INDEX_NONE;
		FString Name;
	};
	struct FSelectedSource { FString Filename; FString Text; int32 Size = 0; };
	enum class EWaveAvailability : uint8 { Present, AuthoredAbsent, Unknown, Nonfile };
	struct FPrecacheAttempt { FString Wave; EWaveAvailability Availability = EWaveAvailability::Unknown; };
	// Return true only for the narrowly verified missing outcome; all other missing outcomes error.
	bool CompleteAuthoredAbsence(EWaveAvailability Availability, bool bBakedMissing, FString& Error);
	class FSourceTable
	{
	public:
		FTable Table;
		TArray<FSourceFile> Files;
		TArray<FSourceEntry> Entries;
		TArray<FString> Warnings;
		int32 MissingWaves = 0;
		bool bLoaded = false;
		TArray<FSelectedSource> SelectedSources;
		TArray<EWaveAvailability> Availability; // every accepted wave in descriptor/wave order
		bool bAvailabilityValidated = false;
		bool Load(ElysiumKeyValuesLoader::IKvFileSystem& Fs, IFileExists& WaveFiles, FString& Error);
		void Shutdown();
		TArray<FString> PrecacheWaves() const;
		TArray<FPrecacheAttempt> PrecacheAttempts() const;
		bool ValidateAvailability(const FString& Metadata, FString& Error);
	};
	FInterval ParseInterval(const FString& Text); // 1012f640, stored float base/span
	void ParseKeys(FTable& Table, FParams& Params, const ElysiumKeyValues::FKvNode& Root,
		TArray<FString>& Warnings); // 101b3bb0
	void AppendWave(FTable& Table, FParams& Params, const FString& Wave); // 3830/3770/3990
}
