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
// Deliberately out of scope (8.5 / B4 / B6): all AI, scripted_sequence anim-at-marker, the +use talk
// path (an NPC has no use-body yet — the porch trigger drives dialog directly), the real .dlg runner
// (B4 replaces the manual EndDialog seam), and feeding (OnFedUpon*, B6). SpawnFrequency /
// MaxLiveChildren / MaxNPCCount are ignored — a maker spawns exactly one child per Spawn input.

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumPendingInput.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumInterestingPlaces.h"

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
// FElysiumNpc — the character leaf shared by every living `npc_*` classname. It stands a skeletal
// model at its origin, follows named patrols or interesting-place routes, and owns dialogue gates.
// ============================================================================================

class FElysiumNpc final : public FElysiumCombatCharacter
{
public:
	bool  bUseInteresting = false;    // use_interesting — the NPC is a look/use target (seeded from the key)
	bool  bInDialog = false;          // a dialog session is open (OnDialogBegin fired, OnDialogEnd pending)
	int32 DialogFlags = 0;            // the StartPlayerDialogRemote param, kept for B4's runner
	int32 TimesTalked = 0;            // times_talked — dialogue interaction count (engine-written; script-read)
	FString StatTemplate;             // stattemplate — the `npctemplate*.txt` stat block this NPC wears
	FString InterestingPlaceGroups;   // authored group allowlist; prevents cross-district wandering
	FString PatrolType;               // raw SetupPatrolType contract (kept for save/debug and later modes)
	FString PatrolPath;               // authored space-separated info_node_patrol_point names
	int32 PatrolIndex = 0;            // next point in the looping authored sequence
	enum class EAmbientPhase : uint8 { None, Moving, Into, Dwelling, Out };
	// Whether a `scripted_sequence` beat currently owns this body, and which half of the move it
	// is in — travel to the mark, then the turn onto the mark's own angles.
	enum class EScriptPhase : uint8 { None, Travel, Facing };

	virtual ~FElysiumNpc() override
	{
		DestroyMotor();
	}

	void DestroyMotor()
	{
		if (Motor && World && World->Embodiment())
		{
			World->Embodiment()->DestroyNpcMotor(Motor);
		}
		Motor = nullptr;
	}

	// The sheet, the WillTalk latch, `default_disposition`, the skeletal body and everything that
	// plays a clip on it now come from the chain (11.4): FElysiumCombatCharacter over
	// FElysiumAnimating, which is where VtMB puts them. This leaf is the dialogue half.

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

	// --- The scripted-move seam (8.5) -------------------------------------------------------
	// A `scripted_sequence` beat with `m_fMoveTo` 1/2/3/5 owns this body for its travel phase: the
	// beat issues the move and advances it, this class owns the motor, the gait's speed and cycle,
	// and the sample back into the entity's own origin/angles. While the script owns the body its
	// autonomous behaviour is parked, not forgotten — the route data survives and `EndScriptMove`
	// re-arms it.

	virtual bool BeginScriptMove(const FVector& Mark, const FVector& MarkAngles,
		EElysiumScriptGait Gait, const FString& CustomClip) override
	{
		if (!Motor || IsInert())
		{
			return false;   // no body to walk: the beat places the NPC on the mark instead
		}

		// The ambient claim is released the same way dialogue releases it — one exit, so an
		// `intersting_place` cannot stay reserved by an NPC a cutscene has taken away.
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		Motor->Stop();
		bMoveIssued = false;
		bWalkingAnimation = false;

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

		float Speed = (Gait == EElysiumScriptGait::Run)
			? ElysiumNpcGait::RunSpeed : ElysiumNpcGait::WalkSpeed;
		FString ScriptWalkLabel;
		FString ScriptWalkAnim;
		// The theatre walk-out is ordinary scripted Walk travel. Its visible cycle is in place, but
		// the selected forward grid cell carries the displacement the original motor used as ground
		// speed. Resolve before MoveTo so the body and its ACT_WALK cycle share one authored stride.
		// Other scripted gaits and autonomous patrols retain their established tuning.
		if (Gait == EElysiumScriptGait::Walk)
		{
			IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
			float AuthoredSpeed = 0.f;
			if (Embodiment && Embodiment->ResolveNpcActivityClip(ModelStem(), TEXT("ACT_WALK"),
				FMath::Max(0, Handle.Index), ScriptWalkLabel, ScriptWalkAnim, AuthoredSpeed))
			{
				if (FMath::IsFinite(AuthoredSpeed) && AuthoredSpeed > 0.f)
				{
					Speed = AuthoredSpeed;
					UE_LOG(LogElysiumNpcEnt, Verbose,
						TEXT("%s scripted walk uses '%s' -> '%s' authored ground speed %.1fcm/s"),
						*DebugString(), *ScriptWalkLabel, *ScriptWalkAnim, Speed);
				}
			}
		}
		if (!Motor->MoveTo(Mark, ElysiumNpcGait::ScriptAcceptanceCm, Speed,
			/*bAllowPartialPath=*/true))
		{
			ScriptPhase = EScriptPhase::None;
			return false;   // no path to the mark: the beat falls back to placing the NPC there
		}
		ScriptPhase = EScriptPhase::Travel;
		ScriptBestDistance = static_cast<float>(FVector::Dist2D(Origin, Mark));
		// The travel cycle. `m_fMoveTo 3` names its own (`doom_walk`, `claws_aggressive_run`,
		// `wolf_form_run`); Walk plays the exact cell resolved for its speed, while Run takes ACT_RUN.
		bool bTravelCycleStarted = false;
		if (Gait == EElysiumScriptGait::Custom)
		{
			bTravelCycleStarted = PlayAnimClip(CustomClip, /*bLoop=*/true);
		}
		else if (Gait == EElysiumScriptGait::Walk && !ScriptWalkLabel.IsEmpty())
		{
			// The global clip resolver needs the vocabulary label (`walk`) to recover the shared-bank
			// owner; it then resolves the same neutral grid cell (`walk_0`) used for Speed above.
			bTravelCycleStarted = PlayAnimClip(ScriptWalkLabel, /*bLoop=*/true);
		}
		if (!bTravelCycleStarted)
		{
			StartWalkingAnimation(Gait == EElysiumScriptGait::Run);
		}
		// The backstop for a body that is still "moving" but no longer arriving. Retail has no
		// equivalent because retail's mover cannot fail; here a beat that never ends stalls the
		// map, so travel is bounded by both a stall window and an absolute cap off the gait.
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

		// CharacterMovement is the physical authority for the whole travel phase. Write its
		// feet/yaw straight into the entity — SetRuntimeOrigin would teleport it back every tick.
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

		// A body that has stopped closing this near the mark is where it is going to end up, so take
		// it now rather than letting it grind. A body walking straight in never trips this — it
		// improves every tick until path following reports Reached on the tight acceptance radius —
		// and with spawnflag 4096 turning character collision off for the beat, neither does a
		// walker whose mark is shared with four others. Reaching here therefore means something
		// actually blocked the body, which is worth saying out loud.
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
			return BeginScriptFacing(Now);   // out of budget, but standing where the beat wanted it
		}
		ScriptPhase = EScriptPhase::None;
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s scripted move to %s gave up %.0fcm short (status %d)"),
			*DebugString(), *ScriptMark.ToString(), Remaining, static_cast<int32>(Status));
		return EElysiumScriptMove::Failed;
	}

	virtual void EndScriptMove() override
	{
		if (ScriptPhase == EScriptPhase::None)
		{
			return;
		}
		ScriptPhase = EScriptPhase::None;
		if (Motor)
		{
			Motor->Stop();
		}
		bMoveIssued = false;
		bWalkingAnimation = false;
		// The pose belongs to the beat (`m_iszPlay` / `m_iszPostIdle` / back to the stance idle),
		// so nothing is played here. Hand the body back to its own behaviour.
		if (bPatrolActive || bUseInteresting)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	// --- Body state a cutscene borrows ------------------------------------------------------
	// Both are the motor's business and both are idempotent there, so this is a plain forward. A
	// bodiless NPC (`elysium.NpcBodies 0`, a headless test, a failed spawn) silently does neither,
	// which is correct: there is nothing to immobilise and nothing to collide with.

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

	// Reaching the mark is not the end of the move: HL1's CCineMonster turns the NPC to the
	// marker's own angles before the action animation starts.
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

	virtual void Think() override
	{
		if (IsInert())
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
			EndScriptMove();
			ResetAnimToIdle();
			return;
		}
		if (bInDialog)
		{
			return;
		}
		if (bPatrolActive && !PatrolPoints.IsEmpty())
		{
			ThinkPatrol();
		}
		else if (bUseInteresting)
		{
			ThinkAmbient();
		}
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
		FElysiumInterestingPlace* Best = nullptr;
		float BestDistanceSq = TNumericLimits<float>::Max();
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
			const float DistanceSq = FVector::DistSquared2D(Origin, Spot->Origin);
			if (!Best || DistanceSq < BestDistanceSq
				|| (FMath::IsNearlyEqual(DistanceSq, BestDistanceSq)
					&& Spot->Handle.Index < Best->Handle.Index))
			{
				Best = Spot;
				BestDistanceSq = DistanceSq;
			}
		}
		if (Best && Best->Claim(Handle))
		{
			CurrentSpotIndex = Best->Handle.Index;
			return Best;
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
	void InputStartDialog(const FElysiumInputArgs& Args)
	{
		if (IsInert() || bInDialog)
		{
			return;
		}
		// A conversation opening on an NPC a beat is walking releases the beat's hold rather than
		// silently stranding it: the beat then finishes on the placement fallback and its outputs
		// still fire, which is what the map's flow depends on.
		EndScriptMove();
		FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		if (Motor && bPatrolActive)
		{
			Motor->Stop();
			bMoveIssued = false;
		}
		bInDialog = true;
		DialogFlags = Args.Param.ToInt();
		static const FName OnDialogBegin(TEXT("OnDialogBegin"));
		FireOutput(OnDialogBegin, Args.Activator);
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s StartPlayerDialogRemote(%d)"), *DebugString(), DialogFlags);

		OpenConversation(Args.Activator);
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
		bInDialog = false;
		++TimesTalked;
		static const FName OnDialogEnd(TEXT("OnDialogEnd"));
		FireOutput(OnDialogEnd, Args.Activator);
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s EndDialog (times_talked=%d)"), *DebugString(), TimesTalked);
		if (bPatrolActive || bUseInteresting)
		{
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	// Load this NPC's `dialogname` `.dlg`, open a branch conversation bound to the installed script host,
	// and hand it to the world (the visual-novel box renders it; the runner fires EndDialog on close).
	// Returns false when there is no dialogue to run, leaving bInDialog latched for the B3 manual seam.
	bool OpenConversation(const FElysiumEntityHandle& Activator)
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

		TSharedRef<FElysiumDlgConversation> Conv =
			MakeShared<FElysiumDlgConversation>(DlgFile, bMale, bMalk, MoveTemp(Cond), MoveTemp(Act));
		Conv->Start();
		World->OpenDialog(Self, Conv);
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
		if (!Table)
		{
			return;   // no rulebook: the sheet stays zeroed and the damage path stays fail-closed
		}
		Sheet.SeedFrom(*Table);

		if (!StatTemplate.IsEmpty())
		{
			UElysiumRulebookSubsystem* Rules = GameState->Rulebook();
			FElysiumClanTemplate Resolved;
			if (Rules && Rules->Clans().Resolve(StatTemplate, Resolved))
			{
				Sheet.ApplyTemplate(Resolved, Table);
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
		if (Visual)
		{
			if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
			{
				Motor = Embodiment->BuildNpcMotor(Visual, Origin, -Angles.Y, ModelStem(),
					FMath::Max(0, Handle.Index));
				if (Motor)
				{
					Motor->SetEnabled(!IsInert());
				}
			}
		}
		if (bUseInteresting && !IsInert())
		{
			NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0)
				+ 0.1 * static_cast<double>(FMath::Max(0, Handle.Index) % 10));
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
		if (Visual)
		{
			if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
			{
				Motor = Embodiment->BuildNpcMotor(Visual, Origin, -Angles.Y, ModelStem(),
					FMath::Max(0, Handle.Index));
				if (Motor)
				{
					Motor->SetEnabled(!IsInert());
				}
			}
		}
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumCombatCharacter::OnDormancyChanged();
		if (IsInert())
		{
			EndScriptMove();
			FinishAmbientUse(/*bFireLeft=*/bAmbientArrived);
		}
		else if (bPatrolActive || bUseInteresting)
		{
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
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		FElysiumCombatCharacter::GetDebugState(Out);
		Out.Emplace(TEXT("UseInteresting"), bUseInteresting ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Interesting groups"), InterestingPlaceGroups.IsEmpty()
			? TEXT("(all)") : InterestingPlaceGroups);
		Out.Emplace(TEXT("In dialog"), bInDialog ? FString::Printf(TEXT("YES (flags %d)"), DialogFlags) : TEXT("no"));
		Out.Emplace(TEXT("Times talked"), FString::FromInt(TimesTalked));
		if (!StatTemplate.IsEmpty())
		{
			Out.Emplace(TEXT("Stat template"), StatTemplate);
		}
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal (standing)") : TEXT("(none)"));
		Out.Emplace(TEXT("Motor"), Motor ? TEXT("Unreal character + Detour crowd") : TEXT("(none)"));
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
	IElysiumNpcMotor* Motor = nullptr; // engine-owned; destroyed through the embodiment seam
	TArray<FString> PatrolNames;
	TArray<FVector> PatrolPoints;
	bool bPatrolActive = false;
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
	EScriptPhase ScriptPhase = EScriptPhase::None;
	FVector ScriptMark = FVector::ZeroVector;
	FVector ScriptMarkAngles = FVector::ZeroVector;
	float ScriptBestDistance = 0.0f;
	double ScriptProgressAt = 0.0;
	double ScriptDeadline = 0.0;
	double ScriptWatchdogAt = 0.0;
};

// ============================================================================================
// npc_VPlayerController — the scene-owned duplicate of the player. It deliberately stops at the
// animating/combat-character layer: no dialogue, AI, use body, collision, or autonomous think.
// ============================================================================================

class FElysiumPlayerControllerNpc final : public FElysiumCombatCharacter
{
public:
	virtual void Spawn() override
	{
		if (CVarNpcBodies.GetValueOnGameThread() != 0)
		{
			BuildBody();
		}
	}

	virtual void OnRuntimeModelChanged() override
	{
		if (CVarNpcBodies.GetValueOnGameThread() != 0)
		{
			FElysiumAnimating::OnRuntimeModelChanged();
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		FElysiumCombatCharacter::GetDebugState(Out);
		Out.Emplace(TEXT("Role"), TEXT("player controller (non-AI, non-solid)"));
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal") : TEXT("(none)"));
	}
};

// ============================================================================================
// FElysiumNpcMaker — npc_maker: a template that spawns one child NPC per Spawn input. The child's
// class is NPCType, its targetname NPCTargetname, standing the maker's model at the maker origin.
// ============================================================================================

class FElysiumNpcMaker final : public FElysiumEntity
{
public:
	bool bEnabled = true;   // Flag_StartDisabled 1 -> starts disabled (latch only; Spawn ignores it)

	virtual void Spawn() override
	{
		// Flag_StartDisabled is a maker spawnflag surrogate, not a CBaseEntity keyfield — read it raw.
		if (Def)
		{
			if (const FString* V = Def->Keys.Find(TEXT("Flag_StartDisabled")))
			{
				bEnabled = FCString::Atoi(**V) == 0;
			}
		}
	}

	// Spawn one child NPC. Retail fires blueblood_maker.Spawn without enabling it first, so Spawn does
	// not gate on bEnabled — the enabled latch would only govern an auto-spawn timer we do not model.
	void InputSpawn(const FElysiumInputArgs&)
	{
		if (!World || !Def)
		{
			return;
		}
		const FString ChildClass = Def->Keys.FindRef(TEXT("NPCType"));
		if (ChildClass.IsEmpty())
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: no NPCType"), *DebugString());
			return;
		}

		FElysiumEntityDef Child;
		Child.Classname = ChildClass;
		Child.TargetName = Def->Keys.FindRef(TEXT("NPCTargetname"));
		Child.Origin = Def->Origin;
		// Carry the template's appearance/identity so the child stands the maker's model.
		for (const TCHAR* Key : { TEXT("model"), TEXT("stattemplate"), TEXT("base_gender"),
			TEXT("default_disposition"), TEXT("angles"), TEXT("use_interesting"),
			TEXT("interesting_place_groups") })
		{
			if (const FString* V = Def->Keys.Find(Key))
			{
				Child.Keys.Add(Key, *V);
			}
		}

		const FElysiumEntityHandle H = World->SpawnRuntimeEntity(MoveTemp(Child));
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s"), *DebugString(), *World->DescribeHandle(H));
	}

	void InputEnable(const FElysiumInputArgs&)  { bEnabled = true; }
	void InputDisable(const FElysiumInputArgs&) { bEnabled = false; }
	void InputToggle(const FElysiumInputArgs&)  { bEnabled = !bEnabled; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
		if (Def)
		{
			Out.Emplace(TEXT("NPCType"), Def->Keys.FindRef(TEXT("NPCType")));
			Out.Emplace(TEXT("NPCTargetname"), Def->Keys.FindRef(TEXT("NPCTargetname")));
		}
	}
};

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
		{ static_cast<FElysiumNpc&>(E).InputStartDialog(Args); });
	// The un-suffixed spelling, wired by 4 `scripted_sequence.OnEndSequence` rows (Chunk after the
	// gallery lockpick, Jack after each tutorial walk) and 19 more across the maps. Routed to the same
	// session opener. NOT RE-confirmed as identical to the Remote form in VtMB — the assumption is
	// that both open the NPC's `dialogname` conversation, which is all this runtime does with either.
	D.Input(TEXT("StartPlayerDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartDialog(Args); });
	D.Input(TEXT("EndDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputEndDialog(Args); });
	D.Input(TEXT("SetupPatrolType"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputSetupPatrolType(Args); });
	D.Input(TEXT("FollowPatrolPath"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputFollowPatrolPath(Args); });
	D.Input(TEXT("ClearPatrolPath"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputClearPatrolPath(Args); });

	// CAI_BaseNPC's own inputs, registered so the name resolves through the chain walk and reports
	// itself instead of dropping as an unknown input. Nothing is performed: the name, the argument
	// type and the retail handler are recovered, the semantics are not. The declaring class is named
	// as CAI_BaseNPC — the level VtMB puts them at — so one work-list row covers every `npc_*` leaf,
	// even though this runtime folds that node into each registered classname.
	using FN = FElysiumNpc;
	// STRING — 334 corpus calls, the third-largest single gap in the game
	// (`docs/vtmb/script_api.md`, datamap 0x105c9814).
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FN, SetRelationship, "9.9 — the NPC relationship model");
	// Map-fired NPC inputs the shipped content wires (`docs/vtmb/sp_tutorial_1-event-surface.md` §7):
	// 30 `TeleportToEntity`, 8 `SetScriptedDiscipline` and 4 `TakeDamage` wires across the exported
	// maps, all of them aimed at an `npc_*` receiver. `TakeDamage` is a *method* on this chain
	// (`FElysiumCombatCharacter::TakeDamage`) and a native the script surface dispatches, but the
	// wire's own datamap record — and so its argument's field type — is unrecovered, which is why the
	// input stays pending rather than forwarding a guessed number into the health track.
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FN, TeleportToEntity,      "10.7 — AI placement");
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FN, SetScriptedDiscipline, "P13 — disciplines");
	ELYSIUM_PENDING_INPUT_ON("CAI_BaseNPC", FN, TakeDamage,            "B5 — combat damage");

	AddNpcField(D, TEXT("use_interesting"), &FElysiumNpc::bUseInteresting);
	AddNpcField(D, TEXT("stattemplate"),    &FElysiumNpc::StatTemplate);
	AddNpcField(D, TEXT("interesting_place_groups"), &FElysiumNpc::InterestingPlaceGroups);
	// times_talked: santamonica/chinatown/e3/demo read `npc.times_talked` to branch first-vs-repeat
	// dialogue. Register it read-only (engine-written, script-read) so the read resolves to a defined
	// value instead of raising AttributeError. B4's dialogue runner drives the count; it stays 0 until then.
	AddNpcField(D, TEXT("times_talked"), &FElysiumNpc::TimesTalked, EElysiumField::Save);
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
