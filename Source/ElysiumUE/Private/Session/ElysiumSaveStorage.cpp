#include "ElysiumSaveStorage.h"
#include "ElysiumSaveGame.h"
#include "ElysiumSaveArchive.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
namespace {
constexpr int32 GUserIndex = 0;
FString SaveGamesDir() { return FPaths::ProjectSavedDir() / TEXT("SaveGames"); }
}

const TCHAR* FElysiumSaveStorage::KindName(EElysiumSaveKind Kind)
{
	switch (Kind)
	{
	case EElysiumSaveKind::Quick: return TEXT("quick");
	case EElysiumSaveKind::Auto: return TEXT("auto");
	default: return TEXT("manual");
	}
}

bool FElysiumSaveStorage::NormalizeSlot(const FString& In, FString& Out, FString& Error)
{
	Out.Reset();
	Error.Reset();
	if (In.IsEmpty() || In.Len() > 64)
	{
		Error = TEXT("slot must contain 1-64 ASCII letters, digits, underscores or hyphens");
		return false;
	}
	for (TCHAR C : In)
	{
		if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') ||
			(C >= '0' && C <= '9') || C == '_' || C == '-'))
		{
			Error = TEXT("invalid slot: use a logical name, not a path or extra arguments");
			return false;
		}
	}
	Out = In.ToLower();
	// Stable spelling for the established service names; all other names use lowercase.
	if (Out == TEXT("quick")) Out = TEXT("Quick");
	else if (Out.StartsWith(TEXT("auto"))) Out[0] = 'A';
	else if (Out.StartsWith(TEXT("elysium-"))) Out[0] = 'E';
	return true;
}

bool FElysiumSaveStorage::ResolveSlotName(EElysiumSaveKind Kind, const FString& Requested,
	FString& Out, FString& Error) const
{
	Out.Reset();
	Error.Reset();
	if (Kind != EElysiumSaveKind::Manual && Kind != EElysiumSaveKind::Quick && Kind != EElysiumSaveKind::Auto)
	{
		Error = TEXT("invalid save kind");
		return false;
	}
	if (Kind != EElysiumSaveKind::Manual)
	{
		if (!Requested.IsEmpty()) { Error = TEXT("service saves do not accept a manual slot"); return false; }
		Out = Kind == EElysiumSaveKind::Quick ? TEXT("Quick")
			: FString::Printf(TEXT("Auto%d"), State->AutoRingCursor);
		return true;
	}
	if (!Requested.IsEmpty())
	{
		if (!NormalizeSlot(Requested, Out, Error)) return false;
		if (Out == TEXT("Quick") || Out.Equals(TEXT("Auto"), ESearchCase::IgnoreCase) ||
			(Out.StartsWith(TEXT("Auto")) && Out.Mid(4).IsNumeric()))
		{
			Out.Reset(); Error = TEXT("Quick/Auto slots are reserved for service saves"); return false;
		}
		return true;
	}
	for (int32 I = 1; I < 1000; ++I)
	{
		const FString Candidate = FString::Printf(TEXT("Elysium-%03d"), I);
		if (!UGameplayStatics::DoesSaveGameExist(Candidate, GUserIndex) && State->WritingSlot != Candidate)
		{
			Out = Candidate;
			return true;
		}
	}
	Error = TEXT("all 999 manual slots are occupied; choose a name or delete a slot");
	return false;
}

bool FElysiumSaveStorage::Write(const FString& Slot, EElysiumSaveKind Kind,
	const FElysiumSavePayload& Snapshot, TFunction<void(bool)> Completion, FString& Error)
{
	check(IsInGameThread());
	Error.Reset();
	FString Canonical;
	if (!NormalizeSlot(Slot, Canonical, Error)) return false;
	// Initial policy: serialize all writes, not only writes targeting the same slot.
	if (IsWriting()) { Error = TEXT("write in progress (one save write at a time)"); return false; }
	TArray<uint8> Bytes;
	if (!ElysiumSave::Write(Snapshot, Bytes, Error)) return false;
	UElysiumSaveGame* Game = Cast<UElysiumSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UElysiumSaveGame::StaticClass()));
	if (!Game) { Error = TEXT("could not create the save-game object"); return false; }
	const FElysiumSaveHeaderData Header = MakeHeader(Snapshot, Kind);
	Game->PayloadVersion = Header.PayloadVersion;
	Game->Map = Header.Map;
	Game->Label = Header.Label;
	Game->ClanName = Header.ClanName;
	Game->Clan = Header.Clan;
	Game->PlaytimeSeconds = Header.PlaytimeSeconds;
	Game->Timestamp = Header.Timestamp;
	Game->Kind = Header.Kind;
	Game->Payload = MoveTemp(Bytes);
	State->WritingSlot = Canonical;
	UGameplayStatics::AsyncSaveGameToSlot(Game, Canonical, GUserIndex,
		FAsyncSaveGameToSlotDelegate::CreateLambda(
			[OwnedState = State, Kind, Completion = MoveTemp(Completion)](const FString&, int32, bool Success)
			{
				OwnedState->WritingSlot.Reset();
				if (Success && Kind == EElysiumSaveKind::Auto)
					OwnedState->AutoRingCursor = (OwnedState->AutoRingCursor + 1) % 5;
				Completion(Success);
			}));
	return true;
}

bool FElysiumSaveStorage::ReadSlotHeader(const FString& Slot, FElysiumSaveHeaderData& Out) const
{
	FString Canonical;
	FString Error;
	if (!NormalizeSlot(Slot, Canonical, Error)) return false;
	if (State->WritingSlot.Equals(Canonical, ESearchCase::IgnoreCase)) { Error = TEXT("write in progress"); return false; }

	const UElysiumSaveGame* Game = Cast<UElysiumSaveGame>(
		UGameplayStatics::LoadGameFromSlot(Canonical, GUserIndex));
	if (!Game)
	{
		return false;
	}
	// Reflected, uncompressed, ahead of the payload: listing a slot never inflates one.
	Out.PayloadVersion  = Game->PayloadVersion;
	Out.Map             = Game->Map;
	Out.Label           = Game->Label;
	Out.ClanName        = Game->ClanName;
	Out.Clan            = Game->Clan;
	Out.PlaytimeSeconds = Game->PlaytimeSeconds;
	Out.Timestamp       = Game->Timestamp;
	Out.Kind            = Game->Kind;
	return true;
}

void FElysiumSaveStorage::ListSlots(TArray<FElysiumSaveSlotInfo>& Out) const
{
	Out.Reset();

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(SaveGamesDir() / TEXT("*.sav")), /*Files*/ true, /*Dirs*/ false);
	for (const FString& File : Files)
	{
		FElysiumSaveSlotInfo Info;
		Info.Slot = FPaths::GetBaseFilename(File);
		if (ReadSlotHeader(Info.Slot, Info.Header))
		{
			Out.Add(MoveTemp(Info));
		}
	}
	Out.Sort([](const FElysiumSaveSlotInfo& A, const FElysiumSaveSlotInfo& B)
	{
		return A.Header.Timestamp > B.Header.Timestamp;
	});
}

bool FElysiumSaveStorage::DeleteSlot(const FString& Slot)
{
	FString Canonical;
	FString Error;
	if (!NormalizeSlot(Slot, Canonical, Error)) return false;
	if (State->WritingSlot.Equals(Canonical, ESearchCase::IgnoreCase)) { Error = TEXT("write in progress"); return false; }

	return UGameplayStatics::DeleteGameInSlot(Canonical, GUserIndex);
}

FElysiumSaveHeaderData FElysiumSaveStorage::MakeHeader(const FElysiumSavePayload& Payload,
	EElysiumSaveKind Kind) const
{
	FElysiumSaveHeaderData H;
	H.PayloadVersion  = FElysiumSaveVersion::Latest;
	H.Map             = Payload.World.CurrentMap;
	H.Clan            = Payload.Player.Sheet.Clan();
	H.ClanName        = FElysiumSheet::ClanName(Payload.Player.Sheet.Clan());
	H.PlaytimeSeconds = Payload.Session.ClockNow;
	H.Timestamp       = FDateTime::UtcNow();
	H.Kind            = KindName(Kind);
	// The label is what the load menu shows. VtMB's `comment` is the map's display name plus the
	// clan; there is no display-name table yet, so the map name stands in.
	H.Label = FString::Printf(TEXT("%s — %s"), *H.Map, *H.ClanName);
	return H;
}

bool FElysiumSaveStorage::ReadSlotPayload(const FString& Slot, FElysiumSavePayload& Out,
	FString& OutError) const
{
	FString Canonical;
	if (!NormalizeSlot(Slot, Canonical, OutError)) return false;
	if (State->WritingSlot.Equals(Canonical, ESearchCase::IgnoreCase)) { OutError = TEXT("write in progress"); return false; }

	const UElysiumSaveGame* Game = Cast<UElysiumSaveGame>(
		UGameplayStatics::LoadGameFromSlot(Canonical, GUserIndex));
	if (!Game)
	{
		OutError = FString::Printf(TEXT("no save in slot '%s'"), *Slot);
		return false;
	}
	return ElysiumSave::Read(Game->Payload, Out, OutError);
}
