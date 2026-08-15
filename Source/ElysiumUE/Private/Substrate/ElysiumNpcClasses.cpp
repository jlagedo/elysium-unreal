// NPC presence and the first native-Unreal locomotion slice: the `npc_*` character leaf and the
// `npc_maker` spawner, plus the retail-authored named patrol routes.
//
// The first-beat path needs Jack (npc_VVampire) and blueblood_maker (npc_maker) to stop parsing as
// inert records so `trig_off_porch.OnEndTouch` resolves — WillTalk / UseInteresting /
// StartPlayerDialogRemote at Jack, Spawn at the maker — instead of dropping as `[no input]`, and so
// the characters stand their real model on the map. An ordinary NPC now promotes that visual to an
// Unreal character/AI-controller body and can follow its retail named patrol; combat remains absent.
// An NPC stands its glTF skeletal body (out/npc/<stem>.glb via
// IElysiumEmbodiment::BuildNpcVisual, the 8.2 path) at its origin, latches the dialog-gating inputs,
// and begins/ends a dialog "session" that fires OnDialogBegin/OnDialogEnd. `npc_maker.Spawn`
// synthesizes one child NPC at runtime.
//
// The leaf here is the **dialogue half only**. Its place in VtMB's chain is CAI_BaseNPC under
// CBaseCombatCharacter under CBaseAnimating (11.4, `ElysiumPlayer.h`), and everything those two own
// — the sheet and its 25 inputs, the WillTalk latch, `default_disposition`, the skeletal body,
// playing a clip on it, following SetOrigin/SetModel, gating it on dormancy — arrives through the
// chain, shared with the player. `elysium.NpcBodies` is the one thing that stays here: it is this
// class's A/B, not the animating node's.
//
// The bounded plain-C++ mind below owns deterministic admission and arbitration only. Existing
// patrol, interesting-place, scripted-sequence and dialogue executors keep their recovered bodies;
// combat perception and the general schedule/task library remain outside this slice.

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumStanceTypes.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumStub.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumPendingInput.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestHooks.h"

#include "HAL/IConsoleManager.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpcEnt, Log, All);

// A/B toggle for the B3 NPC skeletal bodies (mirrors elysium.BrushBodies). Read in the leaf's Spawn,
// so it takes effect on the next map load: 1 stands the models, 0 leaves the NPCs bodiless records
// (their I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarNpcBodies(
	TEXT("elysium.NpcBodies"),
	1,
	TEXT("Stand NPC glTF skeletal bodies at their origins at map load (1, default) or skip them (0)."),
	ECVF_Default);

// An NPC's travel speeds and the bounds a scripted move runs under.
//
// Retail NPC locomotion speed is the cycle's own authored movement. Scripted Walk resolves that
// metadata through the selected ACT_WALK blend cell below; these constants remain the compatible
// fallback for old exports and the established tuning for patrol, Run and Custom travel.
namespace ElysiumNpcGait
{
	inline constexpr float WalkSpeed = 254.0f;   // speed_walk 100 in/s
	inline constexpr float RunSpeed  = 571.5f;   // speed_runbase 225 in/s

	// How close to the mark counts as standing on it.
	inline constexpr float ScriptAcceptanceCm = 24.0f;
	// And how close counts for a body that has stopped closing. A beat sends several NPCs to marks
	// a few centimetres apart — sp_theatre's walk-out lands five of them inside 30 cm — which two
	// 34 cm crowd agents cannot resolve while they collide. They no longer do: spawnflag 4096 turns
	// character collision off for the beat's duration, so the cluster this was written for does not
	// form and the walkers reach the tight acceptance above. What is left is a failure net for a
	// mark a body genuinely cannot stand on (world geometry, a bad graph), and a net wants to be
	// small — declaring a body "arrived" a metre and a half out would hide exactly that failure.
	inline constexpr float ScriptCrowdedCm = 90.0f;
	// The distance that counts as progress, and the two windows without it. Within the net above a
	// short one gives up quickly, because a body that close and no longer improving is stuck rather
	// than working; outside it a long one leaves room for a detour whose straight-line distance to
	// the mark is not falling yet.
	inline constexpr float ScriptProgressCm = 8.0f;
	inline constexpr double ScriptCrowdSettleSeconds = 1.5;
	inline constexpr double ScriptStallSeconds = 4.0;
	// The turn-in-place budget, and how long an unadvanced move waits before the NPC frees itself.
	inline constexpr double ScriptFaceSeconds = 2.0;
	inline constexpr double ScriptWatchdogSeconds = 1.0;

	// The absolute cap on a travel phase. It has to sit under the cleanup timers the map hangs off
	// its own camera track — sp_theatre kills the walk-out beats 20 s after the shot starts, having
	// been authored against a walk that takes about half that — so the budget is the straight-line
	// time plus half again for the route the navmesh actually takes, and a floor for a short hop.
	// A beat that hits the cap places its NPC on the mark and ends, which is always better than
	// being killed mid-travel with its OnEndSequence unfired.
	inline double TravelCapSeconds(float DistanceCm, float SpeedCmPerSecond)
	{
		return 1.5 * static_cast<double>(DistanceCm) / FMath::Max(1.0, static_cast<double>(SpeedCmPerSecond))
			+ 3.0;
	}
}

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddSignField / AddLogicField — file-unique name so all of them
	// can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddNpcField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddNpcField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	const FElysiumInterestingPlaceTable* InterestingPlaceTypes()
	{
		static FElysiumInterestingPlaceTable Table;
		static bool bAttempted = false;
		static bool bLoaded = false;
		if (!bAttempted)
		{
			bAttempted = true;
			FString Error;
			bLoaded = Table.Load(Error);
			if (!bLoaded)
			{
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("interesting-place types unavailable: %s"), *Error);
			}
			else
			{
				UE_LOG(LogElysiumNpcEnt, Log, TEXT("loaded %d interesting-place types"), Table.Num());
			}
		}
		return bLoaded ? &Table : nullptr;
	}
}

// ============================================================================================
// intersting_place — retail's shipped classname is misspelled. The entity owns enable/capacity,
// the authored timing/orientation/type fields, and arrival/leave outputs. NPCs own reservations:
// the logical state remains saveable in the substrate while Unreal only moves the body.
// ============================================================================================

class FElysiumInterestingPlace final : public FElysiumEntity
{
public:
	FString Type;
	bool bEnabled = true;
	int32 MaxNpcs = 1;
	int32 GroupId = 0;
	int32 Rating = 0;
	int32 TestFlags = 0;
	bool bMatchOrientation = false;
	float MinTime = 5.0f;
	float MaxTime = 10.0f;

	bool IsAvailable() const
	{
		return bEnabled && !IsInert() && Claimants.Num() < FMath::Max(1, MaxNpcs);
	}
	bool Claim(const FElysiumEntityHandle& Npc)
	{
		if (Claimants.Contains(Npc.Index))
		{
			return true;
		}
		if (!IsAvailable())
		{
			return false;
		}
		Claimants.Add(Npc.Index);
		return true;
	}
	void Release(const FElysiumEntityHandle& Npc) { Claimants.Remove(Npc.Index); }
	bool IsEnabledFor(const FElysiumEntityHandle& Npc) const
	{
		return bEnabled && !IsInert() && Claimants.Contains(Npc.Index);
	}
	void Arrived(const FElysiumEntityHandle& Npc)
	{
		FireOutput(FName(TEXT("OnNPCArrived")), Npc);
	}
	void Left(const FElysiumEntityHandle& Npc)
	{
		FireOutput(FName(TEXT("OnNPCLeft")), Npc);
	}
	void InputEnable(const FElysiumInputArgs&) { bEnabled = true; }
	void InputDisable(const FElysiumInputArgs&) { bEnabled = false; }
	void InputToggle(const FElysiumInputArgs&) { bEnabled = !bEnabled; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Type"), Type);
		Out.Emplace(TEXT("Occupancy"), FString::Printf(TEXT("%d/%d"), Claimants.Num(), FMath::Max(1, MaxNpcs)));
		Out.Emplace(TEXT("Group"), FString::FromInt(GroupId));
		Out.Emplace(TEXT("Rating"), FString::FromInt(Rating));
	}

private:
	TSet<int32> Claimants;
};

// ============================================================================================
// FElysiumScriptedCharacter — the one movement-only owner shared by ordinary NPCs and the
// scene-owned player duplicate. Unreal owns path following through IElysiumNpcMotor; this class
// owns the authored scripted_sequence gait, arrival/facing order and failure bounds.
// ============================================================================================

class FElysiumScriptedCharacter : public FElysiumCombatCharacter
{
public:
	virtual ~FElysiumScriptedCharacter() override
	{
		DestroyMotor();
	}

	virtual bool BeginScriptMove(const FVector& Mark, const FVector& MarkAngles,
		EElysiumScriptGait Gait, const FString& CustomClip) override
	{
		if (!Motor || IsInert())
		{
			return false;
		}
		if (!ClaimScriptMove())
		{
			return false;
		}
		bScriptMoveClaimed = true;

		Motor->Stop();
		const double Now = World ? World->NowSeconds() : 0.0;
		ScriptMark = Mark;
		ScriptMarkAngles = MarkAngles;
		ScriptProgressAt = Now;
		ScriptWatchdogAt = Now + ElysiumNpcGait::ScriptWatchdogSeconds;

		if (Gait == EElysiumScriptGait::Face)
		{
			ScriptBestDistance = 0.0f;
			ScriptDeadline = Now + ElysiumNpcGait::ScriptFaceSeconds;
			ScriptPhase = EScriptPhase::Facing;
			Motor->Face(-MarkAngles.Y);
			NextThink = static_cast<float>(ScriptWatchdogAt);
			return true;
		}

		float Speed = Gait == EElysiumScriptGait::Run
			? ElysiumNpcGait::RunSpeed : ElysiumNpcGait::WalkSpeed;
		FString ScriptWalkLabel;
		FString ScriptWalkAnim;
		if (Gait == EElysiumScriptGait::Walk)
		{
			IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
			float AuthoredSpeed = 0.f;
			if (Embodiment && Embodiment->ResolveNpcActivityClip(ModelStem(), TEXT("ACT_WALK"),
				FMath::Max(0, Handle.Index), ScriptWalkLabel, ScriptWalkAnim, AuthoredSpeed)
				&& FMath::IsFinite(AuthoredSpeed) && AuthoredSpeed > 0.f)
			{
				Speed = AuthoredSpeed;
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("%s scripted walk uses '%s' -> '%s' authored ground speed %.1fcm/s"),
					*DebugString(), *ScriptWalkLabel, *ScriptWalkAnim, Speed);
			}
		}
		else if (Gait == EElysiumScriptGait::Custom)
		{
			// `CustomClip` already names the exact clip (`m_iszCustomMove`), so it is one lookup away
			// from the same authored ground speed the Walk branch above resolves through its activity
			// -- the label-route sibling of ResolveNpcActivityClip, not a second seam.
			IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
			float AuthoredSpeed = 0.f;
			FString CustomAnim;
			if (Embodiment && Embodiment->ResolveNpcSequenceClip(ModelStem(), CustomClip, CustomAnim,
				AuthoredSpeed) && FMath::IsFinite(AuthoredSpeed) && AuthoredSpeed > 0.f)
			{
				Speed = AuthoredSpeed;
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("%s scripted custom move uses '%s' -> '%s' authored ground speed %.1fcm/s"),
					*DebugString(), *CustomClip, *CustomAnim, Speed);
			}
		}
		if (!Motor->MoveTo(Mark, ElysiumNpcGait::ScriptAcceptanceCm, Speed,
			/*bAllowPartialPath=*/true))
		{
			ScriptPhase = EScriptPhase::None;
			ReleaseScriptMove(TEXT("script path unavailable"));
			bScriptMoveClaimed = false;
			return false;
		}

		ScriptPhase = EScriptPhase::Travel;
		ScriptBestDistance = static_cast<float>(FVector::Dist2D(Origin, Mark));
		bool bTravelCycleStarted = false;
		if (Gait == EElysiumScriptGait::Custom)
		{
			bTravelCycleStarted = PlayAnimClip(CustomClip, /*bLoop=*/true);
		}
		else if (Gait == EElysiumScriptGait::Walk && !ScriptWalkLabel.IsEmpty())
		{
			bTravelCycleStarted = PlayAnimClip(ScriptWalkLabel, /*bLoop=*/true);
		}
		if (!bTravelCycleStarted)
		{
			StartScriptWalkingAnimation(Gait == EElysiumScriptGait::Run);
		}
		ScriptDeadline = Now + ElysiumNpcGait::TravelCapSeconds(ScriptBestDistance, Speed);
		NextThink = static_cast<float>(ScriptWatchdogAt);
		return true;
	}

	virtual EElysiumScriptMove AdvanceScriptMove() override
	{
		if (ScriptPhase == EScriptPhase::None)
		{
			return EElysiumScriptMove::Unsupported;
		}
		if (!Motor || IsInert())
		{
			ScriptPhase = EScriptPhase::None;
			return EElysiumScriptMove::Failed;
		}

		const double Now = World ? World->NowSeconds() : 0.0;
		ScriptWatchdogAt = Now + ElysiumNpcGait::ScriptWatchdogSeconds;
		FVector Feet = Origin;
		float Yaw = -Angles.Y;
		const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
		Origin = Feet;
		Angles.Y = -Yaw;
		if (World)
		{
			World->NotifyVisualChanged(*this);
		}

		if (ScriptPhase == EScriptPhase::Facing)
		{
			if (Status == EElysiumNpcMoveStatus::Moving && Now < ScriptDeadline)
			{
				return EElysiumScriptMove::Moving;
			}
			ScriptPhase = EScriptPhase::None;
			return EElysiumScriptMove::Arrived;
		}

		const float Remaining = static_cast<float>(FVector::Dist2D(Origin, ScriptMark));
		if (Remaining + ElysiumNpcGait::ScriptProgressCm < ScriptBestDistance)
		{
			ScriptBestDistance = Remaining;
			ScriptProgressAt = Now;
		}
		if (Status == EElysiumNpcMoveStatus::Reached)
		{
			return BeginScriptFacing(Now);
		}

		const double Stalled = Now - ScriptProgressAt;
		const bool bNearMark = Remaining <= ElysiumNpcGait::ScriptCrowdedCm;
		if (bNearMark && Stalled >= ElysiumNpcGait::ScriptCrowdSettleSeconds)
		{
			UE_LOG(LogElysiumNpcEnt, Log,
				TEXT("%s settled %.0fcm short of %s (stopped closing for %.1fs)"),
				*DebugString(), Remaining, *ScriptMark.ToString(), Stalled);
			return BeginScriptFacing(Now);
		}
		if (Status == EElysiumNpcMoveStatus::Moving
			&& Stalled < ElysiumNpcGait::ScriptStallSeconds && Now < ScriptDeadline)
		{
			return EElysiumScriptMove::Moving;
		}
		if (bNearMark)
		{
			return BeginScriptFacing(Now);
		}

		ScriptPhase = EScriptPhase::None;
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s scripted move to %s gave up %.0fcm short (status %d)"),
			*DebugString(), *ScriptMark.ToString(), Remaining, static_cast<int32>(Status));
		return EElysiumScriptMove::Failed;
	}

	virtual void EndScriptMove() override
	{
		if (ScriptPhase == EScriptPhase::None && !bScriptMoveClaimed)
		{
			return;
		}
		ScriptPhase = EScriptPhase::None;
		if (Motor)
		{
			Motor->Stop();
		}
		ReleaseScriptMove(TEXT("EndScriptMove"));
		bScriptMoveClaimed = false;
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}

	virtual void SetBodyFrozen(bool bFrozen) override
	{
		if (Motor)
		{
			Motor->SetFrozen(bFrozen);
		}
	}

	virtual void SetIgnoreCharacterCollision(bool bIgnore) override
	{
		if (Motor)
		{
			Motor->SetIgnoreCharacterCollision(bIgnore);
		}
	}

	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Motor)
		{
			Motor->Teleport(Origin, -Angles.Y);
		}
		else
		{
			FElysiumAnimating::OnRuntimeTransformChanged();
		}
	}

protected:
	enum class EScriptPhase : uint8 { None, Travel, Facing };

	void BuildMotor()
	{
		if (Motor || !Visual)
		{
			return;
		}
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Motor = Embodiment->BuildNpcMotor(Visual, Handle, Origin, -Angles.Y, ModelStem(),
				FMath::Max(0, Handle.Index));
			if (Motor)
			{
				Motor->SetEnabled(!IsInert());
			}
		}
	}

	void DestroyMotor()
	{
		if (Motor && World && World->Embodiment())
		{
			World->Embodiment()->DestroyNpcMotor(Motor);
		}
		Motor = nullptr;
	}

	virtual bool ClaimScriptMove() { return true; }
	virtual void ReleaseScriptMove(const TCHAR*) {}

	bool StartScriptWalkingAnimation(bool bRunning)
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment && Visual && Embodiment->PlayNpcActivity(Visual, ModelStem(),
			bRunning ? TEXT("ACT_RUN") : TEXT("ACT_WALK"), FMath::Max(0, Handle.Index),
			/*bLoop=*/true, nullptr))
		{
			return true;
		}
		return PlayAnimClip(bRunning ? TEXT("run") : TEXT("walk"), /*bLoop=*/true);
	}

	EElysiumScriptMove BeginScriptFacing(double Now)
	{
		ScriptPhase = EScriptPhase::Facing;
		ScriptDeadline = Now + ElysiumNpcGait::ScriptFaceSeconds;
		if (Motor)
		{
			Motor->Face(-ScriptMarkAngles.Y);
		}
		return EElysiumScriptMove::Moving;
	}

	IElysiumNpcMotor* Motor = nullptr;
	EScriptPhase ScriptPhase = EScriptPhase::None;
	FVector ScriptMark = FVector::ZeroVector;
	FVector ScriptMarkAngles = FVector::ZeroVector;
	float ScriptBestDistance = 0.0f;
	double ScriptProgressAt = 0.0;
	double ScriptDeadline = 0.0;
	double ScriptWatchdogAt = 0.0;
	bool bScriptMoveClaimed = false;
};

// ============================================================================================
// FElysiumNpc — the character leaf shared by every living `npc_*` classname. It stands a skeletal
// model at its origin, follows named patrols or interesting-place routes, and owns dialogue gates.
// ============================================================================================

class FElysiumNpc final : public FElysiumScriptedCharacter, public IElysiumScheduleRunner
{
public:
	bool  bUseInteresting = false;    // use_interesting — the NPC is a look/use target (seeded from the key)
	bool  bInDialog = false;          // a dialog session is open (OnDialogBegin fired, OnDialogEnd pending)
	int32 DialogFlags = 0;            // raw arg on ordinary/unforced; Remote ignores its variant
	int32 DecodedDialogFlags = 0;     // no bit is named until RE46 closes it
	EElysiumDialogOpenerKind DialogOpener = EElysiumDialogOpenerKind::Remote;
	FString DefaultCamera;            // definition-derived Tier-1 `default_camera`, never save state
	FString PlayerReaction;           // player_reaction — authored `D_* priority` seed
	FElysiumRelationships Relationships;
	int32 TimesTalked = 0;            // times_talked — dialogue interaction count (engine-written; script-read)

	// VtMB's disposition stance machine (`docs/vtmb/animation_and_movers.md`). The index and the
	// clock are retail's own saved pair; the two latches beside them are not saved, because retail's
	// datamap does not carry them either — a save taken mid-fidget restores as not fidgeting.
	FElysiumStanceState Stance;

	// Resolved once per (model, disposition) and re-resolved only when one of those two changes,
	// which is where retail resolves it: at model precache, with the fallback ladder baked in. The
	// row travels with the clips because they are two halves of the same table entry.
	FElysiumStanceClips StanceClips;
	FElysiumDisposition StanceTuning;
	FString StanceResolvedFor;        // "<stem>|<disposition>" the pair above was resolved for
	bool bStanceUnavailable = false;  // this model authors no stance set; do not ask again

	// The running schedule and the variant token its activity picks ride on.
	FElysiumScheduleState Schedule;
	int32 ScheduleActivityCycle = 0;

	// `m_bAllowAlertLookaround` (+0x6434), authored per NPC.
	bool bAllowAlertLookaround = false;
	// `m_iEnemySightings` (+0x60a8). Nothing increments it: an enemy is never assigned in this
	// runtime (roadmap RE48), so it holds 0 and the lookaround gate reads a flat 10%.
	int32 EnemySightings = 0;

	// The door-obstruction selector's own state. `m_hBlockedDoor` (+0x5d28) and `m_hCondHitByDoor`
	// (+0x5d2c) are the two obstruction sources this runtime can carry; `m_vSavePosition` (+0x5dd0)
	// is where the chosen one was standing when the schedules were picked.
	FElysiumEntityHandle BlockedDoor;
	double BlockedDoorExpiresAt = 0.0;
	FElysiumEntityHandle CondHitByDoor;
	bool bCondHitByDoor = false;
	FVector SavePosition = FVector::ZeroVector;

	// The stance index is what selects among a disposition's three idles, so it is this chain's
	// answer for the variant the animation layer asks for. Retail zero-initialises it, which is why
	// the first pose any body shows is `Stance_<Anim>_Idle_1` rather than an index-spread guess.
	virtual int32 IdleVariant() const override { return FMath::Clamp(Stance.Current, 0, ElysiumStance::Count - 1); }
	FString StatTemplate;             // stattemplate — the `npctemplate*.txt` stat block this NPC wears
	bool bFastFood = false;            // inherited General.FastFood — authored non-resistance to feeding
	// Inherited General.Kindred — the authored creature classification the soak table selects on.
	// An NPC with no resolved template falls back to the chain's clan-slot answer.
	bool bKindredTemplate = false;
	bool bHasKindredTemplate = false;
	// Inherited General.DamageFilter{Bashing,Lethal,Aggravated,Flame}. Authored as float
	// multipliers; absent means the template authors no filter for that family.
	float DamageFilters[4] = { 0.f, 0.f, 0.f, 0.f };
	bool  bHasDamageFilter[4] = { false, false, false, false };

	// The last damaging hit this NPC took: who, when and how much. Plain members rather than
	// registered fields — the senses/memory consumers that read them arrive with the schedule
	// kernel, and retail keeps the incoming packet as live state, not as a saved field.
	FElysiumEntityHandle LastDamageAttacker;
	double LastDamageTime = -1.0;
	int32  LastDamageAmount = 0;
	FString InterestingPlaceGroups;   // authored group allowlist; prevents cross-district wandering
	FString PatrolType;               // raw SetupPatrolType contract (kept for save/debug and later modes)
	FString PatrolPath;               // authored space-separated info_node_patrol_point names
	int32 PatrolIndex = 0;            // next point in the looping authored sequence
	enum class EAmbientPhase : uint8 { None, Moving, Into, Dwelling, Out };
	// The sheet, the WillTalk latch, `default_disposition`, the skeletal body and everything that
	// plays a clip on it now come from the chain (11.4): FElysiumCombatCharacter over
	// FElysiumAnimating, which is where VtMB puts them. This leaf is the dialogue half.
	virtual bool ResistsFeeding() const override
	{
		// `FastFood` is inherited through the resolved NPC template. It is the authored data switch
		// used by tutorial and ambient victims that must accept without the Brawl/Hacking check.
		return ElysiumFeed::ResistsByAuthoredPolicy(
			bFastFood, !FElysiumCombatCharacter::ResistsFeeding());
	}

	void ApplyResolvedTemplate(const FElysiumClanTemplate& Resolved,
		const FElysiumStatTable* Table)
	{
		bFastFood = Resolved.GeneralInt(TEXT("FastFood")) != 0;
		bHasKindredTemplate = true;
		bKindredTemplate = Resolved.GeneralInt(TEXT("Kindred")) != 0;

		// The authored damage filters, kept as the template states them. Nothing multiplies them
		// yet — the resolver only accumulates them onto the descriptor (`ElysiumDamage::Apply`
		// step 9 is an open join) — so an absent key stays absent rather than defaulting to 1.
		static const TCHAR* const FilterKeys[] =
		{
			TEXT("DamageFilterBashing"), TEXT("DamageFilterLethal"),
			TEXT("DamageFilterAggravated"), TEXT("DamageFilterFlame"),
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(FilterKeys); ++i)
		{
			const FString Authored = Resolved.GeneralStr(FilterKeys[i]);
			bHasDamageFilter[i] = !Authored.IsEmpty();
			DamageFilters[i] = bHasDamageFilter[i] ? FCString::Atof(*Authored) : 0.f;
		}
		Sheet.ApplyTemplate(Resolved, Table);
	}

	// The authored creature classification wins over the clan slot: an `npctemplate` human carries
	// `Clan None` but a Sabbat vampire template carries `Clan Brujah` AND `Kindred 1`, and only the
	// key distinguishes a ghoul or a Sabbat thug from the clan it is descended from.
	virtual bool IsKindred() const override
	{
		return bHasKindredTemplate ? bKindredTemplate : FElysiumCombatCharacter::IsKindred();
	}

	virtual bool GetTemplateDamageFilter(EElysiumDmgFamily Family, bool bFlame,
		float& OutFilter) const override
	{
		const int32 Index = bFlame ? 3 : static_cast<int32>(Family);
		if (Index < 0 || Index >= UE_ARRAY_COUNT(bHasDamageFilter) || !bHasDamageFilter[Index])
		{
			return false;
		}
		OutFilter = DamageFilters[Index];
		return true;
	}

	// Retail's NPC override saves the complete incoming damage packet before composing the base
	// transaction, and a surviving positive hit remembers its attacker. This is that record; the
	// schedule/senses consumers that read it arrive with the combat AI.
	virtual void OnDamageCommitted(const FElysiumDmg& Dmg) override
	{
		LastDamageAttacker = Dmg.Source;
		LastDamageTime = World ? World->NowSeconds() : 0.0;
		LastDamageAmount = Dmg.CommittedDamage();
	}

	void InputUseInteresting(const FElysiumInputArgs& Args)
	{
		bUseInteresting = Args.Param.ToInt() != 0;
		if (!bUseInteresting)
		{
			FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		}
		else if (!bPatrolActive)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	void InputTeleportToEntity(const FElysiumInputArgs& Args)
	{
		// CAI_BaseNPCTroika's FIELD_EHANDLE input performs its string-to-handle conversion when
		// the input is delivered: first live name match in entity-list order, including the retail
		// trailing-'*' prefix rule. It does not cache or retry a destination.
		if (!World)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s cannot resolve TeleportToEntity without an entity world"), *DebugString());
			return;
		}
		const FElysiumEntity* Destination = Args.Param.Type == EElysiumVariantType::Handle
			? World->Resolve(Args.Param.AsHandle)
			: World->FindByName(Args.Param.ToString());
		if (!Destination)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s TeleportToEntity destination '%s' resolved to no live entity"),
				*DebugString(), *Args.Param.ToString());
			return; // retail consumes the input as a no-op; there is no retry
		}

		// SetAbsOrigin, then SetAbsAngles, followed by the concrete NPC's due-think hook. The
		// authoritative writer crosses the existing embodiment seam without adding placement,
		// velocity, route, schedule, enemy, animation, or safe-location policy.
		SetRuntimeTransform(Destination->Origin, Destination->Angles);
		NextThink = static_cast<float>(World->NowSeconds());
	}

	void SeedPlayerRelationship()
	{
		const FElysiumEntityHandle Player = World ? World->PlayerHandle()
			: FElysiumEntityHandle::Invalid();
		if (!Player.IsSet() || PlayerReaction.IsEmpty() || Relationships.HasEntity(Player))
		{
			return;
		}
		TArray<FString> Tokens;
		PlayerReaction.ParseIntoArrayWS(Tokens);
		EElysiumRelationship Value = EElysiumRelationship::Neutral;
		if (Tokens.Num() != 2 || !ElysiumRelationships::Parse(Tokens[0], Value))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s invalid player_reaction '%s'"),
				*DebugString(), *PlayerReaction);
			return;
		}
		Relationships.SetEntity(Player, Value, FCString::Atoi(*Tokens[1]));
	}

	void InputSetRelationship(const FElysiumInputArgs& Args)
	{
		TArray<FString> Tokens;
		Args.Param.ToString().ParseIntoArrayWS(Tokens);
		if (Tokens.IsEmpty() || Tokens.Num() % 3 != 0)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s SetRelationship expects target D_* priority triples, got '%s'"),
				*DebugString(), *Args.Param.ToString());
			return;
		}

		for (int32 Index = 0; Index < Tokens.Num(); Index += 3)
		{
			const FString& TargetSpec = Tokens[Index];
			EElysiumRelationship Value = EElysiumRelationship::Neutral;
			if (!ElysiumRelationships::Parse(Tokens[Index + 1], Value))
			{
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s SetRelationship unknown value '%s'"),
					*DebugString(), *Tokens[Index + 1]);
				continue;
			}
			const int32 Priority = FCString::Atoi(*Tokens[Index + 2]);
			bool bMatchedEntity = false;
			if (World && TargetSpec.Equals(TEXT("player"), ESearchCase::IgnoreCase))
			{
				bMatchedEntity = Relationships.SetEntity(World->PlayerHandle(), Value, Priority);
			}
			else if (World)
			{
				const bool bWildcard = TargetSpec.Contains(TEXT("*")) || TargetSpec.Contains(TEXT("?"));
				for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
				{
					if (!Candidate || Candidate->IsDead() || Candidate->TargetName.IsEmpty())
					{
						continue;
					}
					const bool bMatches = bWildcard
						? Candidate->TargetName.MatchesWildcard(TargetSpec, ESearchCase::IgnoreCase)
						: Candidate->TargetName.Equals(TargetSpec, ESearchCase::IgnoreCase);
					if (bMatches)
					{
						Relationships.SetEntity(Candidate->Handle, Value, Priority);
						bMatchedEntity = true;
					}
				}
			}
			if (!bMatchedEntity && !TargetSpec.Contains(TEXT("*")) && !TargetSpec.Contains(TEXT("?")))
			{
				Relationships.SetClass(TargetSpec, Value, Priority);
			}
		}
		Mind.RecordExternal(FString::Printf(TEXT("relationship table %d entity / %d class; combat consumer pending"),
			Relationships.NumEntityRules(), Relationships.NumClassRules()));
	}

	void InputSetupPatrolType(const FElysiumInputArgs& Args)
	{
		PatrolType = Args.Param.ToString();
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s SetupPatrolType(%s)"),
			*DebugString(), *PatrolType);
	}

	void InputFollowPatrolPath(const FElysiumInputArgs& Args)
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		PatrolPath = Args.Param.ToString();
		PatrolIndex = 0;
		bPatrolActive = ResolvePatrolPoints();
		bMoveIssued = false;
		if (bPatrolActive && Mind.IsAdmitted() && Mind.Owner() == EElysiumBodyOwner::None
			&& !PatrolOwner.IsSet())
		{
			bPatrolActive = Mind.Acquire(EElysiumBodyOwner::Patrol, /*bSuspendCurrent=*/false,
				PatrolOwner, TEXT("FollowPatrolPath"));
		}
		if (bPatrolActive)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s FollowPatrolPath: %d/%d points (%s)"),
			*DebugString(), PatrolPoints.Num(), PatrolNames.Num(),
			bPatrolActive ? TEXT("armed") : TEXT("not armed"));
	}

	void InputClearPatrolPath(const FElysiumInputArgs&)
	{
		bPatrolActive = false;
		bMoveIssued = false;
		PatrolIndex = 0;
		PatrolPath.Reset();
		PatrolNames.Reset();
		PatrolPoints.Reset();
		if (PatrolOwner.IsSet())
		{
			if (Mind.Owner() == EElysiumBodyOwner::Patrol)
			{
				Mind.Release(PatrolOwner, TEXT("ClearPatrolPath"));
			}
			else
			{
				Mind.ForgetSuspended(EElysiumBodyOwner::Patrol, TEXT("ClearPatrolPath"));
			}
			PatrolOwner.Reset();
		}
		// A cutscene beat outranks the route inputs: clearing the route while a script owns the
		// body drops the route only, and leaves the beat's travel and its cycle running.
		if (ScriptPhase == EScriptPhase::None)
		{
			if (Motor)
			{
				Motor->Stop();
			}
			ResetAnimToIdle();
		}
		if (bUseInteresting)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	// One name out of a `FollowPatrolPath` list.
	//
	// A patrol point is NOT addressed by targetname. Every `info_node_patrol_point` the maps author
	// ships with an empty targetname and carries its name in a `Group` keyvalue — 34 of 34 on
	// `sm_hub_1`, which is what the level script's `FollowPatrolPath("s1 s2 s3 ...")` names. The
	// targetname path is kept ahead of it because it costs nothing and is what a hand-built fixture
	// uses.
	const FElysiumEntity* FindPatrolPoint(const FString& Name) const
	{
		if (const FElysiumEntity* Named = World->FindByName(Name))
		{
			return Named;
		}
		for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
		{
			if (!Ent.IsValid() || Ent->Def == nullptr
				|| !Ent->Def->Classname.Equals(TEXT("info_node_patrol_point"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (Ent->Def->Keys.FindRef(TEXT("Group")).Equals(Name, ESearchCase::IgnoreCase))
			{
				return Ent.Get();
			}
		}
		return nullptr;
	}

	bool ResolvePatrolPoints()
	{
		PatrolNames.Reset();
		PatrolPoints.Reset();
		PatrolPath.ParseIntoArrayWS(PatrolNames);
		if (!World)
		{
			return false;
		}
		for (const FString& Name : PatrolNames)
		{
			if (const FElysiumEntity* Point = FindPatrolPoint(Name))
			{
				PatrolPoints.Add(Point->Origin);
			}
			else
			{
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s patrol point '%s' does not resolve"),
					*DebugString(), *Name);
			}
		}
		PatrolIndex = PatrolPoints.IsEmpty() ? 0 : PatrolIndex % PatrolPoints.Num();
		return !PatrolPoints.IsEmpty();
	}

	bool IssuePatrolMove()
	{
		if (!Motor || !bPatrolActive || PatrolPoints.IsEmpty())
		{
			return false;
		}
		PatrolIndex = FMath::Clamp(PatrolIndex, 0, PatrolPoints.Num() - 1);
		bMoveIssued = Motor->MoveTo(PatrolPoints[PatrolIndex], /*AcceptanceRadiusCm=*/20.0f,
			ElysiumNpcGait::WalkSpeed);
		if (bMoveIssued && !bWalkingAnimation)
		{
			bWalkingAnimation = StartWalkingAnimation();
		}
		return bMoveIssued;
	}

	bool StartWalkingAnimation(bool bRunning = false)
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment && Visual && Embodiment->PlayNpcActivity(Visual, ModelStem(),
			bRunning ? TEXT("ACT_RUN") : TEXT("ACT_WALK"),
			FMath::Max(0, Handle.Index), /*bLoop=*/true, nullptr))
		{
			return true;
		}
		// A few early manifests only carry the retail label. Keep them mobile while the animation
		// catalog remains strict for every model that does expose ACT_WALK.
		return PlayAnimClip(bRunning ? TEXT("run") : TEXT("walk"), /*bLoop=*/true);
	}

	// The ordinary NPC adds body arbitration around the shared scripted motor. The scene-owned
	// player duplicate deliberately takes the default no-mind claim on the same movement rules.
	//
	// This is the one door to `EElysiumBodyOwner::Sequence`. The owning beat and its movement motor
	// both come through it, and the mind is idempotent for the owner it already holds, so the two
	// share the one token in `SequenceOwner` rather than competing for it.
	bool AcquireSequenceBody(const TCHAR* Reason)
	{
		return Mind.Acquire(EElysiumBodyOwner::Sequence, /*bSuspendCurrent=*/PatrolOwner.IsSet(),
			SequenceOwner, Reason);
	}

	// Give the token back. Only called once neither the beat nor the motor is still holding it.
	void ReleaseSequenceBody(const TCHAR* Reason)
	{
		if (!SequenceOwner.IsSet())
		{
			return;
		}
		Mind.Release(SequenceOwner, Reason);
		SequenceOwner.Reset();
		if (Mind.Owner() == EElysiumBodyOwner::Patrol)
		{
			PatrolOwner = Mind.CurrentToken();
		}
	}

	virtual bool ClaimScriptMove() override
	{
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		if (!AcquireSequenceBody(TEXT("BeginScriptMove")))
		{
			return false;
		}
		bMoveIssued = false;
		bWalkingAnimation = false;
		return true;
	}

	virtual void ReleaseScriptMove(const TCHAR* Reason) override
	{
		bMoveIssued = false;
		bWalkingAnimation = false;
		if (bScriptBodyHeld)
		{
			// The beat outlives its travel: `m_iszPlay` and the post-idle still play on this body,
			// so arrival hands the motor back without giving up the claim under them.
			return;
		}
		ReleaseSequenceBody(Reason);
	}

	virtual bool ClaimScriptBody(const TCHAR* Reason) override
	{
		bScriptBodyRequested = true;
		if (bScriptBodyHeld)
		{
			return true;
		}
		if (AmbientOwner.IsSet() || AmbientPhase != EAmbientPhase::None)
		{
			// An interesting-place visit is this NPC's own executor; the beat takes the body off it.
			FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		}
		bScriptBodyHeld = AcquireSequenceBody(Reason);
		// This leaf owns the arbiter, so it owns the one report of a claim it could not grant. The
		// two outcomes are not the same event: an unadmitted mind is an ordinary race this NPC
		// resolves itself on its next think, while an admitted mind that still refuses means another
		// owner holds a body a beat is entitled to.
		if (!bScriptBodyHeld && !Mind.IsAdmitted())
		{
			UE_LOG(LogElysiumNpcEnt, Log,
				TEXT("%s defers a scripted beat's body claim until admission has run"),
				*DebugString());
		}
		else if (!bScriptBodyHeld)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s refused a scripted beat the body: %s owns it. The beat runs on its queue "
					 "lock and the claim is retried when the arbitration allows it"),
				*DebugString(), LexToString(Mind.Owner()));
		}
		return bScriptBodyHeld;
	}

	virtual void ReleaseScriptBody(const TCHAR* Reason) override
	{
		bScriptBodyRequested = false;
		if (!bScriptBodyHeld)
		{
			return;
		}
		bScriptBodyHeld = false;
		if (bScriptMoveClaimed)
		{
			// Cancelled mid-travel: the motor is still on the same token and EndScriptMove, which
			// the beat runs next, is what gives it back.
			return;
		}
		ReleaseSequenceBody(Reason);
	}

	// An open conversation owns this body as surely as a beat does, so it refuses a feed (B6).
	virtual bool IsFeedBusy() const override
	{
		return bInDialog || FElysiumCombatCharacter::IsFeedBusy();
	}

	virtual void Think() override
	{
		if (IsInert())
		{
			return;
		}
		// The activation barrier admits the mind on its first frozen-time think. Admission is a
		// no-op for body, motor and animation; executors may run only on a later think.
		if (Mind.Admit())
		{
			// Every admitted NPC gets a next think, not just the ones with an executor: a standing
			// character's stance machine is an executor too, and without this it would never run.
			NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + 0.1);
			return;
		}
		// B6 — a pair this NPC is part of owns the body outright: it advances the transaction from
		// the feeder's think and nothing else moves either actor while it runs.
		if (TickFeed(World ? World->NowSeconds() : 0.0))
		{
			return;
		}
		if (ScriptPhase != EScriptPhase::None)
		{
			// The owning beat advances the move; this think only watches for a beat that stopped
			// doing so (killed or hidden mid-travel) and releases the body rather than freezing it.
			const double Now = World ? World->NowSeconds() : 0.0;
			if (Now < ScriptWatchdogAt)
			{
				NextThink = static_cast<float>(ScriptWatchdogAt);
				return;
			}
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s released an abandoned scripted move"),
				*DebugString());
			// Everything the beat holds leaves together, the queue lock included: a beat that stopped
			// advancing its own move will not run its teardown either, and half a claim would leave
			// this body suppressed and unowned for the rest of the map.
			ReleaseScriptBody(TEXT("abandoned scripted move"));
			EndScriptMove();
			ScriptOwner = FElysiumEntityHandle::Invalid();
			bScriptOwnerLocked = false;
			ResetAnimToIdle();
			return;
		}
		if (bInDialog)
		{
			// A per-line VCD owns the body while its sequence/gesture event is live. The ordinary
			// dialogue stance think must not replace that one-shot with a disposition idle.
			if (World && World->HasActiveDialogueBodyClip(Handle))
			{
				NextThink = static_cast<float>(World->NowSeconds() + 0.1);
				return;
			}
			// A character in conversation still runs its stance machine -- retail's Talking
			// threshold/chance pair exists precisely for this case. The selector settles it onto its
			// current idle rather than fidgeting through a line, so the reschedule is what keeps it
			// posed rather than what makes it move.
			ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
			return;
		}
		if (ScriptOwner.IsSet())
		{
			// A scripted owner — a `scripted_sequence` beat or a choreographed scene's cast — is
			// driving this body's pose. Script ownership suppresses the ordinary condition-gathering
			// path (`docs/vtmb/npc-ai-reverse-engineering.md`), so nothing below may select a
			// schedule whose idle would replace the clip the owner put on the body.
			const double Now = World ? World->NowSeconds() : 0.0;
			if (bScriptBodyRequested && !bScriptBodyHeld
				&& Mind.CanAcquire(EElysiumBodyOwner::Sequence))
			{
				// The beat asked before this NPC's admission think had run. Admission has happened
				// by now, so the claim it is entitled to lands here.
				bScriptBodyHeld = AcquireSequenceBody(TEXT("scripted beat claim after admission"));
			}
			NextThink = static_cast<float>(Now + 0.25);
			return;
		}
		if (bPatrolActive && !PatrolOwner.IsSet())
		{
			if (!Mind.Acquire(EElysiumBodyOwner::Patrol, /*bSuspendCurrent=*/false,
				PatrolOwner, TEXT("patrol executor admission")))
			{
				NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + 0.25);
				return;
			}
		}
		if (bPatrolActive && !PatrolPoints.IsEmpty())
		{
			ThinkPatrol();
		}
		else if (bUseInteresting)
		{
			ThinkAmbient();
		}
		else
		{
			// A standing NPC. Before this it fell off the end of Think() without touching NextThink,
			// which is why it was never asked again and held whatever pose it spawned in.
			ThinkStanceOrIdle(World ? World->NowSeconds() : 0.0);
		}
	}

	/**
	 * `CAI_BaseNPCTroika::SelectSchedule` case 1, in recovered priority order
	 * (`docs/vtmb/npc-ai-reverse-engineering.md` -> "The idle branch, decided"). First match wins.
	 *
	 * The steps this runtime cannot answer refuse by name rather than guessing, and the refusal
	 * records what would settle it -- a refusal that says nothing is indistinguishable from a step
	 * that silently did not apply.
	 */
	EElysiumScheduleId SelectIdleSchedule()
	{
		// 1. Choreo scene or an active discipline. `m_bInChoreoScene` maps onto the scripted body
		//    owner we already issue; the discipline flag has no domain in this runtime yet, so only
		//    the choreo half is answerable -- and it answers the same schedule either way.
		if (Mind.Owner() == EElysiumBodyOwner::Sequence)
		{
			return EElysiumScheduleId::IdleDisposition;
		}

		// 2. The follower controller at virtual `+0x97c`. Refused: `EElysiumBodyOwner::Follower` is
		//    already rejected by the mind, and the three `follower_type` radii it compares against
		//    are unrecovered.
		//
		// 3. Patrol, and 4. `use_interesting`. Both keep their existing executors rather than being
		//    re-expressed as task programs -- they own the body through the mind's own token, which
		//    is the arbitration this selection order would otherwise duplicate.
		if (bPatrolActive || bUseInteresting)
		{
			return EElysiumScheduleId::None;
		}

		// 5. Alert lookaround. `m_iEnemySightings` has no producer (roadmap RE48), so it sits at 0
		//    and the gate is a flat 10% -- which is the faithful value at that state, not a
		//    stand-in. `no_alert_state` does NOT suppress this route: the recovered base
		//    `SelectIdealState` carries no such test.
		if (bAllowAlertLookaround)
		{
			const int32 Chance = FMath::Min(30, (EnemySightings + 2) * 5);
			if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99) < Chance)
			{
				return EElysiumScheduleId::AlertLookAroundNi;
			}
		}

		// 6. Door obstruction (`CAI_BaseNPCTroika::SelectDoorObstructionSchedule`).
		if (const EElysiumScheduleId Door = SelectDoorObstructionSchedule();
			Door != EElysiumScheduleId::None)
		{
			return Door;
		}

		// 7. Return-to-initial, else the disposition stance. `m_bReturnToInitialPos` has no producer
		//    here, so this resolves to the stance -- which is also retail's own default.
		return EElysiumScheduleId::IdleDisposition;
	}

	// The standing-pose arm, reached from both the idle fall-through and the dialogue arm.
	void ThinkStanceOrIdle(double Now)
	{
		double Delay = 0.25;
		if (Schedule.IsRunning() && ElysiumSchedule::Tick(Schedule, *this, Now, Delay))
		{
			NextThink = static_cast<float>(Now + Delay);
			return;
		}

		const EElysiumScheduleId Next = SelectIdleSchedule();
		if (Next == EElysiumScheduleId::None || !ElysiumSchedule::Start(Schedule, Next, *this))
		{
			// No idle schedule applies -- an executor owns this body, or this model carries no
			// stance set at all. Either way it is re-asked on a slow cadence rather than dropped.
			NextThink = static_cast<float>(Now + 1.0);
			return;
		}
		if (!ElysiumSchedule::Tick(Schedule, *this, Now, Delay))
		{
			// The schedule ended inside its first think -- a body with no stance machine takes this
			// path, because `TASK_SPECIAL_IDLE_ACTIVITY` fails for it.
			NextThink = static_cast<float>(Now + 1.0);
			return;
		}
		NextThink = static_cast<float>(Now + Delay);
	}

	void ThinkPatrol()
	{
		if (!bPatrolActive || PatrolPoints.IsEmpty())
		{
			return;
		}
		const double Now = World ? World->NowSeconds() : 0.0;
		if (!Motor)
		{
			NextThink = static_cast<float>(Now + 0.25);
			return;
		}

		FVector Feet = Origin;
		float Yaw = -Angles.Y;
		const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
		// CharacterMovement is the physical authority while a patrol is active. Write its feet/yaw
		// straight into the entity rather than calling SetRuntimeOrigin, which would teleport it back.
		Origin = Feet;
		Angles.Y = -Yaw;
		if (World)
		{
			World->NotifyVisualChanged(*this);
		}

		if (Status == EElysiumNpcMoveStatus::Reached)
		{
			PatrolIndex = (PatrolIndex + 1) % PatrolPoints.Num();
			bMoveIssued = false;
		}
		else if (Status == EElysiumNpcMoveStatus::Failed
			|| Status == EElysiumNpcMoveStatus::Unavailable)
		{
			bMoveIssued = false; // preserve the point and retry; never silently skip authored route data
		}

		if (!bMoveIssued)
		{
			IssuePatrolMove();
		}
		NextThink = static_cast<float>(Now + (bMoveIssued ? 0.05 : 0.25));
	}

	FElysiumInterestingPlace* CurrentAmbientSpot() const
	{
		if (!World || CurrentSpotIndex == INDEX_NONE)
		{
			return nullptr;
		}
		FElysiumEntity* Entity = World->Resolve(
			FElysiumEntityHandle(CurrentSpotIndex, World->GetEpoch()));
		return Entity && Entity->Def
			&& Entity->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase)
			? static_cast<FElysiumInterestingPlace*>(Entity) : nullptr;
	}

	const FElysiumInterestingPlaceType* AmbientType(const FElysiumInterestingPlace* Spot) const
	{
		const FElysiumInterestingPlaceTable* Table = InterestingPlaceTypes();
		return Table && Spot ? Table->Find(Spot->Type) : nullptr;
	}

	FElysiumInterestingPlace* ClaimAmbientSpot()
	{
		if (!World || !Def || !InterestingPlaceTypes())
		{
			return nullptr;
		}

		// PickRandomInterestingPlace admits nodes within 10,000 Source units, but distance does not
		// rank them. It walks rating 5 -> 0, stops at the first populated tier, and chooses uniformly
		// within that tier. The NPC schedule stream makes the choice replayable across save/load.
		constexpr double FindRadiusCm = 10000.0 * ElysiumMove::U;
		constexpr double FindRadiusSqCm = FindRadiusCm * FindRadiusCm;
		TArray<FElysiumInterestingPlace*> Candidates;
		TArray<int32> CandidateRatings;
		for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
		{
			if (!Candidate || !Candidate->Def
				|| !Candidate->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			FElysiumInterestingPlace* Spot = static_cast<FElysiumInterestingPlace*>(Candidate.Get());
			const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
			if (!Spot->IsAvailable() || FailedSpotIndices.Contains(Spot->Handle.Index)
				|| !AcceptsAmbientGroup(Spot->GroupId)
				|| !TypeRow || TypeRow->Activities.IsEmpty()
				|| !TypeRow->Accepts(Def->Classname, StatTemplate))
			{
				continue;
			}
			if (FVector::DistSquared(Origin, Spot->Origin) > FindRadiusSqCm)
			{
				continue;
			}
			Candidates.Add(Spot);
			CandidateRatings.Add(Spot->Rating);
		}

		const int32 PickedIndex = ElysiumInterestingPlaces::PickHighestRatedCandidate(
			CandidateRatings, ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
		FElysiumInterestingPlace* Picked = Candidates.IsValidIndex(PickedIndex)
			? Candidates[PickedIndex] : nullptr;
		if (Picked && Picked->Claim(Handle))
		{
			if (!Mind.Acquire(EElysiumBodyOwner::Ambient, /*bSuspendCurrent=*/false,
				AmbientOwner, TEXT("interesting-place claim")))
			{
				Picked->Release(Handle);
				return nullptr;
			}
			CurrentSpotIndex = Picked->Handle.Index;
			return Picked;
		}
		return nullptr;
	}

	bool AcceptsAmbientGroup(int32 GroupId)
	{
		if (!bAmbientGroupsParsed)
		{
			bAmbientGroupsParsed = true;
			TArray<FString> Tokens;
			InterestingPlaceGroups.ParseIntoArrayWS(Tokens);
			for (const FString& Token : Tokens)
			{
				AmbientGroups.Add(FCString::Atoi(*Token));
			}
		}
		return AmbientGroups.IsEmpty() || AmbientGroups.Contains(GroupId);
	}

	bool PlayAmbientActivity(const TArray<FElysiumWeightedName>& Choices, bool bLoop,
		double Now, double& OutEnd)
	{
		FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
		const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!TypeRow || !Embodiment || !Visual)
		{
			return false;
		}
		const uint32 Seed = HashCombineFast(static_cast<uint32>(FMath::Max(0, Handle.Index)),
			static_cast<uint32>(AmbientActivityCycle++));
		const FString Activity = TypeRow->PickActivity(Choices, Seed);
		float Seconds = 0.0f;
		if (Activity.IsEmpty() || !Embodiment->PlayNpcActivity(Visual, ModelStem(), Activity,
			AmbientActivityCycle, bLoop, &Seconds))
		{
			return false;
		}
		OutEnd = Now + FMath::Max(0.25f, Seconds);
		return true;
	}

	// --- the disposition stance machine ---------------------------------------------------------
	// `docs/vtmb/animation_and_movers.md` -> "The disposition stance machine". The decision itself is
	// `ElysiumStance::Select`, a pure rule over a resolved clip table and a tuning row; everything
	// here is the resolution and the cadence around it.

	// Bring `StanceClips`/`StanceTuning` up to date for the current model and disposition. Returns
	// false for a body that authors no stance set at all, which is an ordinary answer -- the monsters
	// and one-off models idle off ACT_IDLE instead, and the caller falls back to that.
	bool EnsureStanceResolved()
	{
		const FString Key = FString::Printf(TEXT("%s|%s|%d"),
			*ModelStem(), *Disposition, DispositionLevel);
		if (StanceResolvedFor == Key)
		{
			return !bStanceUnavailable;
		}
		StanceResolvedFor = Key;
		bStanceUnavailable = true;
		StanceClips = FElysiumStanceClips();
		StanceTuning = FElysiumDisposition();

		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment == nullptr
			|| !Embodiment->ResolveDisposition(Disposition, DispositionLevel, StanceTuning)
			|| !Embodiment->ResolveStanceClips(ModelStem(), StanceTuning.AnimName, StanceClips))
		{
			return false;
		}
		bStanceUnavailable = false;
		return true;
	}

	// A disposition change re-keys the whole table: a new row means a new `AnimName`, so the clips
	// and the tuning both move. The stance *index* deliberately survives it -- retail's datamap
	// carries `m_CurrStance` across the change and never resets it.
	virtual bool SetDisposition(const FString& NewDisposition, int32 NewLevel) override
	{
		FElysiumDisposition OldRow;
		FElysiumDisposition NewRow;
		bool bChanged = false;
		if (!CommitDisposition(NewDisposition, NewLevel, bChanged, &OldRow, &NewRow))
		{
			return false;
		}
		if (!bChanged)
		{
			return true;
		}

		StanceResolvedFor.Reset();
		bool bPlayedTransition = false;
		const EElysiumBodyOwner Owner = Mind.Owner();
		if (Visual && !FElysiumCombatCharacter::IsFeedBusy()
			&& (Owner == EElysiumBodyOwner::None || Owner == EElysiumBodyOwner::Dialogue))
		{
			IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
			if (Embodiment)
			{
				const FString OldAnim = !OldRow.AnimName.IsEmpty() ? OldRow.AnimName : OldRow.Name;
				const FString NewAnim = !NewRow.AnimName.IsEmpty() ? NewRow.AnimName : NewRow.Name;
				const int32 StanceNumber = FMath::Clamp(
					Stance.Current, 0, ElysiumStance::Count - 1) + 1;
				auto TryTransition = [&](int32 Number)
				{
					if (OldAnim.IsEmpty() || NewAnim.IsEmpty())
					{
						return false;
					}
					const FString Clip = FString::Printf(TEXT("Stance_Trans_%s_%d_%s_%d"),
						*OldAnim, Number, *NewAnim, Number);
					// This cross-disposition transition is authored per model, and most bodies carry
					// none: probe the vocabulary first, so an absent clip is a quiet negative query
					// result rather than PlayNpcClip's logged miss.
					if (!Embodiment->HasNpcClip(ModelStem(), Clip))
					{
						UE_LOG(LogElysiumNpcEnt, Verbose,
							TEXT("%s disposition transition '%s' not authored; not taken"),
							*DebugString(), *Clip);
						return false;
					}
					float Seconds = 0.f;
					if (!Embodiment->PlayNpcClip(Visual, ModelStem(), Clip,
						/*bLoop=*/false, &Seconds))
					{
						return false;
					}
					UE_LOG(LogElysiumNpcEnt, Verbose,
						TEXT("%s disposition transition '%s' taken"), *DebugString(), *Clip);
					Mind.RecordExternal(FString::Printf(TEXT("disposition %s L%d -> %s L%d via %s"),
						*OldRow.Name, OldRow.Level, *NewRow.Name, NewRow.Level, *Clip));
					if (World)
					{
						NextThink = static_cast<float>(World->NowSeconds() + FMath::Max(0.05f, Seconds));
					}
					return true;
				};
				bPlayedTransition = TryTransition(StanceNumber)
					|| (StanceNumber != 1 && TryTransition(1));
			}
		}
		if (!bPlayedTransition)
		{
			ResetAnimToIdle();
			if (World)
			{
				NextThink = static_cast<float>(World->NowSeconds());
			}
		}
		return true;
	}

	// --- IElysiumScheduleRunner: the task bodies -------------------------------------------------

	// `TASK_SPECIAL_IDLE_ACTIVITY`. One stance selection played on the body; the schedule holds the
	// task open for the clip's own length, which is retail's cadence -- the idle task re-requests
	// `ACT_DISPOSITION` only once the current sequence has finished, so nothing re-enters mid-clip.
	virtual float RunSpecialIdleActivity(double Now) override
	{
		// The task writes a clip onto the body, so it runs only while this NPC's own idle owns it.
		// `SCHED_TROIKA_IDLE_DISPOSITION` is still the faithful selection for a choreo-scene NPC --
		// the guard belongs here, at the one step that would overwrite what the owner is playing.
		// The admitted set matches the disposition-stance transition's: nobody, or a conversation.
		const EElysiumBodyOwner BodyOwner = Mind.Owner();
		if (BodyOwner != EElysiumBodyOwner::None && BodyOwner != EElysiumBodyOwner::Dialogue)
		{
			// Failing the task is how a runner declines: the schedule ends through its (absent) fail
			// schedule and the caller re-asks on the slow cadence rather than spinning at zero delay.
			Mind.RecordExternal(FString::Printf(
				TEXT("TASK_SPECIAL_IDLE_ACTIVITY declined: %s owns the body"), LexToString(BodyOwner)));
			return -1.f;
		}
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment == nullptr || Visual == nullptr || !EnsureStanceResolved())
		{
			return -1.f;
		}
		// `bTalking` is the file's own distinction: a line playing on this character, not a dialogue
		// being open. We have only the session latch until the per-line driver lands, and the two
		// agree on the branch that matters -- a character in dialogue holds its stance either way.
		const int32 Before = Stance.Current;
		const FElysiumStanceChoice Choice = ElysiumStance::Select(StanceClips, StanceTuning, Stance,
			/*bTalking=*/IsDispositionTalking(), Now,
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule));
		float Seconds = 0.f;
		if (!Choice.IsSet()
			|| !Embodiment->PlayNpcClip(Visual, ModelStem(), Choice.Clip, Choice.bLoop, &Seconds))
		{
			return -1.f;
		}
		// Only a change is traced. An idle re-settling on the same stance is the common case by far,
		// and recording it would push everything else out of a 16-row window.
		if (Choice.bChangedStance)
		{
			Mind.RecordExternal(FString::Printf(TEXT("stance %d -> %d via %s"),
				Before, Stance.Current, *Choice.Clip));
		}
		return Seconds;
	}

	virtual bool IsBodyVisible() const override
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		// No embodiment is a headless run, where the question has no renderer to answer it. The
		// service's own default says visible for the same reason.
		return Embodiment == nullptr || Embodiment->IsNpcBodyVisible(Visual);
	}

	virtual float PlayActivity(const FString& Activity) override
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Embodiment == nullptr || Visual == nullptr)
		{
			return -1.f;
		}
		float Seconds = 0.f;
		if (!Embodiment->PlayNpcActivity(Visual, ModelStem(), Activity, ScheduleActivityCycle++,
			/*bLoop=*/false, &Seconds))
		{
			return -1.f;
		}
		return Seconds;
	}

	virtual float RandomSeconds(float Max) override
	{
		return Max > 0.f
			? ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(0.f, Max) : 0.f;
	}

	virtual void RecordScheduleEvent(const FString& Row) override
	{
		Mind.RecordExternal(Row);
	}

	virtual bool FaceSavePosition() override
	{
		if (Motor == nullptr)
		{
			return false;
		}
		const FVector ToSource = SavePosition - Origin;
		if (ToSource.IsNearlyZero())
		{
			return false;
		}
		Motor->Face(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToSource.Y, ToSource.X))));
		return true;
	}

	virtual bool StepAwayFromSavePosition(float DistanceCm) override
	{
		// The rule itself is `ElysiumSchedule::StepAwayFromSavePosition` — extrapolate, project
		// through the motor, re-test the projection against the retreat rule. This leaf supplies the
		// two positions and turns the outcome into the task's pass/fail, so a refusal reaches the
		// schedule's fail path already named rather than as a bare false.
		FVector Destination = FVector::ZeroVector;
		const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
			Motor, Origin, SavePosition, DistanceCm, Destination);
		if (Result != ElysiumSchedule::ERetreat::Moving)
		{
			Mind.RecordExternal(FString::Printf(TEXT("TASK_MOVE_AWAY_PATH refused: %s"),
				ElysiumSchedule::RetreatResultName(Result)));
			return false;
		}
		return true;
	}

	/**
	 * `CAI_BaseNPCTroika::SelectDoorObstructionSchedule` (`0x102b7370`), transcribed.
	 *
	 * Returns `None` when this policy declines -- which is every call today, because neither
	 * obstruction source has a producer yet: nothing sets `m_hBlockedDoor` (a door blocking this
	 * NPC's path) and nothing sets `COND_HIT_BY_DOOR`. The decision itself is complete and is what
	 * those producers will feed; the third recovered source is gated on `COND_ENEMY_UNREACHABLE`,
	 * which presupposes an enemy and so is never set (roadmap RE48).
	 */
	EElysiumScheduleId SelectDoorObstructionSchedule()
	{
		const double Now = World ? World->NowSeconds() : 0.0;

		// The source order is retail's own. A blocked-door handle outlives its usefulness, so an
		// expired one is cleared rather than reused.
		const FElysiumEntity* Source = nullptr;
		if (BlockedDoor.IsSet() && World)
		{
			if (Now < BlockedDoorExpiresAt)
			{
				Source = World->Resolve(BlockedDoor);
			}
			else
			{
				BlockedDoor = FElysiumEntityHandle::Invalid();
			}
		}
		if (Source == nullptr && bCondHitByDoor && CondHitByDoor.IsSet() && World)
		{
			Source = World->Resolve(CondHitByDoor);
		}
		if (Source == nullptr)
		{
			return EElysiumScheduleId::None;
		}

		// With a source chosen, retail asks the hint machinery for cover and takes it when the claim
		// is medium, low or corner cover. We carry no hint-node reader yet, so this falls through to
		// the distance test -- the authored `info_node_cover_med/low/corner` entities the branch
		// needs are exported but unread.
		SavePosition = Source->Origin;

		// 256 units, compared squared, in Source units -- the same 2.54 cm/inch the whole runtime
		// reads verbatim.
		constexpr double ThresholdCm = 256.0 * 2.54;
		const bool bNear = FVector::DistSquared(Origin, SavePosition) <= ThresholdCm * ThresholdCm;
		// `GetEnemy` is unconditionally null here, so both rows resolve to their `_NE` variant --
		// the faithful answer for an NPC with no enemy, not a fallback (roadmap RE48).
		return bNear ? EElysiumScheduleId::BackAwayFromDoorNe
			: EElysiumScheduleId::BackAwayFromDoorWaitNe;
	}

	void BeginAmbientUse(FElysiumInterestingPlace& Spot, double Now)
	{
		bAmbientArrived = true;
		bMoveIssued = false;
		bWalkingAnimation = false;
		if (Motor)
		{
			Motor->Stop();
			if (Spot.bMatchOrientation)
			{
				Motor->Teleport(Spot.Origin, -Spot.Angles.Y);
				Origin = Spot.Origin;
				Angles.Y = Spot.Angles.Y;
			}
		}
		Spot.Arrived(Handle);

		const uint32 StaySeed = HashCombineFast(static_cast<uint32>(FMath::Max(0, Handle.Index)),
			static_cast<uint32>(FMath::Max(0, Spot.Handle.Index)));
		const float Unit = static_cast<float>(StaySeed) / static_cast<float>(MAX_uint32);
		const float Lo = FMath::Max(0.0f, FMath::Min(Spot.MinTime, Spot.MaxTime));
		const float Hi = FMath::Max(Lo, FMath::Max(Spot.MinTime, Spot.MaxTime));
		AmbientLeaveAt = Now + FMath::Lerp(Lo, Hi, Unit);

		const FElysiumInterestingPlaceType* TypeRow = AmbientType(&Spot);
		if (TypeRow && PlayAmbientActivity(TypeRow->IntoActivities, /*bLoop=*/false,
			Now, AmbientNextActivityAt))
		{
			AmbientPhase = EAmbientPhase::Into;
		}
		else
		{
			AmbientPhase = EAmbientPhase::Dwelling;
			AmbientNextActivityAt = Now;
		}
	}

	void BeginAmbientLeave(double Now)
	{
		FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
		const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
		if (TypeRow && PlayAmbientActivity(TypeRow->OutOfActivities, /*bLoop=*/false,
			Now, AmbientNextActivityAt))
		{
			AmbientPhase = EAmbientPhase::Out;
			return;
		}
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
	}

	void FinishAmbientUse(bool bFireLeft)
	{
		// This is the single exit for an ambient claim, including UseInteresting(0), a disabled spot,
		// dialogue, dormancy and a patrol taking ownership. Cancel a request before forgetting it so
		// the native controller cannot keep walking an entity the substrate now considers idle.
		if (AmbientPhase == EAmbientPhase::Moving && bMoveIssued && Motor)
		{
			Motor->Stop();
		}
		if (FElysiumInterestingPlace* Spot = CurrentAmbientSpot())
		{
			if (bFireLeft)
			{
				Spot->Left(Handle);
			}
			Spot->Release(Handle);
		}
		CurrentSpotIndex = INDEX_NONE;
		AmbientPhase = EAmbientPhase::None;
		bAmbientArrived = false;
		bMoveIssued = false;
		bWalkingAnimation = false;
		if (AmbientOwner.IsSet())
		{
			Mind.Release(AmbientOwner, TEXT("interesting-place release"));
			AmbientOwner.Reset();
		}
		if (ScriptPhase == EScriptPhase::None)
		{
			ResetAnimToIdle();   // a script that owns the body owns its pose too
		}
	}

	void ThinkAmbient()
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		if (!Motor)
		{
			NextThink = static_cast<float>(Now + 0.5);
			return;
		}

		FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
		if (AmbientPhase == EAmbientPhase::None || !Spot)
		{
			Spot = ClaimAmbientSpot();
			if (!Spot)
			{
				if (!FailedSpotIndices.IsEmpty())
				{
					FailedSpotIndices.Reset();
				}
				NextThink = static_cast<float>(Now + 1.0);
				return;
			}
			AmbientPhase = EAmbientPhase::Moving;
			bMoveIssued = Motor->MoveTo(Spot->Origin, 24.0f, 254.0f);
			if (bMoveIssued)
			{
				bWalkingAnimation = StartWalkingAnimation();
			}
			else
			{
				FailedSpotIndices.Add(Spot->Handle.Index);
				FinishAmbientUse(/*bFireLeft=*/false);
			}
			NextThink = static_cast<float>(Now + 0.25);
			return;
		}

		if (!Spot->IsEnabledFor(Handle))
		{
			if (!bAmbientArrived)
			{
				FinishAmbientUse(/*bFireLeft=*/false);
				NextThink = static_cast<float>(Now + 0.5);
				return;
			}
			if (AmbientPhase != EAmbientPhase::Out)
			{
				BeginAmbientLeave(Now);
				NextThink = static_cast<float>(Now + 0.1);
				return;
			}
			// An already-started out activity is allowed to finish below even though Disable made
			// the place unavailable to new claimants.
		}

		if (AmbientPhase == EAmbientPhase::Moving)
		{
			FVector Feet = Origin;
			float Yaw = -Angles.Y;
			const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
			Origin = Feet;
			Angles.Y = -Yaw;
			if (World)
			{
				World->NotifyVisualChanged(*this);
			}
			if (Status == EElysiumNpcMoveStatus::Reached)
			{
				BeginAmbientUse(*Spot, Now);
			}
			else if (Status == EElysiumNpcMoveStatus::Failed
				|| Status == EElysiumNpcMoveStatus::Unavailable)
			{
				FailedSpotIndices.Add(Spot->Handle.Index);
				FinishAmbientUse(/*bFireLeft=*/false);
			}
			NextThink = static_cast<float>(Now + 0.05);
			return;
		}

		if (AmbientPhase == EAmbientPhase::Out && Now >= AmbientNextActivityAt)
		{
			FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
			NextThink = static_cast<float>(Now + 0.5);
			return;
		}
		if (AmbientPhase == EAmbientPhase::Into && Now >= AmbientNextActivityAt)
		{
			AmbientPhase = EAmbientPhase::Dwelling;
			AmbientNextActivityAt = Now;
		}
		if (AmbientPhase == EAmbientPhase::Dwelling)
		{
			if (Now >= AmbientLeaveAt)
			{
				BeginAmbientLeave(Now);
			}
			else if (Now >= AmbientNextActivityAt)
			{
				const FElysiumInterestingPlaceType* TypeRow = AmbientType(Spot);
				if (!TypeRow || !PlayAmbientActivity(TypeRow->Activities, /*bLoop=*/false,
					Now, AmbientNextActivityAt))
				{
					ResetAnimToIdle();
					AmbientNextActivityAt = FMath::Min(AmbientLeaveAt, Now + 2.0);
				}
			}
		}
		NextThink = static_cast<float>(Now + 0.1);
	}

	// StartPlayerDialogRemote opens a dialog session: fire OnDialogBegin, then run the NPC's `.dlg`
	// conversation (B4). When the `dialogname` file is missing/unloadable the session falls back to the
	// B3 seam — it waits for a manual EndDialog (ent_fire), so the beat is still driveable by hand.
	virtual FElysiumBodyOwnerToken BeginDialogueBodySession() override
	{
		if (IsInert())
		{
			return FElysiumBodyOwnerToken();
		}
		if (DialogueBodyOwner.IsSet())
		{
			return DialogueBodyOwner;
		}
		// Dialogue is the authored interruption of CCineNPC ownership. Cancel through the sequence
		// itself before asking the mind for Dialogue so travel, action, collision flags and the
		// sequence's delayed completion all leave through the one CancelSequence teardown. This is
		// what prevents an older action deadline from resetting a newer dialogue line to idle.
		if (ScriptOwner.IsSet() && World)
		{
			const FElysiumEntityHandle PreviousOwner = ScriptOwner;
			if (FElysiumEntity* Owner = World->Resolve(PreviousOwner))
			{
				if (Owner->CancelScriptedSequenceForDialogue(Handle) && ScriptOwner.IsSet())
				{
					UE_LOG(LogElysiumNpcEnt, Warning,
						TEXT("%s dialogue cancelled scripted owner %s but the body claim remained"),
						*DebugString(), *PreviousOwner.ToString());
					return FElysiumBodyOwnerToken();
				}
			}
			else
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("%s cleared stale scripted owner %s while opening dialogue"),
					*DebugString(), *PreviousOwner.ToString());
				// Nothing is left to run the beat's own teardown, so its body claim is dropped here
				// too -- otherwise the arbiter would still read Sequence and refuse the dialogue.
				ReleaseScriptBody(TEXT("stale scripted owner cleared for dialogue"));
				EndScriptMove();
				ScriptOwner = FElysiumEntityHandle::Invalid();
				bScriptOwnerLocked = false;
			}
		}
		if (!Mind.Acquire(EElysiumBodyOwner::Dialogue, /*bSuspendCurrent=*/PatrolOwner.IsSet(),
			DialogueBodyOwner, TEXT("dialogue open")))
		{
			return FElysiumBodyOwnerToken();
		}
		if (Motor && bPatrolActive)
		{
			Motor->Stop();
			bMoveIssued = false;
		}
		bInDialog = true;
		return DialogueBodyOwner;
	}

	virtual void EndDialogueBodySession(const FElysiumBodyOwnerToken& Token, bool bSilent) override
	{
		if (DialogueBodyOwner.IsSet() && Token.Owner == DialogueBodyOwner.Owner
			&& Token.Generation == DialogueBodyOwner.Generation)
		{
			Mind.Release(DialogueBodyOwner, bSilent ? TEXT("dialogue silent close")
				: TEXT("dialogue normal close"));
			DialogueBodyOwner.Reset();
			if (Mind.Owner() == EElysiumBodyOwner::Patrol)
			{
				PatrolOwner = Mind.CurrentToken();
			}
		}
		if (bSilent)
		{
			bInDialog = false;
		}
		if (!bInDialog && (bPatrolActive || bUseInteresting))
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	void BeginDialog(EElysiumDialogOpenerKind Opener, int32 RawFlags,
		const FElysiumInputArgs& Args)
	{
		if (IsInert() || bInDialog)
		{
			return;
		}
		// BeginDialogueBodySession performs the scripted-sequence cancellation and body transfer.
		// Keep that ownership transition in one place so direct World::OpenDialog uses the same path.
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		if (Motor && bPatrolActive)
		{
			Motor->Stop();
			bMoveIssued = false;
		}
		if (!BeginDialogueBodySession().IsSet())
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s refused dialogue body ownership"),
				*DebugString());
			return;
		}
		DialogOpener = Opener;
		DialogFlags = RawFlags;
		DecodedDialogFlags = 0;
		if (DialogFlags != 0)
		{
			ElysiumStub::Fired(TEXT("field"), TEXT("CAI_BaseNPC.DialogOpenerInteger"),
				DebugString(), FString::Printf(TEXT("raw=%d"), DialogFlags),
				TEXT("RE46 — recover NPC+0x5bac before decoding flags"));
		}
		static const FName OnDialogBegin(TEXT("OnDialogBegin"));
		FireOutput(OnDialogBegin, Args.Activator);
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s %s(raw=%d decoded=%d)"), *DebugString(),
			ElysiumDialogueCamera::LexToString(Opener), DialogFlags, DecodedDialogFlags);

		OpenConversation(Args.Activator, Opener);
	}

	// K1: each Tier-1 name enters through its own handler. Only after that handler has applied the
	// recovered parameter posture does it join the shared dialogue-session primitive above.
	void InputStartPlayerDialog(const FElysiumInputArgs& Args)
	{
		if (IsInert() || bInDialog)
		{
			return;
		}
		// The pinned ordinary handler stores this integer at NPC+0x5bac. Its bits are not yet named.
		BeginDialog(EElysiumDialogOpenerKind::Forced, Args.Param.ToInt(), Args);
	}

	void InputStartPlayerDialogRemote(const FElysiumInputArgs& Args)
	{
		if (IsInert() || bInDialog)
		{
			return;
		}
		// The pinned Remote handler never reads the input variant; authored `256` is intentionally
		// discarded before the common primitive sees it.
		BeginDialog(EElysiumDialogOpenerKind::Remote, 0, Args);
	}

	void InputStartPlayerDialogUnforced(const FElysiumInputArgs& Args)
	{
		if (IsInert() || bInDialog)
		{
			return;
		}
		// The extra player-refusal predicate is not yet recovered. Keep the unresolved gate visible;
		// no placement, acceptance, or flag meaning is inferred from it.
		ElysiumStub::Fired(TEXT("input"), TEXT("CAI_BaseNPC.StartPlayerDialogUnforcedGate"),
			DebugString(), ElysiumStub::DescribeInput(Args),
			TEXT("RE46 — recover the player-side unforced refusal predicate"));
		BeginDialog(EElysiumDialogOpenerKind::Unforced, Args.Param.ToInt(), Args);
	}

	// The dialog session ends: increment times_talked and fire OnDialogEnd. Reached both by the runner
	// (World::EndDialogSession routes EndDialog to `!self` when the conversation closes) and by a manual
	// ent_fire. Jack's OnDialogEnd wires DialogPostProcess(), which reads the `G` flags the dialogue's
	// field-5 actions wrote and warps the player.
	void InputEndDialog(const FElysiumInputArgs& Args)
	{
		if (!bInDialog)
		{
			return;
		}
		if (DialogueBodyOwner.IsSet())
		{
			EndDialogueBodySession(DialogueBodyOwner, /*bSilent=*/false);
		}
		bInDialog = false;
		++TimesTalked;
		static const FName OnDialogEnd(TEXT("OnDialogEnd"));
		FireOutput(OnDialogEnd, Args.Activator);
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s EndDialog (times_talked=%d)"), *DebugString(), TimesTalked);
		// Unconditional: leaving a conversation releases the stance machine's talking branch, so a
		// standing character has a decision to make on the very next think.
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}

	// Load this NPC's `dialogname` `.dlg`, open a branch conversation bound to the installed script host,
	// and hand it to the world (the visual-novel box renders it; the runner fires EndDialog on close).
	// Returns false when there is no dialogue to run, leaving bInDialog latched for the B3 manual seam.
	bool OpenConversation(const FElysiumEntityHandle& Activator, EElysiumDialogOpenerKind Opener)
	{
		if (!World || !Def)
		{
			return false;
		}
		const FString DialogName = Def->Keys.FindRef(TEXT("dialogname"));
		if (DialogName.IsEmpty())
		{
			return false;   // an NPC with no dialogue file — nothing to open
		}

		const FString Path = FElysiumContentPaths::DlgFromDialogname(DialogName);
		TSharedRef<FElysiumDlgFile> DlgFile = MakeShared<FElysiumDlgFile>();
		FString Err;
		if (!FElysiumDlgFile::LoadFile(Path, DlgFile.Get(), &Err))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s dialog load failed: %s"), *DebugString(), *Err);
			return false;
		}

		// Player gender + clan drive text selection: VtMB shows col-2 for a female PC, and the col-12
		// Malkavian variant for a Malkavian PC. Clan is the 2..8 sheet encoding (Malkavian = 4).
		const UElysiumGameStateSubsystem* GameState = World->GetGameState();
		const bool bMale = GameState ? GameState->PlayerSheet().IsMale() : true;
		const bool bMalk = GameState
			&& GameState->PlayerSheet().Clan() == FElysiumSheet::ClanFromName(TEXT("Malkavian"));
		const FElysiumEntityHandle Self = Handle;
		FElysiumEntityWorld* W = World;

		// Field-4 conditions eval, field-4(NPC)/field-5 actions exec — both through the installed host
		// (EvalCondition also execs statements), so they land in the same `G` the level script reads and
		// obey the same live/off switch and eval log as field-6. dlgexpr -> Python via the normalizer.
		auto Cond = [W, Self, Activator](const FString& Raw) -> bool
		{
			return W->EvalCondition(ElysiumDlgExpr::ConditionToPython(Raw), Self, Activator).ToBool();
		};
		auto Act = [W, Self, Activator](const FString& Raw)
		{
			W->EvalCondition(ElysiumDlgExpr::ActionToPython(Raw), Self, Activator);
		};
		const FString DialogUseScript = UseScript;
		auto StartFallback = [W, Self, Activator, DialogUseScript]() -> TOptional<int32>
		{
			if (DialogUseScript.IsEmpty())
			{
				return TOptional<int32>();   // no usescript: retail's default is line 1
			}
			const FElysiumVariant Result = W->EvalCondition(DialogUseScript, Self, Activator);
			// CallPyDialogFunc accepts only a Python int; every other result (including an error/None)
			// returns 0 and lets CDialog::Acquire apply its first-stored-line fallback.
			return Result.IsInt() ? Result.ToInt() : 0;
		};

		TSharedRef<FElysiumDlgConversation> Conv =
			MakeShared<FElysiumDlgConversation>(DlgFile, bMale, bMalk, MoveTemp(Cond), MoveTemp(Act),
				MoveTemp(StartFallback));
		Conv->Start();
		World->OpenDialog(Self, Conv, Opener, DialogFlags, DefaultCamera, DialogueBodyOwner);
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s opened dialogue '%s' (%d rows)"),
			*DebugString(), *DialogName, DlgFile->Lines.Num());
		return true;
	}

	// The sheet, from `stats.txt`'s defaults overlaid with this NPC's `stattemplate`. That overlay
	// is the whole of an NPC's health track: `npctemplate*` authors `Max_Health` as a literal, and a
	// template that omits it inherits `stats.txt`'s `Default 100` (`docs/vtmb/vdata-catalog.md`). Without it
	// every NPC had a zero ceiling and TakeDamage only logged.
	void SeedSheet()
	{
		UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
		const FElysiumStatTable* Table = GameState ? GameState->Stats() : nullptr;
		UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
		FElysiumClanTemplate Resolved;
		const bool bResolvedTemplate = !StatTemplate.IsEmpty()
			&& Rules && Rules->Clans().Resolve(StatTemplate, Resolved);
		bFastFood = false;
		bHasKindredTemplate = false;
		bKindredTemplate = false;
		for (bool& bHas : bHasDamageFilter) { bHas = false; }
		if (!Table)
		{
			return;   // no rulebook: the sheet stays zeroed and the damage path stays fail-closed
		}
		Sheet.SeedFrom(*Table);

		if (!StatTemplate.IsEmpty())
		{
			if (bResolvedTemplate)
			{
				ApplyResolvedTemplate(Resolved, Table);
				// A template names its clan's `TraitEffectGroup`; that group is where the clan's
				// gifts and banes live, for an NPC exactly as for the player.
				const FString ClanEffect = Resolved.GeneralStr(TEXT("ClanEffect"));
				if (!ClanEffect.IsEmpty())
				{
					Effects.AddUnique(ClanEffect);
				}
				RebuildEffects();
			}
			else
			{
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s stattemplate '%s' resolves to nothing"),
					*DebugString(), *StatTemplate);
			}
		}
		SyncHealthFromSheet();
	}

	virtual void Spawn() override
	{
		SeedSheet();
		// Keyfields (model/angles/use_interesting/stattemplate) are already applied. Stand the body:
		// out/npc/<stem>.glb, playing the standing idle `default_disposition` selects, spread across
		// the three VtMB authors per disposition and seeded from this entity's own index — a cop that
		// stood with its arms crossed must still be doing so after a reload. All of that is
		// FElysiumAnimating's; the cvar is this leaf's A/B.
		if (CVarNpcBodies.GetValueOnGameThread() == 0)
		{
			return;   // gated off: a bodiless record whose I/O still resolves
		}
		BuildBody();
		BuildMotor();
		// Spawn constructs presentation only. Activate arms the first deterministic admission think;
		// no autonomous decision, controller wake or activity write occurs in this phase.
	}

	virtual void Activate() override
	{
		SeedPlayerRelationship();
		Mind.ArmAdmission();
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}

	// SetModel: swap the NPC's appearance (bradbury Heather goth/normal, cemetery prostitute,
	// downtown Nines). The rebuild is FElysiumAnimating's; the A/B gate is this leaf's.
	virtual void OnRuntimeModelChanged() override
	{
		if (CVarNpcBodies.GetValueOnGameThread() == 0)
		{
			return;
		}
		// The swap destroys the motor the beat is steering, so release the hold first — the beat
		// reads Unsupported next tick and finishes on the placement fallback.
		EndScriptMove();
		DestroyMotor();
		FElysiumAnimating::OnRuntimeModelChanged();
		BuildMotor();
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumCombatCharacter::OnDormancyChanged();
		if (IsInert())
		{
			if (DialogueBodyOwner.IsSet())
			{
				if (World && World->GetOpenDialogOwner() == Handle)
				{
					World->CloseDialog(/*bSilent=*/true);
				}
				else
				{
					EndDialogueBodySession(DialogueBodyOwner, /*bSilent=*/true);
				}
			}
			EndScriptMove();
			FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
			Mind.Invalidate(bDead ? TEXT("death") : TEXT("dormancy"), bDead);
			PatrolOwner.Reset();
			AmbientOwner.Reset();
			SequenceOwner.Reset();
			DialogueBodyOwner.Reset();
			// The beat's own ReleaseNpc still runs; it must find nothing left to give back rather
			// than releasing a token this invalidation already retired.
			bScriptBodyRequested = false;
			bScriptBodyHeld = false;
		}
		else
		{
			// Waking, not going dormant: every NPC gets a think back, standing ones included.
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
		if (Motor)
		{
			Motor->SetEnabled(!IsInert());
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << PatrolType;
		Ar << PatrolPath;
		Ar << PatrolIndex;
		uint8 Active = bPatrolActive ? 1 : 0;
		Ar << Active;
		if (Ar.IsLoading())
		{
			// A restored body stands where the payload puts it, so no in-flight travel survives
			// the load. The beat that owned it re-issues its own move (FElysiumScriptedSequence).
			EndScriptMove();
		}
		if (Ar.IsLoading() && Ar.AtEnd())
		{
			bPatrolActive = Active != 0 && ResolvePatrolPoints();
			bMoveIssued = false;
			bWalkingAnimation = false;
			if (bPatrolActive)
			{
				NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
			}
			return; // compatibility with snapshots written before ambient-place state existed
		}
		uint8 SavedAmbientPhase = static_cast<uint8>(AmbientPhase);
		Ar << SavedAmbientPhase;
		Ar << CurrentSpotIndex;
		Ar << AmbientLeaveAt;
		Ar << AmbientNextActivityAt;
		Ar << AmbientActivityCycle;
		uint8 SavedAmbientArrived = bAmbientArrived ? 1 : 0;
		Ar << SavedAmbientArrived;
		if (Ar.IsLoading())
		{
			bPatrolActive = Active != 0 && ResolvePatrolPoints();
			bMoveIssued = false;
			bWalkingAnimation = false;
			AmbientPhase = static_cast<EAmbientPhase>(SavedAmbientPhase);
			bAmbientArrived = SavedAmbientArrived != 0;
			if (bPatrolActive)
			{
				CurrentSpotIndex = INDEX_NONE;
				AmbientPhase = EAmbientPhase::None;
				bAmbientArrived = false;
				NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
			}
			if (!bPatrolActive && AmbientPhase != EAmbientPhase::None)
			{
				FElysiumInterestingPlace* Spot = CurrentAmbientSpot();
				if (!Spot || !Spot->Claim(Handle))
				{
					CurrentSpotIndex = INDEX_NONE;
					AmbientPhase = EAmbientPhase::None;
					bAmbientArrived = false;
				}
				NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
			}
		}

		// Version 13 appends the maker relationship after the pre-existing NPC leaf. Older saves
		// deliberately restore legacy runtime NPCs unowned rather than guessing a maker association.
		if (Ar.Version() >= FElysiumSaveVersion::NpcMaker)
		{
			Ar << OwnerEntity;
			uint8 OwnerNotified = bOwnerTerminationNotified ? 1 : 0;
			Ar << OwnerNotified;
			if (Ar.IsLoading())
			{
				OwnerEntity = World
					? World->RebaseSavedHandle(OwnerEntity) : FElysiumEntityHandle::Invalid();
				bOwnerTerminationNotified = OwnerNotified != 0;
			}
		}

		if (Ar.Version() >= FElysiumSaveVersion::NpcMind)
		{
			uint8 SavedState = static_cast<uint8>(Mind.State());
			EElysiumBodyOwner ResumableOwner = Mind.Owner();
			if (!FElysiumNpcMind::IsResumableOwner(ResumableOwner))
			{
				ResumableOwner = EElysiumBodyOwner::None;
			}
			uint8 SavedOwner = static_cast<uint8>(ResumableOwner);
			Ar << SavedState;
			Ar << SavedOwner;
			if (Ar.IsLoading())
			{
				const EElysiumNpcState State = static_cast<EElysiumNpcState>(SavedState);
				EElysiumBodyOwner Owner = static_cast<EElysiumBodyOwner>(SavedOwner);
				if (!FElysiumNpcMind::IsSupportedState(State)
					|| !FElysiumNpcMind::IsResumableOwner(Owner))
				{
					Owner = EElysiumBodyOwner::None;
				}
				if (Owner == EElysiumBodyOwner::Patrol && !bPatrolActive)
				{
					Owner = EElysiumBodyOwner::None;
				}
				if (Owner == EElysiumBodyOwner::Ambient && AmbientPhase == EAmbientPhase::None)
				{
					Owner = EElysiumBodyOwner::None;
				}
				Mind.Restore(State, Owner);
				PatrolOwner = Owner == EElysiumBodyOwner::Patrol
					? Mind.CurrentToken() : FElysiumBodyOwnerToken();
				AmbientOwner = Owner == EElysiumBodyOwner::Ambient
					? Mind.CurrentToken() : FElysiumBodyOwnerToken();
				// A restore never resumes `Sequence` ownership, so any token from before the load is
				// retired with it. The request survives: whichever order the two entities restore in,
				// a beat that re-stamps its queue lock has its claim taken again on the next think.
				SequenceOwner.Reset();
				bScriptBodyHeld = false;
			}
		}

		// The schedule's IDENTITY is saved; its task position is not, and that is deliberate. A task
		// holds a playing clip, a pending motor move or a wall-clock deadline, and none of those
		// survive a load -- so resuming at task 3 would hold a pose nothing is playing. Restarting
		// the same program preserves the intent (an NPC mid-lookaround resumes looking around rather
		// than dropping to its stance) without pretending the state under it survived.
		if (Ar.Version() >= FElysiumSaveVersion::NpcSchedule)
		{
			uint8 SavedSchedule = static_cast<uint8>(Schedule.Current);
			Ar << SavedSchedule;
			if (Ar.IsLoading())
			{
				Schedule.Clear();
				const EElysiumScheduleId Restored = static_cast<EElysiumScheduleId>(SavedSchedule);
				if (Restored != EElysiumScheduleId::None && ElysiumScheduleFor(Restored) != nullptr)
				{
					ElysiumSchedule::Start(Schedule, Restored, *this);
				}
				// NextThink is deliberately NOT touched here. The base record serializes it, so a
				// restored NPC already carries the cadence it was saved on -- rewriting it to "now"
				// would discard saved state and make the payload fail its own round trip.
			}
		}

		if (Ar.Version() >= FElysiumSaveVersion::NpcSocial)
		{
			Relationships.Serialize(Ar);
			if (Ar.IsLoading() && World)
			{
				Relationships.Rebase(*World);
			}
		}
	}

	virtual const TCHAR* SaveBlockReason() const override
	{
		switch (Mind.Owner())
		{
		case EElysiumBodyOwner::Dialogue:          return TEXT("a conversation is open");
		case EElysiumBodyOwner::Sequence:          return TEXT("a scripted sequence is active");
		case EElysiumBodyOwner::ScriptedSchedule:  return TEXT("a scripted schedule is active");
		case EElysiumBodyOwner::Follower:          return TEXT("a follower session is active");
		default:                                   return nullptr;
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		FElysiumCombatCharacter::GetDebugState(Out);
		Out.Emplace(TEXT("UseInteresting"), bUseInteresting ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Interesting groups"), InterestingPlaceGroups.IsEmpty()
			? TEXT("(all)") : InterestingPlaceGroups);
		Out.Emplace(TEXT("In dialog"), bInDialog
			? FString::Printf(TEXT("YES (%s raw=%d decoded=%d)"),
				ElysiumDialogueCamera::LexToString(DialogOpener), DialogFlags, DecodedDialogFlags)
			: TEXT("no"));
		Out.Emplace(TEXT("default_camera"), DefaultCamera.IsEmpty() ? TEXT("(none)") : DefaultCamera);
		Out.Emplace(TEXT("Times talked"), FString::FromInt(TimesTalked));
		const FElysiumEntityHandle Player = World ? World->PlayerHandle()
			: FElysiumEntityHandle::Invalid();
		Out.Emplace(TEXT("Disposition"), FString::Printf(TEXT("%s L%d%s"), *Disposition,
			DispositionLevel, IsDispositionTalking() ? TEXT(" talking") : TEXT("")));
		Out.Emplace(TEXT("Relationship to player"), FString::Printf(
			TEXT("%s (table %d entity / %d class; combat consumer NOT IMPLEMENTED)"),
			ElysiumRelationships::LexToString(Relationships.Resolve(Player, TEXT("player"))),
			Relationships.NumEntityRules(), Relationships.NumClassRules()));
		if (!StatTemplate.IsEmpty())
		{
			Out.Emplace(TEXT("Stat template"), StatTemplate);
		}
		Out.Emplace(TEXT("Fast food"), bFastFood ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal (standing)") : TEXT("(none)"));
		Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
		const TCHAR* Admission = Mind.Admission() == FElysiumNpcMind::EAdmission::Spawned
			? TEXT("spawned") : (Mind.Admission() == FElysiumNpcMind::EAdmission::Armed
				? TEXT("armed") : TEXT("admitted"));
		Out.Emplace(TEXT("Mind"), FString::Printf(TEXT("%s current=%s ideal=%s"), Admission,
			LexToString(Mind.State()), LexToString(Mind.IdealState())));
		Out.Emplace(TEXT("Body owner"), FString::Printf(TEXT("%s gen=%u parked=%s"),
			LexToString(Mind.Owner()), Mind.Generation(), LexToString(Mind.SuspendedOwner())));
		Out.Emplace(TEXT("Mind transition"), Mind.LastTransition().IsEmpty()
			? TEXT("(none)") : Mind.LastTransition());
		Out.Emplace(TEXT("Patrol"), bPatrolActive
			? FString::Printf(TEXT("point %d/%d: %s"), PatrolIndex + 1, PatrolPoints.Num(), *PatrolPath)
			: TEXT("inactive"));
		Out.Emplace(TEXT("Ambient place"), CurrentSpotIndex == INDEX_NONE
			? TEXT("searching")
			: FString::Printf(TEXT("#%d phase=%d"), CurrentSpotIndex,
				static_cast<int32>(AmbientPhase)));
		Out.Emplace(TEXT("Scripted move"), ScriptPhase == EScriptPhase::None
			? TEXT("(free)")
			: FString::Printf(TEXT("%s to %s, %.0fcm out"),
				ScriptPhase == EScriptPhase::Travel ? TEXT("travelling") : TEXT("facing"),
				*ScriptMark.ToString(), FVector::Dist2D(Origin, ScriptMark)));
	}

private:
	FElysiumNpcMind Mind;
	FElysiumBodyOwnerToken PatrolOwner;
	FElysiumBodyOwnerToken AmbientOwner;
	FElysiumBodyOwnerToken SequenceOwner;
	FElysiumBodyOwnerToken DialogueBodyOwner;
	TArray<FString> PatrolNames;
	TArray<FVector> PatrolPoints;
	bool bPatrolActive = false;
	// A scripted beat has taken this NPC and has not given it back, and whether the arbiter claim
	// behind that request is in hand. The two differ only while a claim is deferred: the beat-queue
	// lock is stamped synchronously, the arbiter claim can arrive a think later.
	bool bScriptBodyRequested = false;
	bool bScriptBodyHeld = false;
	bool bMoveIssued = false;
	bool bWalkingAnimation = false;
	EAmbientPhase AmbientPhase = EAmbientPhase::None;
	int32 CurrentSpotIndex = INDEX_NONE;
	double AmbientLeaveAt = 0.0;
	double AmbientNextActivityAt = 0.0;
	int32 AmbientActivityCycle = 0;
	bool bAmbientArrived = false;
	TSet<int32> FailedSpotIndices;
	TSet<int32> AmbientGroups;
	bool bAmbientGroupsParsed = false;
};

// ============================================================================================
// npc_VPlayerController — the scene-owned duplicate of the player. It shares only the authored
// scripted-sequence motor with ordinary NPCs: no dialogue, AI, use body, or autonomous think.
// ============================================================================================

class FElysiumPlayerControllerNpc final : public FElysiumScriptedCharacter
{
public:
	virtual void Spawn() override
	{
		if (CVarNpcBodies.GetValueOnGameThread() != 0)
		{
			BuildBody();
			BuildControllerMotor();
		}
	}

	virtual void OnRuntimeModelChanged() override
	{
		if (CVarNpcBodies.GetValueOnGameThread() != 0)
		{
			EndScriptMove();
			DestroyMotor();
			FElysiumAnimating::OnRuntimeModelChanged();
			BuildControllerMotor();
		}
	}

	virtual void SetIgnoreCharacterCollision(bool) override
	{
		// The duplicate navigates against the world but never becomes a second solid character.
		if (Motor)
		{
			Motor->SetIgnoreCharacterCollision(true);
		}
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumCombatCharacter::OnDormancyChanged();
		if (IsInert())
		{
			EndScriptMove();
		}
		if (bDead)
		{
			DestroyMotor();
		}
		else if (Motor)
		{
			Motor->SetEnabled(!IsInert());
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		FElysiumCombatCharacter::GetDebugState(Out);
		Out.Emplace(TEXT("Role"), TEXT("player controller (non-AI, non-solid)"));
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal") : TEXT("(none)"));
		Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
	}

private:
	void BuildControllerMotor()
	{
		BuildMotor();
		SetIgnoreCharacterCollision(true);
	}
};

// ============================================================================================
// FElysiumNpcMaker — npc_maker: retail admission, quotas, timed retries and child ownership.
// ============================================================================================

class FElysiumNpcMaker final : public FElysiumEntity
{
public:
	FString NpcType;
	int32 RemainingTotal = 0;       // MaxNPCCount is the mutable remaining finite total
	float SpawnFrequency = 0.0f;
	int32 LiveChildren = 0;
	int32 MaxLiveChildren = 0;
	float CachedGroundZ = 0.0f;
	FString ChildTargetName;
	bool bDisabled = false;
	bool bNpcClip = false;
	bool bFade = false;
	bool bInfinite = false;
	bool bNoDrop = false;           // base CNPCMaker declares it but does not consume it
	bool bViewCone = false;
	int32 MinPcDistance = 0;        // Source units

	enum class EAttempt : uint8
	{
		Spawned,
		LiveLimit,
		Scene,
		Visible,
		ViewCone,
		Distance,
		Occupied,
		InvalidChild,
	};
	EAttempt LastAttempt = EAttempt::InvalidChild;

	static const TCHAR* AttemptName(EAttempt Attempt)
	{
		switch (Attempt)
		{
		case EAttempt::Spawned:      return TEXT("spawned");
		case EAttempt::LiveLimit:    return TEXT("live-limit");
		case EAttempt::Scene:        return TEXT("scene");
		case EAttempt::Visible:      return TEXT("visible");
		case EAttempt::ViewCone:     return TEXT("view-cone");
		case EAttempt::Distance:     return TEXT("distance");
		case EAttempt::Occupied:     return TEXT("occupied");
		case EAttempt::InvalidChild: return TEXT("invalid-child");
		}
		return TEXT("unknown");
	}

	bool IsDepleted() const { return !bInfinite && RemainingTotal < 1; }

	virtual void Spawn() override
	{
		LiveChildren = 0;
		CachedGroundZ = 0.0f;
		if (bInfinite)
		{
			bFade = true;
		}
		NextThink = bDisabled ? ELYSIUM_NEVER_THINK
			: static_cast<float>((World ? World->NowSeconds() : 0.0) + SpawnFrequency);
	}

	EAttempt CanMakeNpc(bool bBypass) const
	{
		if (bBypass)
		{
			return EAttempt::Spawned;
		}
		if (MaxLiveChildren > 0 && LiveChildren >= MaxLiveChildren)
		{
			return EAttempt::LiveLimit;
		}
		if (World && World->IsNpcMakerSceneBlocked())
		{
			return EAttempt::Scene;
		}
		const FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
		const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (Player)
		{
			if (bNpcClip && Embodiment && Embodiment->IsNpcMakerVisibleFromPlayer(Origin))
			{
				return EAttempt::Visible;
			}
			if (bViewCone && Embodiment && Embodiment->IsNpcMakerInPlayerViewCone(Origin))
			{
				return EAttempt::ViewCone;
			}
			if (MinPcDistance > 0)
			{
				const int32 DistanceUnits = FMath::TruncToInt(
					FVector::Dist(Player->Origin, Origin) / ElysiumMove::U);
				if (DistanceUnits < MinPcDistance)
				{
					return EAttempt::Distance;
				}
			}
		}
		if (Embodiment && Embodiment->IsNpcMakerSpawnAreaOccupied(
			FVector(Origin.X, Origin.Y, CachedGroundZ), 34.0f * ElysiumMove::U))
		{
			return EAttempt::Occupied;
		}
		return EAttempt::Spawned;
	}

	EAttempt TrySpawn(bool bBypass = false)
	{
		if (!World || !Def)
		{
			return LastAttempt = EAttempt::InvalidChild;
		}
		if (CachedGroundZ == 0.0f)
		{
			CachedGroundZ = World->Embodiment()
				? World->Embodiment()->ResolveNpcMakerGroundZ(Origin, 2048.0f * ElysiumMove::U)
				: Origin.Z;
		}
		const EAttempt Admission = CanMakeNpc(bBypass);
		if (Admission != EAttempt::Spawned)
		{
			UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Spawn rejected: %s (live %d/%d)"),
				*DebugString(), AttemptName(Admission), LiveChildren, MaxLiveChildren);
			return LastAttempt = Admission;
		}
		if (NpcType.IsEmpty())
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: no NPCType"), *DebugString());
			return LastAttempt = EAttempt::InvalidChild;
		}

		static const TSet<FName> MakerOnlyKeys = {
			FName(TEXT("classname")), FName(TEXT("targetname")), FName(TEXT("origin")),
			FName(TEXT("angles")), FName(TEXT("spawnflags")), FName(TEXT("NPCType")),
			FName(TEXT("MaxNPCCount")), FName(TEXT("SpawnFrequency")),
			FName(TEXT("MaxLiveChildren")), FName(TEXT("NPCTargetname")),
			FName(TEXT("Flag_StartDisabled")), FName(TEXT("Flag_NPCClip")),
			FName(TEXT("Flag_Fade")), FName(TEXT("Flag_InfChild")),
			FName(TEXT("Flag_NoDrop")), FName(TEXT("Flag_ViewCone")),
			FName(TEXT("MinPCDistance"))
		};
		FElysiumEntityDef Child;
		Child.Classname = NpcType;
		Child.Origin = Origin;
		for (const TPair<FString, FString>& KV : Def->Keys)
		{
			if (!MakerOnlyKeys.Contains(FName(*KV.Key)))
			{
				Child.Keys.Add(KV.Key, KV.Value);
			}
		}
		Child.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%g %g %g"), Angles.X, Angles.Y, Angles.Z));
		Child.Outputs = Def->Outputs; // each Construct seeds fresh per-row times counters

		const FElysiumEntityHandle ChildHandle = World->CreateRuntimeEntityNoSpawn(MoveTemp(Child));
		FElysiumEntity* ChildEntity = World->Resolve(ChildHandle);
		if (!ChildEntity || ChildEntity->IsRecordOnly() || !ChildEntity->AsCombatCharacter())
		{
			if (ChildEntity)
			{
				ChildEntity->Kill();
			}
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' is not a live NPC class"),
				*DebugString(), *NpcType);
			return LastAttempt = EAttempt::InvalidChild;
		}

		static const FName OnSpawnNpc(TEXT("OnSpawnNPC"));
		FireOutput(OnSpawnNpc, Handle);
		ChildEntity->SpawnFlags = bFade ? 0x204 : 4;
		World->CallEntitySpawn(*ChildEntity);
		if (ChildEntity->IsDead())
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' removed itself during Spawn"),
				*DebugString(), *NpcType);
			return LastAttempt = EAttempt::InvalidChild;
		}
		ChildEntity->SetOwnerEntity(Handle);
		World->RenameEntity(*ChildEntity, ChildTargetName);
		++LiveChildren;
		if (!bInfinite)
		{
			--RemainingTotal;
			if (IsDepleted())
			{
				NextThink = ELYSIUM_NEVER_THINK;
			}
		}
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s (live %d/%d, remaining %d%s)"),
			*DebugString(), *World->DescribeHandle(ChildHandle), LiveChildren, MaxLiveChildren,
			RemainingTotal, bInfinite ? TEXT(" infinite") : TEXT(""));
		return LastAttempt = EAttempt::Spawned;
	}

	void InputSpawn(const FElysiumInputArgs&) { TrySpawn(/*bBypass=*/false); }

	void InputEnable(const FElysiumInputArgs&)
	{
		if (IsDepleted())
		{
			return;
		}
		bDisabled = false;
		NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
	}
	void InputDisable(const FElysiumInputArgs&)
	{
		bDisabled = true;
		NextThink = ELYSIUM_NEVER_THINK;
	}
	void InputToggle(const FElysiumInputArgs& Args)
	{
		if (bDisabled) { InputEnable(Args); }
		else { InputDisable(Args); }
	}

	virtual void Think() override
	{
		const EAttempt Result = TrySpawn(/*bBypass=*/false);
		if (Result == EAttempt::Spawned && IsDepleted())
		{
			return;
		}
		const double Now = World ? World->NowSeconds() : 0.0;
		if (Result == EAttempt::Spawned || Result == EAttempt::LiveLimit)
		{
			NextThink = static_cast<float>(Now + SpawnFrequency);
		}
		else
		{
			NextThink = static_cast<float>(Now
				+ ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f));
		}
	}

	virtual void OnOwnedEntityTerminated(FElysiumEntity& Child,
		EElysiumOwnedEntityTermination Reason) override
	{
		if (Reason == EElysiumOwnedEntityTermination::RemovedAlive)
		{
			if (!bInfinite)
			{
				++RemainingTotal;
			}
		}
		else
		{
			static const FName OnNpcDied(TEXT("OnNPCDied"));
			FireOutput(OnNpcDied, Child.Handle);
		}
		if (IsDepleted())
		{
			static const FName OnLastNpcDied(TEXT("OnLastNPCDied"));
			FireOutput(OnLastNpcDied, Child.Handle);
		}
		LiveChildren = FMath::Max(0, LiveChildren - 1);
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
		Out.Emplace(TEXT("NPCType"), NpcType.IsEmpty() ? TEXT("(none)") : NpcType);
		Out.Emplace(TEXT("NPCTargetname"), ChildTargetName.IsEmpty() ? TEXT("(none)") : ChildTargetName);
		Out.Emplace(TEXT("Live children"), FString::Printf(TEXT("%d / %d"), LiveChildren, MaxLiveChildren));
		Out.Emplace(TEXT("Remaining total"), bInfinite ? TEXT("infinite") : FString::FromInt(RemainingTotal));
		Out.Emplace(TEXT("Spawn frequency"), FString::Printf(TEXT("%.3f s"), SpawnFrequency));
		Out.Emplace(TEXT("Cached ground Z"), FString::SanitizeFloat(CachedGroundZ));
		Out.Emplace(TEXT("Last attempt"), AttemptName(LastAttempt));
	}
};

#if WITH_DEV_AUTOMATION_TESTS
bool ElysiumNpcTestHooks::ApplyResolvedTemplate(FElysiumEntity& Entity,
	const FElysiumClanTemplate& Resolved)
{
	if (!Entity.Def || !Entity.Def->Classname.StartsWith(TEXT("npc_V"), ESearchCase::IgnoreCase)
		|| !Entity.AsCombatCharacter())
	{
		return false;
	}
	static_cast<FElysiumNpc&>(Entity).ApplyResolvedTemplate(Resolved, /*Table*/ nullptr);
	return true;
}
#endif

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeNpc()       { return MakeUnique<FElysiumNpc>(); }
static TUniquePtr<FElysiumEntity> MakeController(){ return MakeUnique<FElysiumPlayerControllerNpc>(); }
static TUniquePtr<FElysiumEntity> MakeNpcMaker()  { return MakeUnique<FElysiumNpcMaker>(); }
static TUniquePtr<FElysiumEntity> MakeInterestingPlace() { return MakeUnique<FElysiumInterestingPlace>(); }

static void BuildNpcClass(FElysiumClassDesc& D)
{
	// `WillTalk` and `SetAnimation` are not here: they belong to CBaseCombatCharacter and
	// CBaseAnimating, and the chain walk (R2) reaches them — which is the point of 11.4 giving the
	// NPC the same two ancestors VtMB gives it.
	D.Input(TEXT("UseInteresting"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputUseInteresting(Args); });
	D.Input(TEXT("StartPlayerDialogRemote"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartPlayerDialogRemote(Args); });
	D.Input(TEXT("StartPlayerDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartPlayerDialog(Args); });
	D.Input(TEXT("StartPlayerDialogUnforced"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartPlayerDialogUnforced(Args); });
	D.Input(TEXT("EndDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputEndDialog(Args); });
	D.Input(TEXT("SetupPatrolType"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputSetupPatrolType(Args); });
	D.Input(TEXT("FollowPatrolPath"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputFollowPatrolPath(Args); });
	D.Input(TEXT("ClearPatrolPath"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputClearPatrolPath(Args); });
	D.Input(TEXT("SetRelationship"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputSetRelationship(Args); });

	// The runtime folds the retail CAI_BaseNPC / CAI_BaseNPCTroika nodes into every registered
	// `npc_*` leaf; the registry's class walk still exposes their shared input surface.
	using FN = FElysiumNpc;
	// SetRelationship's store/writer is live above. Enemy assignment, senses and combat schedules
	// are intentionally absent from this talk/feed slice and remain visible in NPC diagnostics.
	// `TeleportToEntity` is CAI_BaseNPCTroika's recovered FIELD_EHANDLE input. The current corpus
	// carries 40 wires across three NPC families; all use the same shared leaf implementation here.
	D.Input(TEXT("TeleportToEntity"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputTeleportToEntity(Args); });

	// The remaining map-fired gap is 8 `SetScriptedDiscipline` wires across the exported maps, all
	// of them aimed at an `npc_*` receiver.
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FN, SetScriptedDiscipline, "P13 — disciplines");

	// `TakeDamage` — 4 map wires, plus the same name as a Character method the script surface
	// dispatches (K1: two bindings, one implementation). The wire's own datamap record is
	// unrecovered, so its argument's FIELD TYPE is a genuine unknown; what the corpus passes is a
	// number, and a number routes to the scalar fallback exactly as the script call does. A
	// parameter that is not numeric is refused and reported rather than turned into a plausible
	// default, because the marshalling contract is what is missing, not the receiver.
	D.Input(TEXT("TakeDamage"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			FElysiumNpc& Npc = static_cast<FElysiumNpc&>(E);
			const float Amount = Args.Param.ToFloat();
			if (Amount <= 0.f)
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("%s TakeDamage '%s' is not a positive number — refused (the recovered "
						"datamap record does not name this input's field type)"),
					*Npc.DebugString(), *Args.Param.Describe());
				return;
			}
			Npc.TakeDamage(Amount);
		});

	AddNpcField(D, TEXT("use_interesting"), &FElysiumNpc::bUseInteresting);
	AddNpcField(D, TEXT("allow_alert_lookaround"), &FElysiumNpc::bAllowAlertLookaround);
	AddNpcField(D, TEXT("default_camera"), &FElysiumNpc::DefaultCamera, EElysiumField::Key);
	AddNpcField(D, TEXT("player_reaction"), &FElysiumNpc::PlayerReaction, EElysiumField::Key);
	AddNpcField(D, TEXT("stattemplate"),    &FElysiumNpc::StatTemplate);
	AddNpcField(D, TEXT("interesting_place_groups"), &FElysiumNpc::InterestingPlaceGroups);
	// times_talked: santamonica/chinatown/e3/demo read `npc.times_talked` to branch first-vs-repeat
	// dialogue. Register it read-only (engine-written, script-read) so the read resolves to a defined
	// value instead of raising AttributeError. B4's dialogue runner drives the count; it stays 0 until then.
	AddNpcField(D, TEXT("times_talked"), &FElysiumNpc::TimesTalked, EElysiumField::Save);

	// The stance pair, under retail's own datamap names. Save-only: they carry flag `0x2` there, so
	// they persist and no keyvalue or script writes them. Nothing in the recovered writer set resets
	// either on a schedule, state, dialogue or disposition change, which is why a character keeps its
	// stance across a conversation.
	//
	// The index is clamped on restore rather than trusted: it addresses a three-slot array, and a
	// payload written by another build must not be able to index past it.
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(EElysiumField::Save);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [](const FElysiumEntity& E)
		{ return FElysiumVariant::Int(static_cast<const FElysiumNpc&>(E).Stance.Current); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{
			static_cast<FElysiumNpc&>(E).Stance.Current =
				FMath::Clamp(V.ToInt(), 0, ElysiumStance::Count - 1);
		};
		D.Fields.Add(FName(TEXT("m_CurrStance")), MoveTemp(Acc));
	}
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(EElysiumField::Save);
		Acc.Type = EElysiumVariantType::Float;
		Acc.Get = [](const FElysiumEntity& E)
		{ return FElysiumVariant::Float(static_cast<const FElysiumNpc&>(E).Stance.LastChangeTime); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{ static_cast<FElysiumNpc&>(E).Stance.LastChangeTime = V.ToFloat(); };
		D.Fields.Add(FName(TEXT("m_flStanceTime")), MoveTemp(Acc));
	}
}

static void BuildInterestingPlaceClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumInterestingPlace&>(E).InputToggle(Args); });
	AddNpcField(D, TEXT("type"),              &FElysiumInterestingPlace::Type);
	AddNpcField(D, TEXT("enabled"),           &FElysiumInterestingPlace::bEnabled);
	AddNpcField(D, TEXT("max_npcs"),          &FElysiumInterestingPlace::MaxNpcs);
	AddNpcField(D, TEXT("group_id"),          &FElysiumInterestingPlace::GroupId);
	AddNpcField(D, TEXT("rating"),            &FElysiumInterestingPlace::Rating);
	AddNpcField(D, TEXT("testflags"),         &FElysiumInterestingPlace::TestFlags);
	AddNpcField(D, TEXT("match_orientation"), &FElysiumInterestingPlace::bMatchOrientation);
	AddNpcField(D, TEXT("min_time"),          &FElysiumInterestingPlace::MinTime);
	AddNpcField(D, TEXT("max_time"),          &FElysiumInterestingPlace::MaxTime);
}

static void BuildNpcMakerClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Spawn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputSpawn(Args); });
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputToggle(Args); });

	using FM = FElysiumNpcMaker;
	AddNpcField(D, TEXT("NPCType"),           &FM::NpcType);
	AddNpcField(D, TEXT("MaxNPCCount"),       &FM::RemainingTotal);
	AddNpcField(D, TEXT("SpawnFrequency"),    &FM::SpawnFrequency);
	AddNpcField(D, TEXT("m_cLiveChildren"),   &FM::LiveChildren, EElysiumField::Save);
	AddNpcField(D, TEXT("MaxLiveChildren"),   &FM::MaxLiveChildren);
	AddNpcField(D, TEXT("m_flGround"),        &FM::CachedGroundZ, EElysiumField::Save);
	AddNpcField(D, TEXT("NPCTargetname"),     &FM::ChildTargetName);
	AddNpcField(D, TEXT("Flag_StartDisabled"),&FM::bDisabled);
	AddNpcField(D, TEXT("Flag_NPCClip"),      &FM::bNpcClip);
	AddNpcField(D, TEXT("Flag_Fade"),         &FM::bFade);
	AddNpcField(D, TEXT("Flag_InfChild"),     &FM::bInfinite);
	AddNpcField(D, TEXT("Flag_NoDrop"),       &FM::bNoDrop);
	AddNpcField(D, TEXT("Flag_ViewCone"),     &FM::bViewCone);
	AddNpcField(D, TEXT("MinPCDistance"),      &FM::MinPcDistance);
}

// One shared leaf per living-NPC classname (a class-for-class registration, so the registry's exact
// case-folded Find resolves each). npc_VCamera is a camera control entity with no model — left as an
// inert record for now. The two maker classnames share the maker leaf.
struct FElysiumNpcRegistrar
{
	FElysiumNpcRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		BuildInterestingPlaceClass(Reg.Register(TEXT("intersting_place"),
			ElysiumBaseClassName(), &MakeInterestingPlace));

		static const TCHAR* const NpcClasses[] = {
			TEXT("npc_VVampire"), TEXT("npc_VPedestrian"), TEXT("npc_VHumanCombatant"),
			TEXT("npc_VRat"), TEXT("npc_VDialogPedestrian"), TEXT("npc_VCop"),
			TEXT("npc_VTaxiDriver"), TEXT("npc_VHuman"), TEXT("npc_VHunter"),
			TEXT("npc_VTzimisceRunner"), TEXT("npc_VNewscaster"), TEXT("npc_VAnimal"),
			TEXT("npc_VSabbatLeader"), TEXT("npc_VAndreiBlood"),
		};
		for (const TCHAR* Name : NpcClasses)
		{
			// CAI_BaseNPC's place in VtMB's chain: under CBaseCombatCharacter, which is under
			// CBaseAnimating (11.4). The sheet, the counters and the body all arrive through it.
			BuildNpcClass(Reg.Register(FName(Name), ElysiumCombatCharacterClassName(), &MakeNpc));
		}

		// Created only at runtime by events_player.CreateControllerNPC. It intentionally does not
		// receive FElysiumNpc's dialogue surface or any AI behavior.
		Reg.Register(TEXT("npc_VPlayerController"), ElysiumCombatCharacterClassName(), &MakeController);

		static const TCHAR* const MakerClasses[] = { TEXT("npc_maker"), TEXT("npc_maker_fleshpile") };
		for (const TCHAR* Name : MakerClasses)
		{
			BuildNpcMakerClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeNpcMaker));
		}
	}
};

static FElysiumNpcRegistrar GElysiumNpcRegistrar;
