// 8.5 — `scripted_sequence` / `aiscripted_sequence`: VtMB's cutscene beat.
//
// The class is **`CCineNPC`** in `vampire.dll` (RE: the `aiscripted_sequence` factory at
// `101a8fe0` builds vftable `10477d1c`, whose datamap `10593628` names the class) — an HL1
// `CCineMonster` derivative, not HL2's `CAI_ScriptedSequence`. That lineage fixes spawnflag bits
// 1..128 as WAITTILLSEEN / EXITAGITATED / REPEATABLE / LEAVECORPSE / START_ON_SPAWN / NOINTERRUPT /
// OVERRIDESTATE / NOSCRIPTMOVEMENT. Bits 256, 512, 4096 and 8192 are VtMB additions and their
// meanings are **not established** — nothing here reads them.
//
// A beat is: send the named NPC to this marker, turn it onto the marker's angles, play its action
// animation, then hold a post-idle — and, on either side of it, fire `OnBeginSequence` /
// `OnEndSequence`. The **outputs are the load-bearing half**: across the exported maps 88 wires
// leave these entities, 48 of them `OnEndSequence`, and they unlock Chunk's gallery door, restore
// the camera, and start Jack's next conversation. A beat that never ends stalls the map's script
// flow, which is why every path through this class ends the beat, including every failure.
//
// The beat runs in three phases. **Travel** covers `m_fMoveTo` 1 (Walk) / 2 (Run) / 3 (Custom, over
// `m_iszCustomMove`'s own cycle) / 5 (turn to face) and is handed to the NPC's own movement motor
// through `FElysiumEntity::BeginScriptMove`; 4 (Instantaneous) is a placement and 0 ("No") touches
// nothing. **Action** starts once the mark is reached, so `m_iszPlay` times `OnEndSequence` from
// arrival rather than from the input. **End** holds `m_iszPostIdle` and chains `m_iszNextScript`.
// Across the exported maps 132 of 188 sequences travel: 54 walk, 56 run, 9 custom, 9 instant, 4
// turn — the walk-out of sp_theatre's courtroom is five of them, ~19 metres of authored transit.
//
// Travel needs a body with a motor. Without one — the `!playercontroller` stand-in (10 sequences),
// a bodiless record, `elysium.NpcBodies 0`, a menu backdrop, an unbuilt navigation graph, a mark
// with no path, or any headless test — the NPC is **placed on the marker** instead: the authored
// end state without the transit, and the beat runs on unchanged. That is also what
// `elysium.SeqLocomotion 0` selects.
//
// What is reproduced: BeginSequence/CancelSequence (68 + 16 I/O wires, plus 68 + 8 receiver-qualified
// script calls through the same datamap lookup), the travel phase and its gait, the turn onto the
// mark, `m_iszPlay` timing `OnEndSequence`, `m_iszPostIdle` held after it, `m_iszIdle` as the pose
// the NPC waits in from map load, and `m_iszNextScript` chaining.
//
// What is NOT: root movement. The gait speeds come from `speed_walk`/`speed_runbase`
// (`docs/vtmb/source_movement.md`) rather than from the cycle's own displacement, which is not decoded
// (`docs/vtmb/animation_and_movers.md` A.3), so a travelling NPC can foot-slide. `OnScriptEvent01..08`
// (5 wires) need decoded animation events and do not fire.
//
// **Not RE-established:** whether VtMB fires `OnBeginSequence` at the input or on arrival at the
// mark. It fires at the input here, which is the order every wire in the exported maps was authored
// against — `OnEndSequence` is the one that moves.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"

#include "HAL/IConsoleManager.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSeq, Log, All);

// A/B for the travel half. 0 sends no NPC to its mark under its own power and takes the placement
// path for every `m_fMoveTo`, which is what a beat does anyway wherever there is no motor.
static TAutoConsoleVariable<int32> CVarSeqLocomotion(
	TEXT("elysium.SeqLocomotion"),
	1,
	TEXT("scripted_sequence walks/runs its NPC to the marker (1, default) or places it there (0)."),
	ECVF_Default);

// A/B for the placement half. 0 leaves every NPC where it stands and runs the beat as animation +
// outputs; the outputs fire either way.
static TAutoConsoleVariable<int32> CVarSeqTeleport(
	TEXT("elysium.SeqTeleport"),
	1,
	TEXT("scripted_sequence places its NPC at the marker when it cannot travel there (1, default) or leaves it where it stands (0)."),
	ECVF_Default);

namespace
{
	// HL1 CCineMonster: the mapper's "don't move the NPC to the mark" override. One entity in the
	// exported set carries it (`spawnflags 132`).
	constexpr int32 SF_SCRIPT_NOSCRIPTMOVEMENT = 128;

	// m_fMoveTo — how the NPC is meant to reach the mark.
	constexpr int32 MOVETO_NONE = 0;          // "No" — already in place, do not touch it
	constexpr int32 MOVETO_WALK = 1;
	constexpr int32 MOVETO_RUN = 2;
	constexpr int32 MOVETO_CUSTOM = 3;        // travel over `m_iszCustomMove`'s own cycle
	constexpr int32 MOVETO_INSTANT = 4;       // "Instantaneous" — a placement, not a journey
	constexpr int32 MOVETO_TURN_TO_FACE = 5;  // "No - Turn to Face" — take the angles, not the origin

	// How often a beat samples its NPC's travel. Matches the patrol cadence: the body itself moves
	// on the engine tick, so this only paces the substrate's own read of where it got to.
	constexpr double TRAVEL_TICK_SECONDS = 0.05;

	// Subclass-member field accessor. Mirrors AddNpcField / AddSignField — file-unique name so all of
	// them can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddSeqField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddSeqField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// FElysiumScriptedSequence — one cutscene beat.
// ============================================================================================

class FElysiumScriptedSequence final : public FElysiumEntity
{
public:
	// --- The authored beat (datamap field names, applied at Construct) ----------------------
	FString TargetEntity;   // m_iszEntity — the NPC's targetname. `!playercontroller` on 10 of 108.
	FString PreIdle;        // m_iszIdle — the pose the NPC waits in until the beat is triggered
	FString PreIdleAlt;     // m_iszPreIdle — the binary carries both names; no exported map uses this one
	FString Play;           // m_iszPlay — the action animation; its length IS the beat's duration
	FString PostIdle;       // m_iszPostIdle — held (looping) once the action ends
	FString CustomMove;     // m_iszCustomMove — the travel animation for m_fMoveTo 3; nothing drives it
	FString NextScript;     // m_iszNextScript — the beat to begin when this one ends
	int32   MoveTo = 0;     // m_fMoveTo — 0 No / 1 Walk / 2 Run / 3 Custom / 4 Instant / 5 Turn to face
	float   Radius = 0.f;   // m_flRadius — the engine's NPC search radius; every m_iszEntity here is a
	                        // plain targetname, so the name index resolves it and the radius is unused
	float   Repeat = 0.f;   // m_flRepeat — repeat rate (ms); 104 of 108 are 0

	// --- Live state -------------------------------------------------------------------------
	// The beat's three phases. `Travel` is the NPC walking/running/turning onto the mark, `Action`
	// is `m_iszPlay` running out; both are "between OnBeginSequence and OnEndSequence".
	enum class EPhase : uint8 { Idle, Travel, Action };
	EPhase Phase = EPhase::Idle;
	FElysiumEntityHandle Activator;        // whoever fired BeginSequence, carried to OnEndSequence
	bool bTravelled = false;               // this beat moved the NPC under its own power

	// The RE'd CCineNPC::Use throttle (vampire.dll FUN_101a7390): a BeginSequence arriving
	// before this gate is dropped instead of acted on, and the gate is pushed further out. A
	// successful call sets it to Now + 0.05s (the same constant recovered from the binary). This
	// is what keeps a self-chaining m_iszNextScript (e.g. sm_asylum_1's Jeanette_in_elevator,
	// which names itself) from retriggering inside the same zero-delay drain pass — VtMB never
	// runs the chain more than once per tick, so it never becomes a same-frame loop.
	double NextAllowedBeginTime = -1.0;

	// The NPC this beat drives, or null when it has not spawned yet (an npc_maker child), is gone,
	// or names an unsupported engine alias. A null target still runs the beat as a timing shell so
	// outputs fire and the map's flow continues.
	FElysiumEntity* ResolveTarget() const
	{
		if (!World || TargetEntity.IsEmpty())
		{
			return nullptr;
		}
		if (TargetEntity.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
		{
			return World->FindPlayerController();
		}
		if (TargetEntity.StartsWith(TEXT("!")))
		{
			return nullptr;
		}
		return World->FindByName(TargetEntity);
	}

	// Does this beat move its NPC at all? `m_fMoveTo 0` means "already in place", and the mapper's
	// NOSCRIPTMOVEMENT override says the same thing louder.
	bool MovesTheNpc() const
	{
		return MoveTo != MOVETO_NONE && (SpawnFlags & SF_SCRIPT_NOSCRIPTMOVEMENT) == 0;
	}

	// The gait `m_fMoveTo` asks for, or none for the two values that are placements rather than
	// journeys (0 "No" and 4 "Instantaneous").
	bool TravelGait(EElysiumScriptGait& OutGait) const
	{
		switch (MoveTo)
		{
		case MOVETO_WALK:         OutGait = EElysiumScriptGait::Walk;   return true;
		case MOVETO_RUN:          OutGait = EElysiumScriptGait::Run;    return true;
		case MOVETO_CUSTOM:       OutGait = EElysiumScriptGait::Custom; return true;
		case MOVETO_TURN_TO_FACE: OutGait = EElysiumScriptGait::Face;   return true;
		default:                  return false;
		}
	}

	// Send the NPC to the mark under its own power. False when this beat does not travel, when the
	// A/B is off, or when the NPC has no motor to travel with — every one of which falls through to
	// PlaceOnMark, so the beat reaches the same end state either way.
	bool StartTravel(FElysiumEntity* Npc)
	{
		EElysiumScriptGait Gait = EElysiumScriptGait::Walk;
		if (Npc == nullptr || !MovesTheNpc() || MoveTo == MOVETO_INSTANT
			|| CVarSeqLocomotion.GetValueOnGameThread() == 0 || !TravelGait(Gait))
		{
			return false;
		}
		return Npc->BeginScriptMove(Origin, Angles, Gait, CustomMove);
	}

	// Put the NPC on the mark at once. VtMB's `m_fMoveTo 4`, and this runtime's stand-in wherever
	// travel is unavailable. 5 means "turn to face" and takes the angles only.
	void PlaceOnMark(FElysiumEntity* Npc) const
	{
		if (Npc == nullptr || !MovesTheNpc() || CVarSeqTeleport.GetValueOnGameThread() == 0)
		{
			return;
		}
		if (MoveTo != MOVETO_TURN_TO_FACE)
		{
			Npc->SetRuntimeOrigin(Origin);
		}
		Npc->SetRuntimeAngles(Angles);
	}

	// BeginSequence — 68 I/O wires + 68 receiver-qualified script calls (`script.BeginSequence()`),
	// both arriving through this one registered input (python_bridge.md: a Python attribute and a
	// Hammer input are the same namespace).
	void InputBeginSequence(const FElysiumInputArgs& Args)
	{
		if (IsInert())
		{
			return;
		}

		const double Now = World ? World->NowSeconds() : 0.0;
		if (Now < NextAllowedBeginTime)
		{
			NextAllowedBeginTime += 0.05;
			UE_LOG(LogElysiumSeq, Verbose,
				TEXT("%s BeginSequence throttled (retriggered within 0.05s of the last call)"),
				*DebugString());
			return;
		}
		NextAllowedBeginTime = Now + 0.05;

		Activator = Args.Activator;

		FElysiumEntity* Npc = ResolveTarget();

		static const FName OnBeginSequence(TEXT("OnBeginSequence"));
		FireOutput(OnBeginSequence, Activator);

		bTravelled = StartTravel(Npc);
		if (bTravelled)
		{
			Phase = EPhase::Travel;
			NextThink = static_cast<float>(Now + TRAVEL_TICK_SECONDS);
			UE_LOG(LogElysiumSeq, Verbose, TEXT("%s BeginSequence -> %s travelling to %s move=%d"),
				*DebugString(), *Npc->DebugString(), *Origin.ToString(), MoveTo);
			return;
		}

		PlaceOnMark(Npc);
		StartAction(Npc);
	}

	// The mark has been reached (or the beat never travelled): run `m_iszPlay`, whose length is the
	// beat's remaining duration. No action — 59 of the 108 exported sequences are movement-only —
	// means the beat ends here, in the same pass for a placement and on arrival for a walk.
	void StartAction(FElysiumEntity* Npc)
	{
		Phase = EPhase::Action;

		float Seconds = 0.f;
		if (Npc != nullptr && !Play.IsEmpty())
		{
			Npc->PlayAnimClip(Play, /*bLoop=*/false, &Seconds);
		}
		else if (Npc != nullptr && bTravelled)
		{
			// A travel-only beat leaves the NPC standing in its walk cycle otherwise. VtMB hands the
			// NPC back to AI here, which idles it; the stance idle is this runtime's nearest thing.
			Npc->ResetAnimToIdle();
		}
		UE_LOG(LogElysiumSeq, Verbose, TEXT("%s action -> %s play='%s' (%.2fs) move=%d"),
			*DebugString(), Npc ? *Npc->DebugString() : TEXT("(no body)"), *Play, Seconds, MoveTo);

		if (Seconds > 0.f)
		{
			NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + Seconds);
		}
		else
		{
			EndSequence();
		}
	}

	// One travel tick. The NPC owns the move; this reads how it is going and decides when the
	// action starts. Every non-Moving answer starts it, so no failure can leave the beat hanging.
	void ThinkTravel()
	{
		FElysiumEntity* Npc = ResolveTarget();
		const EElysiumScriptMove Status = Npc != nullptr && !Npc->IsDead()
			? Npc->AdvanceScriptMove() : EElysiumScriptMove::Failed;
		if (Status == EElysiumScriptMove::Moving)
		{
			NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + TRAVEL_TICK_SECONDS);
			return;
		}
		if (Npc != nullptr)
		{
			Npc->EndScriptMove();
			if (Status != EElysiumScriptMove::Arrived)
			{
				// The NPC could not get there. The mark is the beat's authored end state and
				// everything downstream is placed against it, so put it there.
				UE_LOG(LogElysiumSeq, Log, TEXT("%s travel did not reach the mark; placing %s on it"),
					*DebugString(), *Npc->DebugString());
				PlaceOnMark(Npc);
			}
		}
		StartAction(Npc);
	}

	// CancelSequence — 16 I/O wires + 8 script calls. Used to interrupt a beat before starting the
	// next one (`logic_shot_5` cancels sSabbat1_4 and begins sSabbat1_5). VtMB fires OnCancelSequence
	// here; no exported map wires it, so nothing is fired. `OnEndSequence` deliberately does NOT fire
	// — a cancelled beat must not unlock the door its completion would have.
	void InputCancelSequence(const FElysiumInputArgs&)
	{
		if (Phase == EPhase::Idle)
		{
			return;
		}
		Phase = EPhase::Idle;
		NextThink = ELYSIUM_NEVER_THINK;
		if (FElysiumEntity* Npc = ResolveTarget())
		{
			// The travel is abandoned where it stands — a cancelled beat is not a completed one, so
			// the NPC is not placed on a mark it never reached.
			Npc->EndScriptMove();
			if (!Play.IsEmpty() || bTravelled)
			{
				Npc->ResetAnimToIdle();   // cut short; VtMB hands the NPC back to AI, which idles it
			}
		}
		bTravelled = false;
		UE_LOG(LogElysiumSeq, Verbose, TEXT("%s CancelSequence"), *DebugString());
	}

	// The action animation ran out (or there was none): hold the post-idle, fire OnEndSequence, and
	// hand off to the next script.
	void EndSequence()
	{
		Phase = EPhase::Idle;
		bTravelled = false;
		NextThink = ELYSIUM_NEVER_THINK;

		// Where the beat leaves the NPC standing. The post-idle loops — it is a resting pose
		// (`submachinegun_ready`, `cower_idle`, `crouch`), so playing it once would freeze the NPC on
		// its last frame the moment it ended. Only 22 of the 108 name one; without it VtMB hands the
		// NPC back to AI, which idles it, so the stance idle is what the other 30-odd action beats
		// settle into rather than holding the action's final frame.
		if (FElysiumEntity* Npc = ResolveTarget())
		{
			if (!PostIdle.IsEmpty())
			{
				Npc->PlayAnimClip(PostIdle, /*bLoop=*/true);
			}
			else if (!Play.IsEmpty())
			{
				Npc->ResetAnimToIdle();
			}
		}

		static const FName OnEndSequence(TEXT("OnEndSequence"));
		FireOutput(OnEndSequence, Activator);

		// m_iszNextScript — 31 of 108 chain (`sSabbat3_0 -> sSabbat3_1`, the whole hooker loop).
		// Queued as a real BeginSequence delivery so it is indistinguishable from a mapped wire and
		// shows up in the event-queue window.
		if (!NextScript.IsEmpty() && World != nullptr)
		{
			static const FName BeginSequenceInput(TEXT("BeginSequence"));
			World->EnqueueInput(NextScript, BeginSequenceInput, FElysiumVariant::Void(), 0.0,
				Activator, Handle);
		}
	}

	virtual void Think() override
	{
		switch (Phase)
		{
		case EPhase::Travel: ThinkTravel(); break;
		case EPhase::Action: EndSequence(); break;
		default: break;
		}
	}

	// A beat frozen mid-flight (11.9). The travel phase cannot be resumed from the payload — the
	// restored NPC stands wherever the save put it and its motor holds no request — so the beat
	// re-issues its own move on the first think after the load, from wherever the NPC now is. An
	// action phase rides on the restored NextThink and needs nothing but its activator back.
	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		uint8 SavedPhase = static_cast<uint8>(Phase);
		Ar << SavedPhase;
		Ar << Activator;
		if (Ar.IsLoading())
		{
			Phase = static_cast<EPhase>(SavedPhase);
			bTravelled = Phase == EPhase::Travel;
			if (Phase == EPhase::Travel)
			{
				NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
				bResumeTravel = true;
			}
		}
	}

	// The waiting pose. VtMB's script grabs its NPC at level start and holds it in the pre-idle
	// until the beat is triggered — which is why plus_jenny is already sobbing and the prophet
	// already praying when the player first walks up. Done in PostSpawn because the NPC's own
	// Spawn() (where its body is built) must already have run.
	virtual void PostSpawn() override
	{
		const FString& Wait = PreIdle.IsEmpty() ? PreIdleAlt : PreIdle;
		if (Wait.IsEmpty())
		{
			return;
		}
		if (FElysiumEntity* Npc = ResolveTarget())
		{
			// Overrides the disposition stance 8.5 picked at spawn: this pose is authored for this
			// NPC at this spot, and the stance idle is only the default when nothing else says.
			Npc->PlayAnimClip(Wait, /*bLoop=*/true);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Target NPC"), TargetEntity.IsEmpty() ? TEXT("(none)") : TargetEntity);
		Out.Emplace(TEXT("Resolved"), ResolveTarget() ? TEXT("yes") : TEXT("no (player / unspawned)"));
		Out.Emplace(TEXT("Running"), bRunning ? TEXT("YES") : TEXT("no"));
		if (bRunning && World != nullptr && NextThink < ELYSIUM_NEVER_THINK)
		{
			Out.Emplace(TEXT("Ends in"), FString::Printf(TEXT("%.2f s"),
				FMath::Max(0.0, NextThink - World->NowSeconds())));
		}
		Out.Emplace(TEXT("Move to"), FString::Printf(TEXT("%d%s"), MoveTo,
			(SpawnFlags & SF_SCRIPT_NOSCRIPTMOVEMENT) != 0 ? TEXT(" (NOSCRIPTMOVEMENT)") : TEXT("")));
		for (const TPair<const TCHAR*, const FString*> F : {
				TPair<const TCHAR*, const FString*>(TEXT("Pre-idle"),    &PreIdle),
				TPair<const TCHAR*, const FString*>(TEXT("Play"),        &Play),
				TPair<const TCHAR*, const FString*>(TEXT("Post-idle"),   &PostIdle),
				TPair<const TCHAR*, const FString*>(TEXT("Custom move"), &CustomMove),
				TPair<const TCHAR*, const FString*>(TEXT("Next script"), &NextScript) })
		{
			if (!F.Value->IsEmpty())
			{
				Out.Emplace(F.Key, *F.Value);
			}
		}
	}
};

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeScriptedSequence() { return MakeUnique<FElysiumScriptedSequence>(); }

static void BuildScriptedSequenceClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("BeginSequence"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumScriptedSequence&>(E).InputBeginSequence(Args); });
	D.Input(TEXT("CancelSequence"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumScriptedSequence&>(E).InputCancelSequence(Args); });

	AddSeqField(D, TEXT("m_iszEntity"),     &FElysiumScriptedSequence::TargetEntity);
	AddSeqField(D, TEXT("m_iszIdle"),       &FElysiumScriptedSequence::PreIdle);
	AddSeqField(D, TEXT("m_iszPreIdle"),    &FElysiumScriptedSequence::PreIdleAlt);
	AddSeqField(D, TEXT("m_iszPlay"),       &FElysiumScriptedSequence::Play);
	AddSeqField(D, TEXT("m_iszPostIdle"),   &FElysiumScriptedSequence::PostIdle);
	AddSeqField(D, TEXT("m_iszCustomMove"), &FElysiumScriptedSequence::CustomMove);
	AddSeqField(D, TEXT("m_iszNextScript"), &FElysiumScriptedSequence::NextScript);
	AddSeqField(D, TEXT("m_fMoveTo"),       &FElysiumScriptedSequence::MoveTo);
	AddSeqField(D, TEXT("m_flRadius"),      &FElysiumScriptedSequence::Radius);
	AddSeqField(D, TEXT("m_flRepeat"),      &FElysiumScriptedSequence::Repeat);
}

// `aiscripted_sequence` is the same CCineNPC with a second vftable slot patched in (`101a8fe0`); the
// difference is how hard it overrides the NPC's AI, which this runtime has none of. One leaf serves both.
struct FElysiumScriptedSequenceRegistrar
{
	FElysiumScriptedSequenceRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		for (const TCHAR* Name : { TEXT("scripted_sequence"), TEXT("aiscripted_sequence") })
		{
			BuildScriptedSequenceClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeScriptedSequence));
		}
	}
};

static FElysiumScriptedSequenceRegistrar GElysiumScriptedSequenceRegistrar;
