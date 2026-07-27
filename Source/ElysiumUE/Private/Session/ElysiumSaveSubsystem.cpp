#include "ElysiumSaveSubsystem.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveGame.h"

#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSave, Log, All);

namespace
{
	// `.sav` files live under Saved/SaveGames/ — the player's own data, gitignored like everything
	// else generated. This is where USaveGame's platform-safe path resolves on desktop.
	FString SaveGamesDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames");
	}

	constexpr int32 GUserIndex = 0;
	const TCHAR* const GQuickSlot = TEXT("Quick");
	const TCHAR* const GManualPrefix = TEXT("Elysium-");
	const TCHAR* const GAutoPrefix = TEXT("Auto");
}

const TCHAR* UElysiumSaveSubsystem::KindName(EElysiumSaveKind Kind)
{
	switch (Kind)
	{
	case EElysiumSaveKind::Quick: return TEXT("quick");
	case EElysiumSaveKind::Auto:  return TEXT("auto");
	default:                      return TEXT("manual");
	}
}

void UElysiumSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterCommands();
}

void UElysiumSaveSubsystem::Deinitialize()
{
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();
	Super::Deinitialize();
}

// ================================================================================================
// Slots
// ================================================================================================

FString UElysiumSaveSubsystem::ResolveSlotName(EElysiumSaveKind Kind, const FString& Requested) const
{
	if (Kind == EElysiumSaveKind::Quick)
	{
		return GQuickSlot;   // one quicksave, always the same slot
	}
	if (Kind == EElysiumSaveKind::Auto)
	{
		return FString::Printf(TEXT("%s%d"), GAutoPrefix, AutoRingCursor % AutoRingSize);
	}
	if (!Requested.IsEmpty())
	{
		return Requested;
	}
	// The next free manual slot. Sequential rather than "highest + 1" so a deleted slot is reused
	// and the list never grows holes the menu has to render around.
	for (int32 i = 1; i < 1000; ++i)
	{
		const FString Candidate = FString::Printf(TEXT("%s%03d"), GManualPrefix, i);
		if (!UGameplayStatics::DoesSaveGameExist(Candidate, GUserIndex))
		{
			return Candidate;
		}
	}
	return FString::Printf(TEXT("%s999"), GManualPrefix);
}

bool UElysiumSaveSubsystem::ReadSlotHeader(const FString& Slot, FElysiumSaveHeaderData& Out) const
{
	const UElysiumSaveGame* Game = Cast<UElysiumSaveGame>(
		UGameplayStatics::LoadGameFromSlot(Slot, GUserIndex));
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

void UElysiumSaveSubsystem::ListSlots(TArray<FElysiumSaveSlotInfo>& Out) const
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

bool UElysiumSaveSubsystem::DeleteSlot(const FString& Slot)
{
	return UGameplayStatics::DeleteGameInSlot(Slot, GUserIndex);
}

// ================================================================================================
// Freeze / thaw
// ================================================================================================

bool UElysiumSaveSubsystem::CanSave(FString& OutReason) const
{
	UGameInstance* GI = GetGameInstance();
	const UElysiumGameFlowSubsystem* Flow = GI ? GI->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	if (!Flow || !Maps || !State)
	{
		OutReason = TEXT("no session subsystems");
		return false;
	}
	if (!Flow->IsInSession())
	{
		OutReason = TEXT("there is no run to save");
		return false;
	}
	if (Maps->HasPendingMapLoad())
	{
		OutReason = TEXT("a map load is in flight");
		return false;
	}
	if (Maps->IsMenuBackdrop())
	{
		OutReason = TEXT("the current world is the menu backdrop, not a run");
		return false;
	}

	const FElysiumEntityWorld* World = State->CurrentEntityWorld();
	if (!World)
	{
		OutReason = TEXT("no entity world is built");
		return false;
	}
	if (!World->FindPlayer())
	{
		OutReason = TEXT("this world has no player");
		return false;
	}
	// Neither a sign panel nor an open conversation carries a resume point the payload models: the
	// panel is a modal the player has to dismiss, and the conversation holds a branch cursor the
	// snapshot does not serialize. Refusing loudly beats writing a slot that reopens on nothing.
	if (World->GetOpenSign().IsSet())
	{
		OutReason = TEXT("a sign panel is open");
		return false;
	}
	if (World->GetOpenDialog() != nullptr)
	{
		OutReason = TEXT("a conversation is open");
		return false;
	}
	OutReason.Reset();
	return true;
}

bool UElysiumSaveSubsystem::BuildPayload(FElysiumSavePayload& Out, FString& OutError) const
{
	Out = FElysiumSavePayload();

	UGameInstance* GI = GetGameInstance();
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!State)
	{
		OutError = TEXT("no game-state subsystem");
		return false;
	}

	// --- Session ------------------------------------------------------------------------------
	Out.Session.ClockNow = State->GameClock().GetNow();
	for (const TPair<FString, FElysiumVariant>& G : State->GetGlobals())
	{
		Out.Session.Globals.Emplace(G.Key, G.Value);
	}
	for (const TPair<FString, int32>& Q : State->GetQuests())
	{
		Out.Session.Quests.Emplace(Q.Key, Q.Value);
	}
	// Sorted so two saves of the same state are byte-identical (§8). `G` is case-sensitive, so the
	// comparison is too.
	Out.Session.Globals.Sort([](const TPair<FString, FElysiumVariant>& A,
	                            const TPair<FString, FElysiumVariant>& B)
	{
		return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0;
	});
	Out.Session.Quests.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
	{
		return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0;
	});
	Out.Session.RngSessionSeed = ElysiumRng::SessionSeed();
	ElysiumRng::Snapshot(Out.Session.Rng);

	// --- Player -------------------------------------------------------------------------------
	// The live entity is the truth while a map is up, so dehydrate it into a copy of the record
	// rather than reading the record, which is only refreshed at teardown.
	Out.Player = State->PlayerRecord();
	FElysiumEntityWorld* World = State->CurrentEntityWorld();
	if (const FElysiumPlayer* PlayerEnt = World ? World->FindPlayer() : nullptr)
	{
		PlayerEnt->Dehydrate(Out.Player);
	}

	// --- Maps ---------------------------------------------------------------------------------
	// Every map visited this run, plus the current one frozen right now through the same call a
	// travel boundary uses.
	Out.Maps = State->MapSnapshots();
	if (World && !World->MapName().IsEmpty())
	{
		FElysiumMapSnapshot Current;
		World->Freeze(Current);
		Out.Maps.Add(Current.MapName, MoveTemp(Current));
	}

	// --- World --------------------------------------------------------------------------------
	Out.World.VisitedMaps = State->VisitedMaps();
	if (World)
	{
		Out.World.CurrentMap = World->MapName();
		Out.World.VisitedMaps.AddUnique(Out.World.CurrentMap);
		if (const FElysiumPlayer* PlayerEnt = World->FindPlayer())
		{
			// The entity's origin IS the pawn's actor location (11.4 samples it once a frame) and its
			// stored yaw is the Source-space negation of the control yaw, so both go back verbatim.
			Out.World.PlayerOrigin = PlayerEnt->Origin;
			Out.World.PlayerYaw = -PlayerEnt->Angles.Y;
			Out.World.bHasPlacement = true;
		}
	}
	else if (Maps)
	{
		Out.World.CurrentMap = Maps->GetCurrentMapName();
	}

	if (Out.World.CurrentMap.IsEmpty())
	{
		OutError = TEXT("no current map to save");
		return false;
	}
	OutError.Reset();
	return true;
}

void UElysiumSaveSubsystem::ApplyPayload(const FElysiumSavePayload& In)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	if (!State)
	{
		return;
	}

	// The world that is about to die must not write over any of this: travel is deferred to the end
	// of the frame, so its teardown lands after we return. Detach is what 11.4's ForgetPlayer is for
	// EndSession — the same "this world no longer owns the session" statement, one step wider.
	if (FElysiumEntityWorld* Dying = State->CurrentEntityWorld())
	{
		Dying->Detach();
	}

	State->ClearAllGlobals();
	for (const TPair<FString, FElysiumVariant>& G : In.Session.Globals)
	{
		State->SetGlobal(G.Key, G.Value);
	}
	// A load is a wholesale replace and must be SILENT — SetQuestState pays out the completion
	// state's awards, so routing a load through it would replay the whole run's XP. RestoreQuests
	// is the door that only moves the map; the journal arrives with the player record below.
	State->RestoreQuests(TArray<TPair<FString, int32>>(In.Session.Quests));

	ElysiumRng::SeedAll(In.Session.RngSessionSeed);
	ElysiumRng::Restore(In.Session.Rng);

	// The clock is the save's, not zero: every FireTime in every restored queue is absolute against it.
	State->TimeControl().ResetClock(In.Session.ClockNow);

	State->PlayerRecord() = In.Player;

	TMap<FString, FElysiumMapSnapshot> Snapshots = In.Maps;
	State->SetMapSnapshots(MoveTemp(Snapshots));
	TArray<FString> Visited = In.World.VisitedMaps;
	State->SetVisitedMaps(MoveTemp(Visited));
}

FElysiumSaveHeaderData UElysiumSaveSubsystem::MakeHeader(const FElysiumSavePayload& Payload,
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

// ================================================================================================
// Save / load
// ================================================================================================

bool UElysiumSaveSubsystem::Save(EElysiumSaveKind Kind, const FString& RequestedSlot,
	FString& OutSlot, FString& OutError)
{
	OutSlot.Reset();
	if (!CanSave(OutError))
	{
		UE_LOG(LogElysiumSave, Warning, TEXT("save refused: %s"), *OutError);
		return false;
	}

	// The freeze is synchronous and must be atomic with respect to the frame — it is a memory walk
	// over the live world, and half of it taken before a tick and half after would be a wrong save.
	FElysiumSavePayload Payload;
	if (!BuildPayload(Payload, OutError))
	{
		UE_LOG(LogElysiumSave, Warning, TEXT("save refused: %s"), *OutError);
		return false;
	}

	TArray<uint8> Bytes;
	if (!ElysiumSave::Write(Payload, Bytes, OutError))
	{
		UE_LOG(LogElysiumSave, Error, TEXT("save refused: %s"), *OutError);
		return false;
	}

	UElysiumSaveGame* Game = Cast<UElysiumSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UElysiumSaveGame::StaticClass()));
	if (!Game)
	{
		OutError = TEXT("could not create the save-game object");
		return false;
	}
	const FElysiumSaveHeaderData Header = MakeHeader(Payload, Kind);
	Game->PayloadVersion  = Header.PayloadVersion;
	Game->Map             = Header.Map;
	Game->Label           = Header.Label;
	Game->ClanName        = Header.ClanName;
	Game->Clan            = Header.Clan;
	Game->PlaytimeSeconds = Header.PlaytimeSeconds;
	Game->Timestamp       = Header.Timestamp;
	Game->Kind            = Header.Kind;
	Game->Payload         = MoveTemp(Bytes);

	OutSlot = ResolveSlotName(Kind, RequestedSlot);
	if (Kind == EElysiumSaveKind::Auto)
	{
		AutoRingCursor = (AutoRingCursor + 1) % AutoRingSize;   // rotate, like VtMB's ring
	}

	const int32 PayloadBytes = Game->Payload.Num();
	const FString Slot = OutSlot;
	// Compress-and-write goes off the game thread; the state it describes is already a private copy.
	UGameplayStatics::AsyncSaveGameToSlot(Game, Slot, GUserIndex,
		FAsyncSaveGameToSlotDelegate::CreateWeakLambda(this,
			[Slot, PayloadBytes](const FString&, const int32, bool bSuccess)
			{
				UE_LOG(LogElysiumSave, Display, TEXT("save '%s': %s (%d payload bytes)"),
					*Slot, bSuccess ? TEXT("written") : TEXT("FAILED"), PayloadBytes);
			}));

	UE_LOG(LogElysiumSave, Display, TEXT("saving '%s' (%s) — map %s, %d maps, %d payload bytes"),
		*OutSlot, KindName(Kind), *Header.Map, Payload.Maps.Num(), PayloadBytes);
	return true;
}

bool UElysiumSaveSubsystem::ReadSlotPayload(const FString& Slot, FElysiumSavePayload& Out,
	FString& OutError) const
{
	const UElysiumSaveGame* Game = Cast<UElysiumSaveGame>(
		UGameplayStatics::LoadGameFromSlot(Slot, GUserIndex));
	if (!Game)
	{
		OutError = FString::Printf(TEXT("no save in slot '%s'"), *Slot);
		return false;
	}
	return ElysiumSave::Read(Game->Payload, Out, OutError);
}

bool UElysiumSaveSubsystem::Load(const FString& Slot, FString& OutError)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		OutError = TEXT("no map subsystem");
		return false;
	}

	FElysiumSavePayload Payload;
	if (!ReadSlotPayload(Slot, Payload, OutError))
	{
		UE_LOG(LogElysiumSave, Warning, TEXT("load '%s' refused: %s"), *Slot, *OutError);
		return false;
	}

	// Check the destination before touching the session, the same precondition New Game asks: a load
	// that cannot travel must not have thrown the current run away on the way to failing.
	if (!Maps->ExportedMaps().Contains(Payload.World.CurrentMap))
	{
		OutError = FString::Printf(TEXT("saved map '%s' is not exported+baked"), *Payload.World.CurrentMap);
		UE_LOG(LogElysiumSave, Warning, TEXT("load '%s' refused: %s"), *Slot, *OutError);
		return false;
	}

	ApplyPayload(Payload);

	if (Payload.World.bHasPlacement)
	{
		Maps->RequestRestorePlacement(Payload.World.PlayerOrigin, Payload.World.PlayerYaw);
	}
	if (!Maps->Travel(Payload.World.CurrentMap))
	{
		OutError = FString::Printf(TEXT("travel to '%s' was refused"), *Payload.World.CurrentMap);
		return false;
	}

	UE_LOG(LogElysiumSave, Display, TEXT("loaded '%s': map %s at t=%.3f, %d map snapshots"),
		*Slot, *Payload.World.CurrentMap, Payload.Session.ClockNow, Payload.Maps.Num());
	return true;
}

// ================================================================================================
// Verbs
// ================================================================================================

void UElysiumSaveSubsystem::RegisterCommands()
{
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.save.slots"),
		TEXT("List every save slot on disk with its header (map, clan, playtime, kind)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			TArray<FElysiumSaveSlotInfo> Slots;
			ListSlots(Slots);
			if (Slots.Num() == 0)
			{
				UE_LOG(LogElysiumSave, Display, TEXT("no save slots"));
				return;
			}
			for (const FElysiumSaveSlotInfo& S : Slots)
			{
				UE_LOG(LogElysiumSave, Display, TEXT("%-16s v%d %-7s %s  t=%.0fs  %s"),
					*S.Slot, S.Header.PayloadVersion, *S.Header.Kind, *S.Header.Label,
					S.Header.PlaytimeSeconds, *S.Header.Timestamp.ToString());
			}
		}),
		ECVF_Default));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.save.cansave"),
		TEXT("Report whether a save would be accepted right now, and why not if it would not."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			FString Reason;
			const bool bOk = CanSave(Reason);
			UE_LOG(LogElysiumSave, Display, TEXT("cansave: %s%s%s"),
				bOk ? TEXT("yes") : TEXT("no"), bOk ? TEXT("") : TEXT(" — "), bOk ? TEXT("") : *Reason);
		}),
		ECVF_Default));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.save.delete"),
		TEXT("elysium.save.delete <slot> — remove a save slot."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogElysiumSave, Display, TEXT("usage: elysium.save.delete <slot>"));
				return;
			}
			UE_LOG(LogElysiumSave, Display, TEXT("delete '%s': %s"), *Args[0],
				DeleteSlot(Args[0]) ? TEXT("gone") : TEXT("not found"));
		}),
		ECVF_Default));

	// §10 — "why did this not persist" is a text diff, not a debugger session. With one argument it
	// dumps a slot (or `live` for what a save right now would hold); with two it diffs them.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.save.diff"),
		TEXT("elysium.save.diff <slot|live> [slot|live] — dump a payload as readable name/value ")
		TEXT("lines, or diff two of them."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			auto Gather = [this](const FString& Which, TArray<FString>& OutLines) -> bool
			{
				FElysiumSavePayload Payload;
				FString Error;
				const bool bOk = Which.Equals(TEXT("live"), ESearchCase::IgnoreCase)
					? BuildPayload(Payload, Error)
					: ReadSlotPayload(Which, Payload, Error);
				if (!bOk)
				{
					UE_LOG(LogElysiumSave, Warning, TEXT("save.diff '%s': %s"), *Which, *Error);
					return false;
				}
				ElysiumSave::Describe(Payload, OutLines);
				return true;
			};

			if (Args.Num() < 1)
			{
				UE_LOG(LogElysiumSave, Display, TEXT("usage: elysium.save.diff <slot|live> [slot|live]"));
				return;
			}

			TArray<FString> A;
			if (!Gather(Args[0], A))
			{
				return;
			}
			if (Args.Num() == 1)
			{
				for (const FString& Line : A)
				{
					UE_LOG(LogElysiumSave, Display, TEXT("%s"), *Line);
				}
				UE_LOG(LogElysiumSave, Display, TEXT("(%d lines)"), A.Num());
				return;
			}

			TArray<FString> B;
			if (!Gather(Args[1], B))
			{
				return;
			}
			// A set diff, not a positional one: the dumps are already in a stable order, and what a
			// reader wants is "which values are not in both", not where they moved to.
			const TSet<FString> SetA(A);
			const TSet<FString> SetB(B);
			int32 Diffs = 0;
			for (const FString& Line : A)
			{
				if (!SetB.Contains(Line)) { UE_LOG(LogElysiumSave, Display, TEXT("- %s"), *Line); ++Diffs; }
			}
			for (const FString& Line : B)
			{
				if (!SetA.Contains(Line)) { UE_LOG(LogElysiumSave, Display, TEXT("+ %s"), *Line); ++Diffs; }
			}
			UE_LOG(LogElysiumSave, Display, TEXT("%s vs %s: %d differing lines (%d / %d)"),
				*Args[0], *Args[1], Diffs, A.Num(), B.Num());
		}),
		ECVF_Default));
}
