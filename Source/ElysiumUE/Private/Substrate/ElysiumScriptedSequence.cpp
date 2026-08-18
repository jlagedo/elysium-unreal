// 8.5 — `scripted_sequence` / `aiscripted_sequence`: VtMB's cutscene beat.
//
// The class is **`CCineNPC`** in `vampire.dll` (RE: the `aiscripted_sequence` factory at
// `101a8fe0` builds vftable `10477d1c`, whose datamap `10593628` names the class) — an HL1
// `CCineMonster` derivative, not HL2's `CAI_ScriptedSequence`. That lineage fixes spawnflag bits
// 1..128 as WAITTILLSEEN / EXITAGITATED / REPEATABLE / LEAVECORPSE / START_ON_SPAWN / NOINTERRUPT /
// OVERRIDESTATE / NOSCRIPTMOVEMENT. The VtMB additions are decoded in `docs/vtmb/entity_io.md`:
// 256 holds the post-idle (the beat never completes, so `OnEndSequence` never fires), 512 marks a
// priority script that a second sequence cannot take the NPC away from, 1024 and 2048 are debug
// aids no map sets, 4096 sets a troika flag on the NPC for the beat's duration, and 8192 is
// authored by four sequences but tested nowhere. Only 128 is read here.
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
// Across the exported maps the travelling values are 54 walk, 56 run, 9 custom and 4 turn-to-face,
// beside 9 instantaneous placements — the walk-out of sp_theatre's courtroom is five of the walks,
// carrying Isaac, Therese, Nines, Skelter and VV ~19 metres with no `m_iszPlay` to time it.
//
// Travel needs a body with a motor. The embodied `!playercontroller` stand-in uses the same motor
// seam as an NPC; a bodiless record, `elysium.NpcBodies 0`, a menu backdrop, an unbuilt navigation
// graph, a mark with no path, or any headless test is **placed on the marker** instead: the authored
// end state without the transit, and the beat runs on unchanged. That is also what
// `elysium.SeqLocomotion 0` selects.
//
// What is reproduced: BeginSequence/CancelSequence (68 + 16 I/O wires, plus 68 + 8 receiver-qualified
// script calls through the same datamap lookup), the travel phase and its gait, the turn onto the
// mark, `m_iszPlay` timing `OnEndSequence`, `m_iszPostIdle` held after it, `m_iszIdle` as the pose
// the NPC waits in from map load, and `m_iszNextScript` chaining.
//
// The visible locomotion clips stay in place: scripted Walk resolves the selected ACT_WALK cell's
// decoded ground speed and gives it to the existing motor, so actor translation is applied once.
// Old sidecars and the other gaits retain their established fallback speeds. `OnScriptEvent01..08`
// (5 wires) still need decoded animation events and do not fire.
//
// **Not RE-established:** whether VtMB fires `OnBeginSequence` at the input or on arrival at the
// mark. It fires at the input here, which is the order every wire in the exported maps was authored
// against — `OnEndSequence` is the one that moves.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSeq, Log, All);

namespace
{
	// HL1 CCineMonster: the mapper's "don't move the NPC to the mark" override. One entity in the
	// exported set carries it (`spawnflags 132`).
	constexpr int32 SF_SCRIPT_NOSCRIPTMOVEMENT = 128;

	// HL1 CCineMonster REPEATABLE. `FUN_101a8640` reads it twice: with the bit CLEAR the finished
	// beat schedules its own removal, and a `m_iszNextScript` that resolves back to this very entity
	// is NOT re-fired -- the chain condition is `(next != this) || (spawnflags & 4)`. So a beat only
	// holds itself forever by being repeatable, which is what the authored "stand here doing this"
	// idiom relies on (48 of 182 exported sequences set it, and every self-naming one is among them).
	//
	// Only the chain gate is reproduced here. The self-removal half would retire 134 sequences after
	// their first run and is a separate change with its own blast radius.
	constexpr int32 SF_SCRIPT_REPEATABLE = 4;

	// VtMB's own additions, decoded in `docs/vtmb/entity_io.md`.
	// 256 — with a post-idle and no live next-cine, the sequence-done path replays the post-idle and
	// returns before cleanup, so the beat never completes (21 sequences).
	constexpr int32 SF_SCRIPT_HOLD_POSTIDLE = 256;
	// 512 — a priority script: a second sequence cannot take this NPC away (31 sequences).
	constexpr int32 SF_SCRIPT_PRIORITY = 512;
	// 4096 — troika flag 0x40 for the beat's duration, which makes
	// CBaseAnimating::IsIgnoreCollisionEntity answer true for every NPC and the player (6 sequences,
	// five of them sp_theatre's courtroom walk-out).
	constexpr int32 SF_SCRIPT_IGNORE_CHARACTER_COLLISION = 4096;

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
	FString CustomMove;     // m_iszCustomMove — the travel cycle for m_fMoveTo 3, played and speed-resolved
	FString NextScript;     // m_iszNextScript — the beat to begin when this one ends
	int32   MoveTo = 0;     // m_fMoveTo — 0 No / 1 Walk / 2 Run / 3 Custom / 4 Instant / 5 Turn to face
	float   Radius = 0.f;   // m_flRadius — parsed and inert: no site in CCineNPC reads it
	float   Repeat = 0.f;   // m_flRepeat — parsed and inert, same as m_flRadius

	// --- Live state -------------------------------------------------------------------------
	// The beat's three phases. `Travel` is the NPC walking/running/turning onto the mark, `Action`
	// is `m_iszPlay` running out; both are "between OnBeginSequence and OnEndSequence".
	enum class EPhase : uint8 { Idle, Travel, Action };
	EPhase Phase = EPhase::Idle;
	FElysiumEntityHandle Activator;        // whoever fired BeginSequence, carried to OnEndSequence
	bool bTravelled = false;               // this beat moved the NPC under its own power
	bool bResumeTravel = false;            // a load landed mid-travel; re-issue the move once
	// The NPC this beat has claimed (VtMB's m_pCine on the NPC side). Held from a successful
	// BeginSequence until the beat ends or is cancelled, and re-stamped after a load.
	FElysiumEntityHandle OwnedNpc;
	// Spawnflag 256 parked this beat in its post-idle instead of completing it.
	bool bPostIdleHeld = false;
	virtual const TCHAR* SaveBlockReason() const override
	{
		return (Phase != EPhase::Idle || bPostIdleHeld)
			? TEXT("a scripted sequence is active") : nullptr;
	}

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
		if (Npc == nullptr || !MovesTheNpc() || MoveTo == MOVETO_INSTANT || !TravelGait(Gait))
		{
			return false;
		}
		return Npc->BeginScriptMove(Origin, Angles, Gait, CustomMove);
	}

	// Put the NPC on the mark at once. VtMB's `m_fMoveTo 4`, and this runtime's stand-in wherever
	// travel is unavailable. 5 means "turn to face" and takes the angles only.
	void PlaceOnMark(FElysiumEntity* Npc) const
	{
		if (Npc == nullptr || !MovesTheNpc())
		{
			return;
		}
		if (MoveTo != MOVETO_TURN_TO_FACE)
		{
			Npc->SetRuntimeOrigin(Origin);
		}
		Npc->SetRuntimeAngles(Angles);
	}

	// Take the NPC for this beat: stamp the ownership VtMB keeps as m_pCine, take the body arbiter
	// for the whole beat, and apply the body state the flags ask for. `bScriptOwnerLocked` folds the
	// two refusal reasons into one bit the challenger can read off the base class without knowing
	// what a sequence is.
	//
	// The two locks are one claim: the queue lock says which beat owns the NPC, and the arbiter claim
	// stops the NPC's own idle selection from playing over `m_iszPlay`. The claim spans travel,
	// action and a held post-idle — the movement motor's own claim nests inside it — so no arrival
	// hands the body back while the beat is still animating it.
	void ClaimNpc(FElysiumEntity* Npc)
	{
		if (Npc == nullptr)
		{
			return;
		}
		OwnedNpc = Npc->Handle;
		Npc->ScriptOwner = Handle;
		Npc->bScriptOwnerLocked = (SpawnFlags & SF_SCRIPT_PRIORITY) != 0 || !NextScript.IsEmpty();
		// A refused claim is handled here rather than propagated, and reported by the leaf that owns
		// the arbiter — only it knows which owner refused and whether admission had run. The beat
		// runs either way: `OnEndSequence` unlocks doors and starts conversations, and the queue lock
		// stamped above is what keeps the NPC's own idle selection off the body until the claim lands.
		Npc->ClaimScriptBody(TEXT("scripted sequence beat"));
		if ((SpawnFlags & SF_SCRIPT_IGNORE_CHARACTER_COLLISION) != 0)
		{
			Npc->SetIgnoreCharacterCollision(true);
		}
	}

	// Give it back. Every exit runs through here — completion, cancellation, and the refusal paths —
	// so a beat can never leave an NPC permanently non-colliding, permanently claimed, or holding a
	// body arbiter nobody will release.
	void ReleaseNpc()
	{
		if (!OwnedNpc.IsSet())
		{
			return;
		}
		if (FElysiumEntity* Npc = World ? World->Resolve(OwnedNpc) : nullptr)
		{
			if ((SpawnFlags & SF_SCRIPT_IGNORE_CHARACTER_COLLISION) != 0)
			{
				Npc->SetIgnoreCharacterCollision(false);
			}
			if (Npc->ScriptOwner == Handle)
			{
				// Both locks leave together. An NPC a later beat has already taken keeps that beat's
				// claim: releasing here would strand it holding a body the arbiter says is free.
				Npc->ReleaseScriptBody(TEXT("scripted sequence beat ended"));
				Npc->ScriptOwner = FElysiumEntityHandle::Invalid();
				Npc->bScriptOwnerLocked = false;
			}
		}
		OwnedNpc = FElysiumEntityHandle::Invalid();
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

		FElysiumEntity* Npc = ResolveTarget();

		// Priority scripts cannot be kicked out of the queue (FUN_101a8ac0). VtMB tests the owner
		// that already holds the NPC, so nothing here fires — not even OnBeginSequence — when the
		// claim is refused.
		if (Npc != nullptr && Npc->bScriptOwnerLocked && Npc->ScriptOwner.IsSet()
			&& !(Npc->ScriptOwner == Handle))
		{
			const FElysiumEntity* Owner = World ? World->Resolve(Npc->ScriptOwner) : nullptr;
			if (Owner == nullptr)
			{
				// The owner was killed mid-beat and took its release with it. A claim nobody can
				// answer for is not a refusal — clear it rather than locking the NPC forever.
				Npc->ScriptOwner = FElysiumEntityHandle::Invalid();
				Npc->bScriptOwnerLocked = false;
			}
			else
			{
				const bool bPriority = (Owner->SpawnFlags & SF_SCRIPT_PRIORITY) != 0;
				UE_LOG(LogElysiumSeq, Log, TEXT("%s: %s is %s and cannot be kicked out of the queue"),
					*DebugString(), *Owner->DebugString(),
					bPriority ? TEXT("a priority script") : TEXT("specified as the 'Next Script'"));
				return;
			}
		}

		Activator = Args.Activator;
		bPostIdleHeld = false;
		ClaimNpc(Npc);

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
		if (bResumeTravel)
		{
			// The first think after a load: the NPC holds no request, so the beat re-issues its
			// move from wherever the payload left the body standing.
			bResumeTravel = false;
			bTravelled = StartTravel(Npc);
			if (bTravelled)
			{
				NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + TRAVEL_TICK_SECONDS);
				return;
			}
			PlaceOnMark(Npc);
			StartAction(Npc);
			return;
		}
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
	void CancelSequence()
	{
		if (Phase == EPhase::Idle && !bPostIdleHeld)
		{
			return;
		}
		// A held post-idle is still the beat owning its NPC, so cancelling one is the way out of it.
		bPostIdleHeld = false;
		ReleaseNpc();
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

	void InputCancelSequence(const FElysiumInputArgs&)
	{
		CancelSequence();
	}

	virtual bool CancelScriptedSequenceForDialogue(const FElysiumEntityHandle& NpcHandle) override
	{
		if (!(OwnedNpc == NpcHandle))
		{
			return false;
		}
		CancelSequence();
		return !OwnedNpc.IsSet();
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
		FElysiumEntity* Npc = ResolveTarget();
		if (Npc != nullptr)
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

		// Spawnflag 256 (FUN_101a8640): with a post-idle and no live next-cine, VtMB logs
		// "Post Idle %s finished", re-enters the post-idle and returns *before* the cleanup — so
		// the beat never completes, OnEndSequence never fires and the chain never runs. sp_theatre's
		// Ash and Damsel hold their conversation idles for the whole walk-out shot this way.
		//
		// The engine's condition is a live `m_hNextCine` handle; the nearest thing here is an
		// authored chain, so an empty `m_iszNextScript` stands in for it.
		if ((SpawnFlags & SF_SCRIPT_HOLD_POSTIDLE) != 0 && !PostIdle.IsEmpty() && NextScript.IsEmpty())
		{
			bPostIdleHeld = true;
			UE_LOG(LogElysiumSeq, Verbose, TEXT("%s: holding post-idle '%s' (spawnflag 256)"),
				*DebugString(), *PostIdle);
			return;   // deliberately keeps the claim: the beat has not finished with its NPC
		}

		ReleaseNpc();

		static const FName OnEndSequence(TEXT("OnEndSequence"));
		FireOutput(OnEndSequence, Activator);

		// m_iszNextScript — 31 of 108 chain (`sSabbat3_0 -> sSabbat3_1`, the whole hooker loop).
		// Queued as a real BeginSequence delivery so it is indistinguishable from a mapped wire and
		// shows up in the event-queue window.
		if (!NextScript.IsEmpty() && World != nullptr)
		{
			// `(next != this) || REPEATABLE` — a beat that names ITSELF and is not repeatable does
			// not chain. In VtMB it is on its way out at this point (the same flag scheduled its
			// removal), so re-entering it would be reviving a beat the engine has already retired.
			// Every self-naming sequence in the exported set is repeatable, so this refuses nothing
			// that ships; it is the shape of the rule, kept honest rather than assumed.
			const bool bSelfChain = NextScript.Equals(TargetName, ESearchCase::IgnoreCase);
			if (bSelfChain && (SpawnFlags & SF_SCRIPT_REPEATABLE) == 0)
			{
				UE_LOG(LogElysiumSeq, Log,
					TEXT("%s names itself as m_iszNextScript but is not REPEATABLE (spawnflags %d); "
					     "the chain stops here"), *DebugString(), SpawnFlags);
			}
			else
			{
				static const FName BeginSequenceInput(TEXT("BeginSequence"));
				World->EnqueueInput(NextScript, BeginSequenceInput, FElysiumVariant::Void(), 0.0,
					Activator, Handle);
			}
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
		// The activator rides as a bare index and is re-stamped here rather than through the
		// archive's handle operator: leaf state is an opaque blob to ApplySnapshot, so this class
		// is the only code that can put the live epoch back on it.
		int32 ActivatorIndex = Activator.IsSet() ? Activator.Index : INDEX_NONE;
		Ar << ActivatorIndex;
		// The claim rides as a bare index for the same reason the activator does, and is the
		// authority on load: re-stamping the NPC from here is what keeps the two sides agreeing
		// without the base class carrying scripted-beat state through the snapshot.
		int32 OwnedIndex = OwnedNpc.IsSet() ? OwnedNpc.Index : INDEX_NONE;
		Ar << OwnedIndex;
		Ar << bPostIdleHeld;
		if (Ar.IsLoading())
		{
			Phase = static_cast<EPhase>(SavedPhase);
			Activator = (ActivatorIndex == INDEX_NONE || World == nullptr)
				? FElysiumEntityHandle::Invalid()
				: FElysiumEntityHandle(ActivatorIndex, World->GetEpoch());
			OwnedNpc = (OwnedIndex == INDEX_NONE || World == nullptr)
				? FElysiumEntityHandle::Invalid()
				: FElysiumEntityHandle(OwnedIndex, World->GetEpoch());
			bTravelled = Phase == EPhase::Travel;
			bResumeTravel = Phase == EPhase::Travel;
			if (Phase == EPhase::Travel)
			{
				NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
			}
			// A restored body is simulating and colliding again; put back what the beat had asked
			// for, exactly as the choreo scene re-freezes its restored cast.
			if (FElysiumEntity* Npc = OwnedNpc.IsSet() && World ? World->Resolve(OwnedNpc) : nullptr)
			{
				ClaimNpc(Npc);
			}
		}
	}

	// The waiting pose. VtMB's script grabs its NPC at level start and holds it in the pre-idle
	// until the beat is triggered — which is why plus_jenny is already sobbing and the prophet
	// already praying when the player first walks up. Done in Activate because the NPC's own body
	// graph must already exist and the player must already occupy the final frozen placement.
	virtual void Activate() override
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

	virtual void PreloadForActivation() override
	{
		FElysiumEntity* Npc = ResolveTarget();
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		FString ProxyStem;
		if (!Npc && Embodiment
			&& TargetEntity.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
		{
			if (const FElysiumPlayer* Player = World->FindPlayer())
			{
				ProxyStem = FPaths::GetBaseFilename(Player->Model).ToLower();
			}
		}

		auto Preload = [Npc, Embodiment, &ProxyStem](const FString& Clip)
		{
			if (Clip.IsEmpty())
			{
				return;
			}
			if (Npc)
			{
				Npc->PreloadAnimClip(Clip);
			}
			else if (Embodiment && !ProxyStem.IsEmpty())
			{
				Embodiment->PreloadNpcClipForModel(
					ProxyStem, /*bPlayerMaterial=*/false, Clip);
			}
		};

		Preload(PreIdle.IsEmpty() ? PreIdleAlt : PreIdle);
		Preload(Play);
		Preload(PostIdle);
		Preload(CustomMove);
		if (MoveTo == MOVETO_WALK)
		{
			Preload(TEXT("walk"));
		}
		else if (MoveTo == MOVETO_RUN)
		{
			Preload(TEXT("run"));
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Target NPC"), TargetEntity.IsEmpty() ? TEXT("(none)") : TargetEntity);
		Out.Emplace(TEXT("Resolved"), ResolveTarget() ? TEXT("yes") : TEXT("no (player / unspawned)"));
		Out.Emplace(TEXT("Running"), Phase == EPhase::Travel ? TEXT("YES (travelling to the mark)")
			: Phase == EPhase::Action ? TEXT("YES (action)") : TEXT("no"));
		if (Phase == EPhase::Action && World != nullptr && NextThink < ELYSIUM_NEVER_THINK)
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
