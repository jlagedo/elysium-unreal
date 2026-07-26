#include "ElysiumScriptNatives.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNative, Log, All);

namespace
{
	// The engine `vampire` module: 11 global functions + 24 Character methods (python_bridge.md).
	// This one static table is the single source of truth — both hosts test membership against it
	// and the Scripting Cog window renders it.
	const ElysiumScriptNatives::FNativeBinding GNativeBindings[] = {
		// 11 module globals
		{ TEXT("FindPlayer"),          false, TEXT("player object") },
		{ TEXT("FindEntityByName"),    false, TEXT("entity lookup") },
		{ TEXT("FindEntitiesByName"),  false, TEXT("entity list (cpython host)") },
		{ TEXT("FindEntitiesByClass"), false, TEXT("entity list (cpython host)") },
		{ TEXT("ScheduleTask"),        false, TEXT("defers source on the event queue") },
		{ TEXT("SquadSeesPlayer"),     false, TEXT("stub (unused by shipped scripts)") },
		{ TEXT("CreateEntityNoSpawn"), false, TEXT("runtime create, no spawn (cpython host)") },
		{ TEXT("CallEntitySpawn"),     false, TEXT("runtime spawn (cpython host)") },
		{ TEXT("ChangeMap"),           false, TEXT("fires a trigger_changelevel after delay") },
		{ TEXT("OneOfSet"),            false, TEXT("stub") },
		{ TEXT("IsClan"),              false, TEXT("player sheet clan") },
		{ TEXT("IsPCMalk"),            false, TEXT("player sheet clan") },
		// 24 Character methods
		{ TEXT("React"),               true,  TEXT("stub") },
		{ TEXT("SetExpression"),       true,  TEXT("stub") },
		{ TEXT("SetDisposition"),      true,  TEXT("stance only (9.9 owns reactions)") },
		{ TEXT("SetGesture"),          true,  TEXT("plays the named clip") },
		{ TEXT("HasItem"),             true,  TEXT("stub (no inventory)") },
		{ TEXT("GiveItem"),            true,  TEXT("stub (no inventory)") },
		{ TEXT("RemoveItem"),          true,  TEXT("stub (no inventory)") },
		{ TEXT("AmmoCount"),           true,  TEXT("stub (no inventory)") },
		{ TEXT("GiveAmmo"),            true,  TEXT("stub (no inventory)") },
		{ TEXT("HasWeaponEquipped"),   true,  TEXT("stub (no inventory)") },
		{ TEXT("StartBarter"),         true,  TEXT("stub") },
		{ TEXT("WorldMap"),            true,  TEXT("stub") },
		{ TEXT("SewerMap"),            true,  TEXT("stub") },
		{ TEXT("SetQuest"),            true,  TEXT("quest map") },
		{ TEXT("CurrentMoney"),        true,  TEXT("stub (no character sheet)") },
		{ TEXT("IsMale"),              true,  TEXT("player sheet") },
		{ TEXT("SeductiveFeed"),       true,  TEXT("stub") },
		{ TEXT("SetCamera"),           true,  TEXT("stub") },
		{ TEXT("CalcFeat"),            true,  TEXT("stub (no character sheet)") },
		{ TEXT("DialogDiscipline"),    true,  TEXT("stub") },
		{ TEXT("BumpStat"),            true,  TEXT("stub (no character sheet)") },
		{ TEXT("GetMasqueradeLevel"),  true,  TEXT("stub (no masquerade meter)") },
		{ TEXT("GetQuestState"),       true,  TEXT("quest map") },
		{ TEXT("IsFollowerOf"),        true,  TEXT("stub") },
	};

	const ElysiumScriptNatives::FNativeBinding* FindBinding(const FString& Name, bool bWantMethod)
	{
		for (const ElysiumScriptNatives::FNativeBinding& B : GNativeBindings)
		{
			if (B.bMethod == bWantMethod && Name.Equals(B.Name, ESearchCase::CaseSensitive)) { return &B; }
		}
		return nullptr;
	}

	// The default return for a stubbed Character method (no backing system yet). Predicate-shaped
	// methods read false so a gate over them fails closed (error-to-false's spirit).
	FElysiumVariant CharMethodStubResult(FName Method, UElysiumGameStateSubsystem* State)
	{
		if (Method == FName(TEXT("IsMale")))
		{
			return FElysiumVariant::Bool(State ? State->PlayerSheet().bMale : true);
		}
		if (Method == FName(TEXT("HasItem")) || Method == FName(TEXT("HasWeaponEquipped"))
			|| Method == FName(TEXT("IsFollowerOf"))) { return FElysiumVariant::Bool(false); }
		if (Method == FName(TEXT("AmmoCount")) || Method == FName(TEXT("CurrentMoney"))
			|| Method == FName(TEXT("CalcFeat")) || Method == FName(TEXT("GetMasqueradeLevel"))
			|| Method == FName(TEXT("DialogDiscipline"))) { return FElysiumVariant::Int(0); }
		return FElysiumVariant::Void();
	}
}

namespace ElysiumScriptNatives
{
	TArrayView<const FNativeBinding> NativeBindings()
	{
		return MakeArrayView(GNativeBindings, UE_ARRAY_COUNT(GNativeBindings));
	}

	bool IsNativeGlobal(const FString& Name) { return FindBinding(Name, /*bWantMethod*/ false) != nullptr; }
	bool IsCharacterMethod(const FString& Name) { return FindBinding(Name, /*bWantMethod*/ true) != nullptr; }

	FString CanonicalStatName(const FString& Raw)
	{
		static const TCHAR* const Known[] = {
			// attributes / feats
			TEXT("Strength"), TEXT("Dexterity"), TEXT("Stamina"), TEXT("Charisma"),
			TEXT("Manipulation"), TEXT("Appearance"), TEXT("Perception"), TEXT("Intelligence"),
			TEXT("Wits"), TEXT("Humanity"),
			// skills / disciplines used in checks
			TEXT("Persuasion"), TEXT("Seduction"), TEXT("Intimidate"), TEXT("Dominate"),
			TEXT("Dementation"), TEXT("Haggle"), TEXT("Firearms"), TEXT("Research"),
			TEXT("Brawl"), TEXT("Melee"), TEXT("Dodge"), TEXT("Stealth"), TEXT("Security"),
			TEXT("Lockpick"), TEXT("Hacking"), TEXT("Scholarship"), TEXT("Awareness"),
			TEXT("Inspection"), TEXT("Blood"), TEXT("Health"),
		};
		for (const TCHAR* K : Known)
		{
			if (Raw.Equals(K, ESearchCase::IgnoreCase)) { return FString(K); }
		}
		return Raw;   // unknown stat: keep as written
	}

	FString DescribeArgs(TArrayView<const FElysiumVariant> Args)
	{
		FString Out;
		for (int32 i = 0; i < Args.Num(); ++i)
		{
			if (i > 0) { Out += TEXT(", "); }
			Out += Args[i].Describe();
		}
		return Out;
	}

	void Record(UElysiumGameStateSubsystem* State, FName Name, const FString& Display,
		const FElysiumVariant& Result, bool bStub)
	{
		UE_LOG(LogElysiumNative, Verbose, TEXT("[native%s] %s -> %s"),
			bStub ? TEXT(" stub") : TEXT(""), *Display, *Result.Describe());
		if (State) { State->RecordNativeCall(Display, Result, bStub, Name); }
	}

	FElysiumVariant CallCharacterMethod(UElysiumGameStateSubsystem* State, FElysiumEntityWorld* World,
		const FElysiumEntityHandle& Self, FName Method, TArrayView<const FElysiumVariant> Args)
	{
		// Receiver label: the PC (FindPlayer()) or the resolved NPC handle.
		FString Recv = TEXT("FindPlayer()");
		if (Self.IsSet()) { Recv = World ? World->DescribeHandle(Self) : Self.ToString(); }

		// Stat-taking methods normalise their (case-inconsistent) stat name so the log reads one
		// spelling — the case-insensitive stat-name requirement (python_bridge.md).
		TArray<FElysiumVariant> LogArgs(Args.GetData(), Args.Num());
		if ((Method == FName(TEXT("BumpStat")) || Method == FName(TEXT("CalcFeat")))
			&& LogArgs.Num() > 0 && LogArgs[0].IsString())
		{
			LogArgs[0] = FElysiumVariant::String(CanonicalStatName(LogArgs[0].AsString));
		}
		const FString Display = FString::Printf(TEXT("%s.%s(%s)"), *Recv, *Method.ToString(), *DescribeArgs(LogArgs));

		// Real backing: the quest map on the game-state subsystem.
		if (Method == FName(TEXT("SetQuest")))
		{
			if (State && Args.Num() >= 2) { State->SetQuestState(Args[0].ToString(), Args[1].ToInt()); }
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ false);
			return FElysiumVariant::Void();
		}
		if (Method == FName(TEXT("GetQuestState")))
		{
			const int32 Q = (State && Args.Num() >= 1) ? State->GetQuestState(Args[0].ToString()) : 0;
			const FElysiumVariant R = FElysiumVariant::Int(Q);
			Record(State, Method, Display, R, /*bStub*/ false);
			return R;
		}
		if (Method == FName(TEXT("IsMale")))
		{
			const FElysiumVariant R = CharMethodStubResult(Method, State);
			Record(State, Method, Display, R, /*bStub*/ false);
			return R;
		}

		// The animation half of the character surface (8.5). Both reach the receiver's body through
		// FElysiumEntity's virtual seam, so the host needs no knowledge of the NPC leaf.
		//
		// SetGesture(char, sequence) — "Sets the entity to play the named sequence" (ml_doc). A
		// one-shot: the script names a gesture, not a new resting pose.
		if (Method == FName(TEXT("SetGesture")) && World && Args.Num() >= 1)
		{
			FElysiumEntity* E = World->Resolve(Self);
			const bool bPlayed = E && E->PlayAnimClip(Args[0].ToString(), /*bLoop=*/true);
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ !bPlayed);
			return FElysiumVariant::Void();
		}
		// SetDisposition(char, name, level) — 2,510 calls, 2,467 of them a `.dlg` line's action.
		// Only the animation half is answered here: the NPC re-picks its standing stance from the
		// disposition table. The emotional-state model and `level` are 9.9's, so this still records
		// as a stub — the coverage report must not claim more than it does.
		if (Method == FName(TEXT("SetDisposition")) && World && Args.Num() >= 1)
		{
			FElysiumEntity* E = World->Resolve(Self);
			if (E) { E->SetDispositionName(Args[0].ToString()); }
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ true);
			return FElysiumVariant::Void();
		}

		// Everything else logs a stub and returns its default (an unlisted method — the receiver
		// binds any name — falls here too, so e.g. ClearActiveDisciplines runs without raising).
		const FElysiumVariant R = CharMethodStubResult(Method, State);
		Record(State, Method, Display, R, /*bStub*/ true);
		return R;
	}

	FElysiumVariant CallSimpleGlobal(UElysiumGameStateSubsystem* State, FElysiumEntityWorld* World,
		const FElysiumScriptContext& Ctx, FName Name, TArrayView<const FElysiumVariant> Args)
	{
		const FString Display = FString::Printf(TEXT("%s(%s)"), *Name.ToString(), *DescribeArgs(Args));

		if (Name == FName(TEXT("ChangeMap")))
		{
			// ChangeMap(delay, landmark, trigger) (P4.6): the scripted map transition. After `delay`
			// seconds, activate the named trigger_changelevel — it carries the destination map + the
			// landmark, so this reduces to path 2 (level_transitions.md). Enqueue its ChangeLevel input
			// through the real event queue (chokepoint 2), so it single-steps in the Event Queue window
			// and the transition runs deferred (not inside this eval). Arg 1 (landmark) is the trigger's
			// own landmark key — carried only for the log; the trigger reads its own map/landmark.
			if (World && Args.Num() >= 3)
			{
				World->EnqueueInput(Args[2].ToString(), FName(TEXT("ChangeLevel")), FElysiumVariant::Void(),
					Args[0].ToFloat(), Ctx.Activator, Ctx.Self);
			}
			Record(State, Name, Display, FElysiumVariant::Void(), /*bStub*/ false);
			return FElysiumVariant::Void();
		}
		if (Name == FName(TEXT("ScheduleTask")))
		{
			// ScheduleTask(delay, "<source>") (5.4): defer the source string on the event queue,
			// evaluated at now+delay through the installed host — the real deferred-task mechanism, not
			// a stub. Provenance is this eval's `!self` (the scheduling entity).
			if (World && Args.Num() >= 2)
			{
				World->EnqueuePython(Args[1].ToString(), Args[0].ToFloat(), Ctx.Activator, Ctx.Self);
			}
			Record(State, Name, Display, FElysiumVariant::Void(), /*bStub*/ false);
			return FElysiumVariant::Void();
		}

		// IsClan(character, "ClanName") / IsPCMalk() read the player sheet clan (the 2..8 encoding).
		// Only the PC carries a sheet in this slice — NPCs have no clan model yet — so both answer for
		// the player; the character argument is accepted (and logged) but not otherwise consulted.
		if (Name == FName(TEXT("IsClan")) || Name == FName(TEXT("IsPCMalk")))
		{
			const int32 Have = State ? State->PlayerSheet().Clan : 0;
			const int32 Want = (Name == FName(TEXT("IsPCMalk")))
				? FElysiumPlayerSheet::ClanFromName(TEXT("Malkavian"))
				: (Args.Num() >= 2 ? FElysiumPlayerSheet::ClanFromName(Args.Last().ToString()) : 0);
			const FElysiumVariant Clan = FElysiumVariant::Bool(Want != 0 && Have == Want);
			Record(State, Name, Display, Clan, /*bStub*/ false);
			return Clan;
		}

		// The rest have no backing yet — log a stub and return a plausible default. Predicate-shaped
		// globals read false so a gate over them fails closed (error-to-false's spirit).
		FElysiumVariant R = FElysiumVariant::Void();
		if (Name == FName(TEXT("SquadSeesPlayer")) || Name == FName(TEXT("OneOfSet")))
		{
			R = FElysiumVariant::Bool(false);
		}
		Record(State, Name, Display, R, /*bStub*/ true);
		return R;
	}
}
