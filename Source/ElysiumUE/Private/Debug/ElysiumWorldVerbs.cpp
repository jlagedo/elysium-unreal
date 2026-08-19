#include "ElysiumEntityWorld.h"

#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "Substrate/ElysiumEntityWorldShared.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static FAutoConsoleCommand GElysiumTrigger(
	TEXT("elysium.trigger"),
	TEXT("elysium.trigger <on|off> -- globally resume or suspend map trigger resolution, entity thinks, and deferred I/O/Python."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("usage: elysium.trigger <on|off> (currently %s)"),
				FElysiumEntityWorld::IsTriggerResolutionEnabled() ? TEXT("on") : TEXT("off"));
			return;
		}

		if (Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			FElysiumEntityWorld::SetTriggerResolutionEnabled(true);
		}
		else if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			FElysiumEntityWorld::SetTriggerResolutionEnabled(false);
		}
		else
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.trigger: expected 'on' or 'off', got '%s'"), *Args[0]);
		}
	}));

// --- Verification / test verbs ----------------------------------------------------------
// The P1.4 test harness: prove the world, the two chokepoints, the queue, and the ring buffer
// end-to-end with nothing but the log. Phase 2 replaces these with the Cog entity/queue windows
// and the full `ent_*` verb set (which fire through this same AcceptInput/queue).

namespace ElysiumWorldVerbsPrivate
{
	FElysiumEntityWorld* ElysiumCurrentWorld(UWorld* W)
	{
		if (!W)
		{
			return nullptr;
		}
		if (const UGameInstance* GI = W->GetGameInstance())
		{
			if (UElysiumMapSubsystem* Maps = GI->GetSubsystem<UElysiumMapSubsystem>())
			{
				if (AElysiumMapActor* Map = Maps->GetCurrentMap())
				{
					return Map->GetEntityWorld();
				}
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldCmd(
	TEXT("elysium.world"),
	TEXT("elysium.world — summarize the live entity world (counts, classname histogram, queue, ring, dead wires)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumWorldVerbsPrivate::ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world: no live world (load a map first)"));
			return;
		}

		UE_LOG(LogElysiumWorld, Display, TEXT("world: %d entities, epoch %u, now %.3fs"),
			EW->NumEntities(), EW->GetEpoch(), EW->NowSeconds());

		TMap<FString, int32> Histo;
		for (const TUniquePtr<FElysiumEntity>& E : EW->Entities())
		{
			if (E && E->Def)
			{
				++Histo.FindOrAdd(E->Def->Classname);
			}
		}
		Histo.ValueSort([](int32 A, int32 B) { return A > B; });
		int32 Shown = 0;
		for (const TPair<FString, int32>& Pair : Histo)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("  %5d  %s"), Pair.Value, *Pair.Key);
			if (++Shown >= 15)
			{
				UE_LOG(LogElysiumWorld, Display, TEXT("  ... (%d classnames total)"), Histo.Num());
				break;
			}
		}

		UE_LOG(LogElysiumWorld, Display,
			TEXT("queue: %d pending%s | ring: %d/%d | dead wires: %d unknown targets, %d unknown inputs"),
			EW->Queue().Num(), EW->Queue().IsPaused() ? TEXT(" (paused)") : TEXT(""),
			EW->RingBuffer().Num(), EW->RingBuffer().Capacity(),
			EW->UnknownTargets(), EW->UnknownInputs());
		UE_LOG(LogElysiumWorld, Display, TEXT("brush bodies: %d | touches: %d begin, %d end"),
			EW->NumBrushBodies(), EW->TouchBegins(), EW->TouchEnds());
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldIoCmd(
	TEXT("elysium.world.io"),
	TEXT("elysium.world.io [n] — dump the last n I/O history lines from the ring buffer (default 40)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumWorldVerbsPrivate::ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world.io: no live world (load a map first)"));
			return;
		}
		const int32 N = Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 40;
		TArray<FString> Lines;
		EW->RingBuffer().CollectOrdered(N, Lines);
		UE_LOG(LogElysiumWorld, Display, TEXT("I/O history: %d lines (of %d recorded)"),
			Lines.Num(), EW->RingBuffer().Num());
		for (const FString& L : Lines)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("%s"), *L);
		}
	}));
