#include "Scripting/ElysiumScriptNatives.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNative, Log, All);

namespace
{
	// The engine `vampire` module: 11 global functions + 24 Character methods (python_bridge.md).
	// This one static table is the single source of truth — both hosts test membership against it
	// and the Scripting Cog window renders it.
	//
	// **`Whisper` and `FrenzyTrigger` are deliberately absent, and must stay absent.** `vamputil.py`
	// defines both as script helpers, and both are also datamap **input** names, so the binding is a
	// property of the call site rather than of the name (`docs/vtmb/script_api.md`): a bare
	// `Whisper("Crying")` (24 `.dlg` sites) runs the helper, which forwards to `pc.Whisper(...)`, while
	// the receiver-qualified `pc.Whisper("Crying")` (9 sites) fires the player datamap input directly;
	// `FrenzyTrigger(char)` hands the input a `1` where `pc.FrenzyTrigger()` hands it nothing. A row
	// here would resolve both spellings through this one surface and collapse that split — the bare
	// spelling would stop being the script's own function. The qualified spelling reaches its input
	// through the ordinary class-chain walk (`ElysiumPlayerClasses.cpp`), which is where it belongs.
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
		{ TEXT("OneOfSet"),            false, TEXT("1-of-N dialogue selector") },
		{ TEXT("IsClan"),              false, TEXT("player sheet clan") },
		{ TEXT("IsPCMalk"),            false, TEXT("player sheet clan") },
		// 24 Character methods
		{ TEXT("React"),               true,  TEXT("stub") },
		{ TEXT("SetExpression"),       true,  TEXT("stub") },
		{ TEXT("SetDisposition"),      true,  TEXT("stance only (9.9 owns reactions)") },
		{ TEXT("SetGesture"),          true,  TEXT("plays the named clip") },
		{ TEXT("HasItem"),             true,  TEXT("ordinary slots then the keyring") },
		{ TEXT("GiveItem"),            true,  TEXT("player-only GiveNamedItem") },
		{ TEXT("RemoveItem"),          true,  TEXT("stack decrement, else destroy, else keyring") },
		{ TEXT("AmmoCount"),           true,  TEXT("stack count / loaded magazine") },
		{ TEXT("GiveAmmo"),            true,  TEXT("stack count / reserve pool") },
		{ TEXT("HasWeaponEquipped"),   true,  TEXT("the active weapon's classname, case-sensitive") },
		{ TEXT("StartBarter"),         true,  TEXT("stub (9.8b owns barter and containers)") },
		{ TEXT("WorldMap"),            true,  TEXT("stub") },
		{ TEXT("SewerMap"),            true,  TEXT("stub") },
		{ TEXT("SetQuest"),            true,  TEXT("quest map + the catalogue's awards and journal") },
		{ TEXT("CurrentMoney"),        true,  TEXT("the combat character's money field") },
		{ TEXT("TakeDamage"),          true,  TEXT("damage onto the receiver's combat character") },
		{ TEXT("IsMale"),              true,  TEXT("player sheet") },
		{ TEXT("SeductiveFeed"),       true,  TEXT("stub") },
		{ TEXT("SetCamera"),           true,  TEXT("the scripted-shot channel") },
		{ TEXT("CalcFeat"),            true,  TEXT("the feat rating over the sheet") },
		{ TEXT("DialogDiscipline"),    true,  TEXT("stub (P13 owns using a power in dialogue)") },
		{ TEXT("BumpStat"),            true,  TEXT("dots onto the sheet's base") },
		{ TEXT("GetMasqueradeLevel"),  true,  TEXT("the masquerade counter") },
		{ TEXT("GetQuestState"),       true,  TEXT("quest map") },
		{ TEXT("IsFollowerOf"),        true,  TEXT("stub") },
	};

	// OneOfSet's roll. -1 = draw one per engine frame; >= 0 pins it.
	int32 GOneOfSetPinnedRoll = -1;
	FAutoConsoleVariableRef CVarOneOfSetRoll(
		TEXT("elysium.script.oneofset"),
		GOneOfSetPinnedRoll,
		TEXT("Pin OneOfSet's roll to a fixed value (>= 0) so a one-of-N dialogue set always selects the ")
		TEXT("same row, or -1 (default) to draw one roll per frame."),
		ECVF_Cheat);

	// One draw per frame, so every gate in a set sees the same roll (header). The frame is the unit
	// because a conversation turn gathers its whole choice list in one synchronous burst; a per-call
	// draw would let a 7-row set pass zero rows or two.
	int32 CurrentOneOfSetRoll()
	{
		if (GOneOfSetPinnedRoll >= 0)
		{
			return GOneOfSetPinnedRoll;
		}
		static uint64 RolledOnFrame = MAX_uint64;
		static int32 Roll = 0;
		if (RolledOnFrame != GFrameCounter)
		{
			RolledOnFrame = GFrameCounter;
			// The roll comes from the session's own OneOfSet stream, whose position is in the
			// save (`docs/architecture/save-architecture.md` §8). A conversation reopened after a load then selects the
			// same row it would have without one, which is the whole point of the per-frame draw.
			Roll = ElysiumRng::Stream(EElysiumRngStream::OneOfSet).RandHelper(MAX_int32);
		}
		return Roll;
	}

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
			return FElysiumVariant::Bool(State ? State->PlayerSheet().IsMale() : true);
		}
		if (Method == FName(TEXT("IsFollowerOf"))) { return FElysiumVariant::Bool(false); }
		if (Method == FName(TEXT("DialogDiscipline")))
		{
			return FElysiumVariant::Int(0);
		}
		return FElysiumVariant::Void();
	}

	// The receiver a Character method runs on. An UNSET handle is `FindPlayer()` — the script wrote
	// no receiver, which is the PC — so it resolves to the player rather than to nothing.
	FElysiumCombatCharacter* ResolveCharacter(FElysiumEntityWorld* World, const FElysiumEntityHandle& Self)
	{
		if (!World)
		{
			return nullptr;
		}
		FElysiumEntity* E = Self.IsSet() ? World->Resolve(Self) : nullptr;
		return E ? E->AsCombatCharacter() : static_cast<FElysiumCombatCharacter*>(World->FindPlayer());
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

	int32 OneOfSetRoll() { return CurrentOneOfSetRoll(); }
	void SetOneOfSetRoll(int32 PinnedRoll) { GOneOfSetPinnedRoll = PinnedRoll; }

	bool OneOfSet(int32 Which, int32 Count)
	{
		return Count > 0 && (CurrentOneOfSetRoll() % Count) == (Which - 1);
	}

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
		if (bStub)
		{
			// The one funnel every native return passes through, so hanging the stub report here
			// covers all 36 bindings plus the unlisted names a receiver binds on the spot. The
			// table's own `Backing` text is the owner line — it already says what each stub stands
			// in for, and keeping one source stops the two drifting.
			const FString Raw = Name.ToString();
			const ElysiumScriptNatives::FNativeBinding* B = FindBinding(Raw, /*bWantMethod*/ true);
			const bool bMethod = (B != nullptr);
			if (!B) { B = FindBinding(Raw, /*bWantMethod*/ false); }
			// Keyed on the qualified name rather than on `Display`, which carries the arguments —
			// one row per unimplemented native, not one per distinct call.
			ElysiumStub::Fired(TEXT("native"),
				FString::Printf(TEXT("%s.%s"), bMethod ? TEXT("Character") : TEXT("vampire"), *Raw),
				FString(), Display,
				B ? FString(B->Status)
				  : FString(TEXT("not a binding in the vampire module — returns its default")));
		}
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

		// Real backing: the game-state subsystem's quest funnel — the map write plus the completion
		// state's awards and journal row. The receiver is deliberately ignored, as VtMB's own
		// thunk ignores it: `SetQuest` always lands on the player (`docs/vtmb/script_api.md`).
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
		// TakeDamage lands on the RECEIVER, which is the whole point of the one call that uses it:
		// sp_theatre's `courtroom_camera_24` fires `Find('Sire2').TakeDamage(1000)` to destroy the
		// sire on camera at the climax of the trial. The class is `CBaseCombatCharacter`, so an NPC
		// answers for itself exactly as the PC does.
		if (Method == FName(TEXT("TakeDamage")))
		{
			FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			if (Char && Args.Num() >= 1)
			{
				Char->TakeDamage(Args[0].ToFloat());
			}
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ Char == nullptr);
			return FElysiumVariant::Void();
		}

		// CurrentMoney reads the receiver's own money field (it lives on the combat character,
		// which is where VtMB has it and where MoneyAdd/MoneyRemove write). The rest of the economy
		// — prices, barter, the HUD readout — is not this method.
		if (Method == FName(TEXT("CurrentMoney")))
		{
			const FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FElysiumVariant R = FElysiumVariant::Int(Char ? Char->Money : 0);
			Record(State, Method, Display, R, /*bStub*/ Char == nullptr);
			return R;
		}

		// The sheet-counter surface. All four read or write the RECEIVER's own sheet — the
		// class is `CBaseCombatCharacter`, so an NPC answers for itself exactly as the PC does.
		if (Method == FName(TEXT("CalcFeat")))
		{
			const FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			// The argument is the feat name; `LogArgs[0]` above has already normalised its casing
			// for the log, and the lookup itself is case-insensitive either way.
			const FString Name = Args.Num() >= 1 ? Args[0].ToString() : FString();
			const FElysiumVariant R = FElysiumVariant::Int(Char ? Char->CalcFeat(Name) : 0);
			Record(State, Method, Display, R, /*bStub*/ Char == nullptr);
			return R;
		}
		if (Method == FName(TEXT("BumpStat")))
		{
			FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			// (char, stat, times) — the third argument is a REPEAT COUNT, not an amount, and an
			// absent one is the single dot the two-argument call sites mean.
			const FString Stat = Args.Num() >= 1 ? Args[0].ToString() : FString();
			const int32 Times = Args.Num() >= 2 ? Args[1].ToInt() : 1;
			const int32 Landed = Char ? Char->BumpStat(Stat, Times) : 0;
			Record(State, Method, Display, FElysiumVariant::Int(Landed), /*bStub*/ Char == nullptr);
			return FElysiumVariant::Void();   // VtMB returns None
		}
		if (Method == FName(TEXT("GetMasqueradeLevel")))
		{
			const FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FElysiumVariant R = FElysiumVariant::Int(Char ? Char->GetMasqueradeLevel() : 0);
			Record(State, Method, Display, R, /*bStub*/ Char == nullptr);
			return R;
		}

		// The inventory surface (`docs/vtmb/inventory.md` §6).
		// All six read or write the RECEIVER's own inventory, except `GiveItem`, which is a
		// player-only service in retail and stays one here. Item names are classnames throughout.
		if (Method == FName(TEXT("HasItem")))
		{
			const FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FString Item = Args.Num() >= 1 ? Args[0].ToString() : FString();
			// Case-insensitive across all ordinary slots, then the carried keyring's records.
			const FElysiumVariant R = FElysiumVariant::Bool(Char && Char->Inventory.Has(*Char, Item));
			Record(State, Method, Display, R, /*bStub*/ Char == nullptr);
			return R;
		}
		if (Method == FName(TEXT("GiveItem")))
		{
			// Player-only: retail reaches `GiveNamedItem` through the receiver's player component,
			// and a receiver without one has nothing to grant into.
			FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
			FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FString Item = Args.Num() >= 1 ? Args[0].ToString() : FString();
			const bool bIsPlayer = Player != nullptr && Char == static_cast<FElysiumCombatCharacter*>(Player);
			const bool bGiven = bIsPlayer && Player->Inventory.GiveNamedItem(*Player, Item).IsSet();
			if (!bGiven)
			{
				// Retail's own line. A failed grant is a reproduced failure posture, not a gap.
				UE_LOG(LogElysiumNative, Log, TEXT("Could not give item (\"%s\")"), *Item);
			}
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ false);
			return FElysiumVariant::Void();   // VtMB returns None
		}
		if (Method == FName(TEXT("RemoveItem")))
		{
			FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FString Item = Args.Num() >= 1 ? Args[0].ToString() : FString();
			if (Char)
			{
				Char->Inventory.ScriptRemove(*Char, Item);
			}
			// **None even on no match** — the wrapper reports nothing either way.
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ Char == nullptr);
			return FElysiumVariant::Void();
		}
		if (Method == FName(TEXT("AmmoCount")))
		{
			const FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FString Item = Args.Num() >= 1 ? Args[0].ToString() : FString();
			// The ORDINARY slots only. A stackable item answers with its stack count; a
			// non-stackable one with its primary LOADED magazine; an item not owned, with zero.
			const FElysiumItem* Owned = Char ? Char->Inventory.FindOrdinary(*Char, Item) : nullptr;
			int32 Count = 0;
			if (Owned)
			{
				Count = Owned->IsStackable() ? Owned->ItemCount : Owned->MagazineCount;
			}
			const FElysiumVariant R = FElysiumVariant::Int(Count);
			Record(State, Method, Display, R, /*bStub*/ Char == nullptr);
			return R;
		}
		if (Method == FName(TEXT("GiveAmmo")))
		{
			FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FString Item = Args.Num() >= 1 ? Args[0].ToString() : FString();
			const int32 Amount = Args.Num() >= 2 ? Args[1].ToInt() : 0;
			// Requires the named item to be owned. A stackable item adds to its own count; a
			// non-stackable one adds RESERVE rounds of its primary ammo type to the character —
			// deliberately not the loaded magazine `AmmoCount` reads. The wrapper carries no clamp.
			if (FElysiumItem* Owned = Char ? Char->Inventory.FindOrdinary(*Char, Item) : nullptr)
			{
				if (Owned->IsStackable())
				{
					Owned->ItemCount += Amount;
				}
				else
				{
					Char->Inventory.AddReserve(Owned->AmmoType, Amount);
				}
			}
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ Char == nullptr);
			return FElysiumVariant::Void();
		}
		if (Method == FName(TEXT("HasWeaponEquipped")))
		{
			const FElysiumCombatCharacter* Char = ResolveCharacter(World, Self);
			const FString Item = Args.Num() >= 1 ? Args[0].ToString() : FString();
			// An EXACT, case-sensitive compare against the active weapon's classname — the one
			// inventory predicate that is not case-folded, and it does not mean "owned".
			const FElysiumItem* Active = Char ? Char->Inventory.Active(*Char) : nullptr;
			const FElysiumVariant R = FElysiumVariant::Bool(
				Active != nullptr && Active->ClassName().Equals(Item, ESearchCase::CaseSensitive));
			Record(State, Method, Display, R, /*bStub*/ Char == nullptr);
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
		// SetCamera(char, shotfile) — "Sets the entity to use the named shot file as their cinematic
		// camera mode" (ml_doc), so the argument keys `vdata/camerashots/` (115 calls).
		//
		// **The receiver must be the player.** `vampire.dll FUN_10198070` resolves the actor, reads
		// `ent + 0xa8` — the cached `CBasePlayer*`, non-NULL only for players, the same field
		// `CDialog::Acquire` and `CanStartGrappleAttack` use to mean "this entity is a player" — and
		// calls `CBasePlayer::SetCamera` on it; a receiver that is not a player raises
		// `"SetShot needs to be called on a v..."` and **nothing happens**. Every shipped call site
		// spells it `pc.SetCamera("Shot")`, and the shot's own subject is therefore always player 1,
		// which is what makes a `DialogTarget` anchor mean "the player's dialogue partner" (RC6/RC13).
		if (Method == FName(TEXT("SetCamera")) && World && Args.Num() >= 1)
		{
			const bool bReceiverIsPlayer = !Self.IsSet() || World->PlayerHandle() == Self;
			if (!bReceiverIsPlayer)
			{
				UE_LOG(LogElysiumNative, Warning,
					TEXT("SetShot needs to be called on a valid player: %s"), *Display);
				Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ false);
				return FElysiumVariant::Void();
			}
			// `param_3` is NULL on both of `FUN_1017d020`'s arms, so the shot's subject is
			// `UTIL_PlayerByIndex(1)`; the world supplies it.
			World->SetScriptedCamera(Args[0].ToString(), FElysiumEntityHandle::Invalid());
			const bool bUp = World->HasScriptedCamera();
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ !bUp);
			return FElysiumVariant::Void();
		}
		// SetDisposition(char, name, level) — 2,510 calls, 2,467 of them a `.dlg` line's action.
		// One transaction updates the resolved row, stance transition, expression, gaze/blink policy
		// and saved level. It does not touch combat relationships or the RPG reaction score.
		if (Method == FName(TEXT("SetDisposition")) && World && Args.Num() >= 2)
		{
			FElysiumEntity* E = World->Resolve(Self);
			FElysiumCombatCharacter* Character = E ? E->AsCombatCharacter() : nullptr;
			const bool bApplied = Character
				&& Character->SetDisposition(Args[0].ToString(), Args[1].ToInt());
			Record(State, Method, Display, FElysiumVariant::Void(), /*bStub*/ !bApplied);
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
			// ChangeMap(delay, landmark, trigger): the scripted map transition. After `delay`
			// seconds, activate the named trigger_changelevel — it carries the destination map + the
			// landmark, so this reduces to path 2 (level_transitions.md). Enqueue its ChangeNow input
			// through the real event queue (chokepoint 2), so it single-steps in the Event Queue window
			// and the transition runs deferred (not inside this eval). `ChangeNow` is the name the maps'
			// own wires use, so the scripted and entity paths fire one input. Arg 1 (landmark) is the
			// trigger's own landmark key — carried only for the log; the trigger reads its own
			// map/landmark.
			if (World && Args.Num() >= 3)
			{
				World->EnqueueInput(Args[2].ToString(), FName(TEXT("ChangeNow")), FElysiumVariant::Void(),
					Args[0].ToFloat(), Ctx.Activator, Ctx.Self);
			}
			Record(State, Name, Display, FElysiumVariant::Void(), /*bStub*/ false);
			return FElysiumVariant::Void();
		}
		if (Name == FName(TEXT("ScheduleTask")))
		{
			// ScheduleTask(delay, "<source>"): defer the source string on the event queue,
			// evaluated at now+delay through the installed host — the real deferred-task mechanism, not
			// a stub. Provenance is this eval's `!self` (the scheduling entity).
			if (World && Args.Num() >= 2)
			{
				World->EnqueuePython(Args[1].ToString(), Args[0].ToFloat(), Ctx.Activator, Ctx.Self);
			}
			Record(State, Name, Display, FElysiumVariant::Void(), /*bStub*/ false);
			return FElysiumVariant::Void();
		}

		// OneOfSet(which, count) — the 1-based one-of-N dialogue selector, `(roll % count) == which - 1`
		// over an engine counter (`docs/vtmb/script_api.md`; the roll model is in this module's header).
		// A hardcoded false would fail all 589 gates riding on it closed.
		if (Name == FName(TEXT("OneOfSet")))
		{
			const int32 Which = Args.Num() >= 1 ? Args[0].ToInt() : 0;
			const int32 Count = Args.Num() >= 2 ? Args[1].ToInt() : 0;
			// VtMB returns PyInt_FromLong, so the result is the integer 0/1, not a bool.
			const FElysiumVariant R = FElysiumVariant::Int(OneOfSet(Which, Count) ? 1 : 0);
			Record(State, Name, Display, R, /*bStub*/ false);
			return R;
		}

		// IsClan(character, "ClanName") / IsPCMalk() read the player sheet clan (the 2..8 encoding).
		// Only the PC carries a sheet in this slice — NPCs have no clan model yet — so both answer for
		// the player; the character argument is accepted (and logged) but not otherwise consulted.
		if (Name == FName(TEXT("IsClan")) || Name == FName(TEXT("IsPCMalk")))
		{
			const int32 Have = State ? State->PlayerSheet().Clan() : 0;
			const int32 Want = (Name == FName(TEXT("IsPCMalk")))
				? FElysiumSheet::ClanFromName(TEXT("Malkavian"))
				: (Args.Num() >= 2 ? FElysiumSheet::ClanFromName(Args.Last().ToString()) : 0);
			const FElysiumVariant Clan = FElysiumVariant::Bool(Want != 0 && Have == Want);
			Record(State, Name, Display, Clan, /*bStub*/ false);
			return Clan;
		}

		// The rest have no backing yet — log a stub and return a plausible default. Predicate-shaped
		// globals read false so a gate over them fails closed (error-to-false's spirit).
		FElysiumVariant R = FElysiumVariant::Void();
		if (Name == FName(TEXT("SquadSeesPlayer")))
		{
			R = FElysiumVariant::Bool(false);
		}
		Record(State, Name, Display, R, /*bStub*/ true);
		return R;
	}
}
