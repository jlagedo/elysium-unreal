#include "ElysiumSessionSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
DEFINE_LOG_CATEGORY_STATIC(LogElysiumSave, Log, All);

void UElysiumSessionSubsystem::PublishSaveResult(const FElysiumSaveResult& Result)
{
	SaveResult = Result;
	const TCHAR* Phase = Result.State == EElysiumSaveOperationState::Capturing ? TEXT("capturing") :
		Result.State == EElysiumSaveOperationState::Writing ? TEXT("writing") :
		Result.State == EElysiumSaveOperationState::Written ? TEXT("written") : TEXT("failed");
	UE_LOG(LogElysiumSave, Display, TEXT("save operation %llu '%s': %s %s"),
		Result.OperationId, *Result.Slot, Phase, *Result.Error);
	// Broadcast a local value: a listener can submit another request on terminal completion.
	const FElysiumSaveResult Published = Result;
	SaveResults.Broadcast(Published);
}

bool UElysiumSessionSubsystem::ExecuteSaveCommand(const TArray<FString>& Args,
	FString& OutSlot, FString& OutError)
{
	OutSlot.Reset();
	if (Args.Num() > 1) { OutError = TEXT("usage: elysium.save [slot]"); return false; }
	return RequestSave({EElysiumSaveKind::Manual, Args.IsEmpty() ? FString() : Args[0]}, OutSlot, OutError);
}

bool UElysiumSessionSubsystem::RequestSave(const FElysiumSaveRequest& Request,
	FString& OutSlot, FString& OutError)
{
	check(IsInGameThread());
	OutSlot.Reset();
	OutError.Reset();
	// Do not overwrite the active operation's observable result on a rejected competing request.
	if (bCapturingSave || SaveStorage.IsWriting())
	{
		OutError = TEXT("write in progress (one save operation at a time)");
		return false;
	}
	FElysiumSaveResult Operation;
	Operation.OperationId = ++NextSaveOperation;
	if (!SaveStorage.ResolveSlotName(Request.Kind, Request.Slot, Operation.Slot, OutError) || !CanSave(OutError))
	{
		Operation.Error = OutError;
		PublishSaveResult(Operation);
		return false;
	}
	TGuardValue<bool> CaptureGuard(bCapturingSave, true);
	Operation.State = EElysiumSaveOperationState::Capturing;
	// No external observer runs halfway through capture.
	FElysiumSavePayload Captured;
	if (!BuildPayload(Captured, OutError))
	{
		Operation.State = EElysiumSaveOperationState::Failed;
		Operation.Error = OutError;
		PublishSaveResult(Operation);
		return false;
	}
	const FElysiumSavePayload Snapshot = MoveTemp(Captured);
	PublishSaveResult(Operation);
	TWeakObjectPtr<UElysiumSessionSubsystem> WeakThis(this);
	if (!SaveStorage.Write(Operation.Slot, Request.Kind, Snapshot,
		[WeakThis, Operation](bool Success) mutable
		{
			Operation.State = Success ? EElysiumSaveOperationState::Written : EElysiumSaveOperationState::Failed;
			if (!Success) Operation.Error = TEXT("native slot write failed");
			if (UElysiumSessionSubsystem* Session = WeakThis.Get()) Session->PublishSaveResult(Operation);
		}, OutError))
	{
		Operation.State = EElysiumSaveOperationState::Failed;
		Operation.Error = OutError;
		PublishSaveResult(Operation);
		return false;
	}
	// Unreal may invoke the delegate inline when the platform service or envelope write
	// fails (GameplayStatics.cpp::AsyncSaveGameToSlot). Never turn that terminal result
	// back into Writing, and do not report acceptance for an already-failed request.
	if (SaveResult.OperationId == Operation.OperationId && SaveResult.State == EElysiumSaveOperationState::Failed)
	{
		OutError = SaveResult.Error;
		return false;
	}
	OutSlot = Operation.Slot;
	if (SaveResult.OperationId != Operation.OperationId || SaveResult.State != EElysiumSaveOperationState::Written)
	{
		Operation.State = EElysiumSaveOperationState::Writing;
		PublishSaveResult(Operation);
	}
	return true;
}

bool UElysiumSessionSubsystem::CanSave(FString& OutReason) const
{
	if (bCapturingSave || SaveStorage.IsWriting())
	{
		OutReason = TEXT("write in progress (one save operation at a time)");
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	const UElysiumGameFlowSubsystem* Flow = GI ? GI->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	const UElysiumSessionSubsystem* State = this;
	if (!Flow || !Maps)
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
		OutReason = TEXT("the current world is the front-end shell, not a run");
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
	if (World->FindPlayer()->IsGrappling())
	{
		OutReason = TEXT("a grapple is in progress"); // GetSaveBlockedReason 0x10174f80, reason 3.
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
	if (const FString Reason = World->ScriptedSessionSaveBlockReason(); !Reason.IsEmpty())
	{
		OutReason = Reason;
		return false;
	}
	OutReason.Reset();
	return true;
}

bool UElysiumSessionSubsystem::BuildPayload(FElysiumSavePayload& Out, FString& OutError) const
{
	Out = FElysiumSavePayload();

	UGameInstance* GI = GetGameInstance();
	const UElysiumSessionSubsystem* State = this;
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;

	// Session.
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

	// Player.
	// The live entity is the truth while a map is up, so dehydrate it into a copy of the record
	// rather than reading the record, which is only refreshed at teardown.
	Out.Player = State->PlayerRecord();
	FElysiumEntityWorld* World = State->CurrentEntityWorld();
	if (const FElysiumPlayer* PlayerEnt = World ? World->FindPlayer() : nullptr)
	{
		PlayerEnt->Dehydrate(Out.Player);
	}

	// Maps.
	// Every map visited this run, plus the current one frozen right now through the same call a
	// travel boundary uses.
	Out.Maps = State->MapSnapshots();
	if (World && !World->MapName().IsEmpty())
	{
		FElysiumMapSnapshot Current;
		World->Freeze(Current);
		Out.Maps.Add(Current.MapName, MoveTemp(Current));
	}

	// World.
	Out.World.VisitedMaps = State->VisitedMaps();
	if (World)
	{
		Out.World.CurrentMap = World->MapName();
		Out.World.VisitedMaps.AddUnique(Out.World.CurrentMap);
		if (const IElysiumEmbodiment* Body = World->Embodiment())
		{
			FRotator View = FRotator::ZeroRotator;
			if (Body->GetPlayerCapsuleTransform(Out.World.PlayerOrigin, View))
			{
				// The save format deliberately retains its capsule-centre + Unreal-yaw contract. Entity
				// logic now owns Source feet separately, so old payloads restore without migration.
				Out.World.PlayerYaw = View.Yaw;
				Out.World.bHasPlacement = true;
			}
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

void UElysiumSessionSubsystem::ApplyPayload(const FElysiumSavePayload& In)
{
	UElysiumSessionSubsystem* State = this;

	// The world that is about to die must not write over any of this: travel is deferred to the end
	// of the frame, so its teardown lands after we return. Detach is what ForgetPlayer is for
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

	TMap<FString, FElysiumMapSnapshot> RestoredMaps = In.Maps;
	State->SetMapSnapshots(MoveTemp(RestoredMaps));
	TArray<FString> RestoredVisits = In.World.VisitedMaps;
	State->SetVisitedMaps(MoveTemp(RestoredVisits));
}

bool UElysiumSessionSubsystem::Load(const FString& Slot, FString& OutError)
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

void UElysiumSessionSubsystem::RegisterSaveCommands()
{
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.save"), TEXT("elysium.save [slot] — capture the run and write a native slot."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			FString Slot, Error;
			if (!ExecuteSaveCommand(Args, Slot, Error))
				UE_LOG(LogElysiumSave, Warning, TEXT("save refused: %s"), *Error);
		}), ECVF_Default));
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
			if (Args.Num() != 1)
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
