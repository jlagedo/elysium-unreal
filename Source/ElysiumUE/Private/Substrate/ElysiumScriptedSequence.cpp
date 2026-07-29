// 8.5 — `scripted_sequence` / `aiscripted_sequence`: VtMB's cutscene beat.
//
// The class is **`CCineNPC`** in `vampire.dll` (RE: the `aiscripted_sequence` factory at
// `101a8fe0` builds vftable `10477d1c`, whose datamap `10593628` names the class) — an HL1
// `CCineMonster` derivative, not HL2's `CAI_ScriptedSequence`. That lineage fixes spawnflag bits
// 1..128 as WAITTILLSEEN / EXITAGITATED / REPEATABLE / LEAVECORPSE / START_ON_SPAWN / NOINTERRUPT /
// OVERRIDESTATE / NOSCRIPTMOVEMENT. Bits 256, 512, 4096 and 8192 are VtMB additions and their
// meanings are **not established** — nothing here reads them.
//
// A beat is: move the named NPC to this marker, play its action animation, then hold a post-idle —
// and, on either side of it, fire `OnBeginSequence` / `OnEndSequence`. The **outputs are the
// load-bearing half**: across the exported maps 88 wires leave these entities, 48 of them
// `OnEndSequence`, and they unlock Chunk's gallery door, restore the camera, and start Jack's next
// conversation. A beat that never ends stalls the map's script flow.
//
// What is reproduced: BeginSequence/CancelSequence (68 + 16 I/O wires, plus 68 + 8 receiver-qualified
// script calls through the same datamap lookup), `m_iszPlay` timing `OnEndSequence`, `m_iszPostIdle`
// held after it, `m_iszIdle` as the pose the NPC waits in from map load, and `m_iszNextScript`
// chaining.
//
// What is NOT: locomotion. 66 of the 108 sequences carry `m_fMoveTo != 0`, and this runtime has no
// navmesh, no walk cycle driver, and no decoded root motion (`animation_and_movers.md` A.3), so the
// NPC is **placed at the marker** instead of walking to it — the authored end state, without the
// transit. `m_iszCustomMove` is read and held for that work but has
// nothing to drive. `OnScriptEvent01..08` (5 wires) need decoded animation events and do not fire.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "HAL/IConsoleManager.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSeq, Log, All);

// A/B for the placement half only. 0 leaves every NPC where it stands and runs the beat as
// animation + outputs, which is what the class did before this task; the outputs fire either way.
static TAutoConsoleVariable<int32> CVarSeqTeleport(
	TEXT("elysium.SeqTeleport"),
	1,
	TEXT("scripted_sequence places its NPC at the marker on BeginSequence (1, default) or leaves it where it stands (0)."),
	ECVF_Default);

namespace
{
	// HL1 CCineMonster: the mapper's "don't move the NPC to the mark" override. One entity in the
	// exported set carries it (`spawnflags 132`).
	constexpr int32 SF_SCRIPT_NOSCRIPTMOVEMENT = 128;

	// m_fMoveTo — how the NPC is meant to reach the mark. Only the two that mean "do not travel"
	// are behavioural here; 1/2/3/4 all collapse to the same placement.
	constexpr int32 MOVETO_NONE = 0;          // "No" — already in place, do not touch it
	constexpr int32 MOVETO_TURN_TO_FACE = 5;  // "No - Turn to Face" — take the angles, not the origin

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
	bool bRunning = false;                 // between OnBeginSequence and OnEndSequence
	FElysiumEntityHandle Activator;        // whoever fired BeginSequence, carried to OnEndSequence

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

	// Place the NPC on the mark. This is the locomotion stand-in: VtMB walks the NPC here over
	// several seconds, we put it here at once. m_fMoveTo 0 means "already in place" and is left
	// completely alone; 5 means "turn to face" and takes the angles only.
	void PlaceOnMark(FElysiumEntity* Npc) const
	{
		if (Npc == nullptr || MoveTo == MOVETO_NONE || CVarSeqTeleport.GetValueOnGameThread() == 0)
		{
			return;
		}
		if ((SpawnFlags & SF_SCRIPT_NOSCRIPTMOVEMENT) != 0)
		{
			return;   // the mapper said don't move it
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
		bRunning = true;

		FElysiumEntity* Npc = ResolveTarget();
		PlaceOnMark(Npc);

		static const FName OnBeginSequence(TEXT("OnBeginSequence"));
		FireOutput(OnBeginSequence, Activator);

		// The action animation gives the beat its length. No action (59 of 108 are movement-only)
		// means a zero-length beat, so it ends in the same pass and the flow moves straight on.
		float Seconds = 0.f;
		if (Npc != nullptr && !Play.IsEmpty())
		{
			Npc->PlayAnimClip(Play, /*bLoop=*/false, &Seconds);
		}
		UE_LOG(LogElysiumSeq, Verbose, TEXT("%s BeginSequence -> %s play='%s' (%.2fs) move=%d"),
			*DebugString(), Npc ? *Npc->DebugString() : TEXT("(no body)"), *Play, Seconds, MoveTo);

		if (Seconds > 0.f)
		{
			NextThink = (World ? World->NowSeconds() : 0.0) + Seconds;
		}
		else
		{
			EndSequence();
		}
	}

	// CancelSequence — 16 I/O wires + 8 script calls. Used to interrupt a beat before starting the
	// next one (`logic_shot_5` cancels sSabbat1_4 and begins sSabbat1_5). VtMB fires OnCancelSequence
	// here; no exported map wires it, so nothing is fired. `OnEndSequence` deliberately does NOT fire
	// — a cancelled beat must not unlock the door its completion would have.
	void InputCancelSequence(const FElysiumInputArgs&)
	{
		if (!bRunning)
		{
			return;
		}
		bRunning = false;
		NextThink = ELYSIUM_NEVER_THINK;
		if (!Play.IsEmpty())
		{
			if (FElysiumEntity* Npc = ResolveTarget())
			{
				Npc->ResetAnimToIdle();   // the action is cut short; VtMB hands the NPC back to AI
			}
		}
		UE_LOG(LogElysiumSeq, Verbose, TEXT("%s CancelSequence"), *DebugString());
	}

	// The action animation ran out (or there was none): hold the post-idle, fire OnEndSequence, and
	// hand off to the next script.
	void EndSequence()
	{
		bRunning = false;
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
		if (bRunning)
		{
			EndSequence();
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
