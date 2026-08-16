// 11.4 — CBaseCombatCharacter: the character sheet, its 25 datamap inputs, the damage entries and
// the gaze rig (S3).
//
// Moved verbatim out of `ElysiumPlayerClasses.cpp` when the discipline runtime landed; the file
// granularity rule in `Source/ElysiumUE/CLAUDE.md` puts one primary class per `.cpp`. The public
// declaration stays the chain header `Public/ElysiumPlayer.h`, the feed transaction stays
// `Substrate/ElysiumFeed.cpp`, the discipline transactions stay `Substrate/ElysiumDisciplines.cpp`,
// and the class registration stays at the one registration site, `ElysiumPlayerClasses.cpp`.

#include "ElysiumPlayer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U / StandViewZ — the one units conversion
#include "ElysiumSheetSlots.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"        // FElysiumDmg + the shared apply path
#include "Substrate/ElysiumDisciplines.h"    // Cycle 9 — the interruption + teardown entries
#include "Substrate/ElysiumDisposition.h"   // FElysiumEyeTargetTuning, the gaze layer's content
#include "Substrate/ElysiumGameSound.h"     // the sound-event bus + its category names
#include "Substrate/ElysiumItemClasses.h"   // FElysiumItem — Inventory_Remove's entity parameter
#include "Substrate/ElysiumLaw.h"           // Cycle 11b — FireWorldEvent, the `events_world` bus
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"

// ============================================================================================
// FElysiumCombatCharacter — CBaseCombatCharacter
// ============================================================================================

void FElysiumCombatCharacter::PendingInput(const TCHAR* Input, const TCHAR* Owner,
	const FElysiumInputArgs& Args, const TCHAR* DeclaringClass) const
{
	// Registered so the name resolves through the R2 walk, but nothing behind it yet — the same
	// condition as an unregistered classname's input, so it reports through the same surface and
	// lands in the same work list. Keyed on the class the input is declared on, not on the
	// receiver, so one row covers every NPC that receives it.
	ElysiumStub::Fired(TEXT("input"),
		FString::Printf(TEXT("%s.%s"),
			DeclaringClass ? DeclaringClass : *ElysiumCombatCharacterClassName().ToString(), Input),
		DebugString(), ElysiumStub::DescribeInput(Args), Owner);
}

void FElysiumCombatCharacter::AddMoney(int32 Delta)
{
	// `CBaseCombatCharacter::MoneyAdd` (`10340E50`) is a raw `+=` with no floor, which is what the
	// quest `AwardMoney` path reaches. `InputMoneyRemove` adds its own floor for the subtracting
	// direction; this one deliberately does not.
	if (Delta != 0) { Money += Delta; }
}

void FElysiumCombatCharacter::InputMoneyAdd(const FElysiumInputArgs& Args)
{
	AddMoney(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputMoneyRemove(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	// The floor is the runtime's, not a recovered rule: VtMB's MoneyRemove is reached through the
	// barter/quest paths that check affordability first (9.10 owns those checks).
	if (N != 0) { Money = FMath::Max(0, Money - N); }
}

const FElysiumStatTable* FElysiumCombatCharacter::SheetRules() const
{
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	if (GameState != nullptr)
	{
		return GameState->Stats();
	}
	// Cycle 9 — no GameInstance behind this world (a Substrate-tier run). The bound fallback is
	// null in a real run and in an unbound test alike, so this changes nothing that had a subsystem
	// (`Substrate/ElysiumSheetMath.h` → "The headless table binding").
	return ElysiumSheetRules::BoundTables().Stats;
}

// The rulebook this character reads its rules out of, or null in a bare world.
static UElysiumRulebookSubsystem* CharRulebook(const FElysiumCombatCharacter& Char)
{
	UElysiumGameStateSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
	return GameState ? GameState->Rulebook() : nullptr;
}

void FElysiumCombatCharacter::RebuildEffects()
{
	UElysiumRulebookSubsystem* Rules = CharRulebook(*this);
	// Cycle 9 — the headless fallback, so a Substrate-tier run resolves the same groups a live one
	// does. The subsystem always wins; the bound tables are null in a real run.
	const ElysiumSheetRules::FBoundTables& Bound = ElysiumSheetRules::BoundTables();
	const FElysiumTraitEffects* EffectTable = Rules ? &Rules->TraitEffects() : Bound.TraitEffects;
	const FElysiumFeatTable* FeatTable = Rules ? &Rules->Feats() : Bound.Feats;
	if (Effects.IsEmpty() || EffectTable == nullptr)
	{
		EffectLayer.Reset();
		RecomputeSheet();
		return;
	}
	if (!EffectLayer.IsValid())
	{
		EffectLayer = MakeShared<FElysiumSheetEffects>();
	}
	EffectLayer->Build(*EffectTable, Effects, FeatTable);
	RecomputeSheet();
}

void FElysiumCombatCharacter::RecomputeSheet()
{
	Sheet.RecomputeCurrent(SheetRules(), SheetEffects());
	SyncHealthFromSheet();
}

void FElysiumCombatCharacter::AddTrait(EElysiumTraitContainer Container, int32 Slot, int32 Delta)
{
	// AddBase carries the gate — the effective max on a gain, bypassed on a loss — and re-derives
	// every current value from the new base. The bounds are `stats.txt`'s own (Humanity 0..10,
	// Masquerade 0..5, BloodPool 0..15), tightened by whatever the clan's trait effects cap.
	Sheet.AddBase(Container, Slot, Delta, SheetRules(), SheetEffects());
}

int32 FElysiumCombatCharacter::CalcFeat(const FString& Name) const
{
	UElysiumRulebookSubsystem* Rules = CharRulebook(*this);
	// Cycle 9 — same headless fallback as the effect layer above.
	const FElysiumFeatTable* FeatTable =
		Rules ? &Rules->Feats() : ElysiumSheetRules::BoundTables().Feats;
	if (FeatTable == nullptr)
	{
		return 0;   // no rulebook: every check fails closed, as an unresolved gate does
	}
	bool bResolved = false;
	bool bIsFeat = false;
	// `this` carries the per-feat code term: the Sneaking feat's clamped `trigger_stealth_mod`
	// aggregate is added inside `FeatValue`, which is where the recovered walk puts it.
	const int32 Value = ElysiumFeats::Calc(*FeatTable, Sheet, SheetEffects(), Name,
		bResolved, bIsFeat, this);
	if (!bResolved)
	{
		// VtMB raises `AttributeError("invalid feat name -- %s")`. A raise here would abort the
		// whole conversation line, so the name is reported and the gate fails closed — the
		// error-to-false posture the rest of the scripting surface takes.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s CalcFeat(\"%s\") — no feat and no trait of that name"),
			*DebugString(), *Name);
	}
	return Value;
}

int32 FElysiumCombatCharacter::BumpStat(const FString& Stat, int32 Times)
{
	// A count below 1 does nothing — VtMB raises `"invalid args in BumpStat"`, and the loop it
	// guards cannot run backwards, so **BumpStat cannot decrement**.
	if (Stat.IsEmpty() || Times < 1)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s BumpStat(\"%s\", %d) — invalid args"),
			*DebugString(), *Stat, Times);
		return 0;
	}
	EElysiumTraitContainer Container;
	int32 Slot = INDEX_NONE;
	if (!ElysiumFindSheetSlot(*Stat, Container, Slot))
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s BumpStat(\"%s\") — no such trait"), *DebugString(), *Stat);
		return 0;
	}

	const FElysiumStatTable* Rules = SheetRules();
	int32 Landed = 0;
	for (int32 i = 0; i < Times; ++i)
	{
		// The ceiling is BumpStat's own, hardcoded and independent of the stat's authored `Max`:
		// each pass is skipped unless the BASE is under 5. A stat whose Max is higher still stops
		// here, which is why this cannot be folded into IncBase.
		if (Sheet.GetBase(Container, Slot) >= 5)
		{
			break;
		}
		if (!Sheet.IncBase(Container, Slot, Rules, SheetEffects()))
		{
			break;
		}
		++Landed;
	}
	SyncHealthFromSheet();
	// One client notification fires after the loop, not per dot (8.9 owns the readout).
	return Landed;
}

int32 FElysiumCombatCharacter::GetMasqueradeLevel() const
{
	return Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade);
}

void FElysiumCombatCharacter::AddHumanity(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}
	// One flag doubles both directions: Toreador's gift (gains) and its bane (losses) are the same
	// `Fx_Humanity_Mods_Doubled +1`, and no other shipped group sets it.
	const FElysiumSheetEffects* Layer = SheetEffects();
	const int32 Scaled = (Layer && Layer->Flag(TEXT("Fx_Humanity_Mods_Doubled")) > 0) ? Delta * 2 : Delta;
	AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity, Scaled);
}

// ==================== Cycle 11b hunk 4/9 — the Masquerade level outputs =========================
// `docs/vtmb/player-entity.md` § "Law, Masquerade and world response": "`ChangeMasqueradeLevel`
// mutates sheet stat index `0x1c`, republishes player client state and fires the game-rules output
// for the resulting level plus the generic level-changed output."
//
// The retail split is reproduced as it is authored: the datamap INPUT is
// `CBaseCombatCharacter.ChangeMasqueradeLevel` on the character, and the OUTPUTS belong to the
// world's game-rules entity. Corpus evidence over the 23 exported maps: every one of the 140
// `OnMasqueradeLevel*` rows sits on `events_world` (targetname `world`) — `OnMasqueradeLevel1..5`
// 23 rows each, `OnMasqueradeLevelChanged` 25 (la_hub_1 authors three), and every row is a Python
// callback (`OnMasqueradeLevelN()`, plus la_hub_1's `checkMasquerade()` / `fleeingHos()`) with no
// entity target. So the mutation is the producer and `events_world` is the firing entity, which is
// exactly the shape `ElysiumLaw::FireWorldEvent` already carries for the eight cop/hunter outputs.
namespace
{
	// `OnMasqueradeLevel<N>` for a resulting level of 1..5, or `NAME_None` outside that range. The
	// six authored output names themselves live in `ElysiumLaw::Outputs`, beside the rest of the
	// `events_world` surface; this is only the level-to-name selection.
	//
	// The corpus authors no `OnMasqueradeLevel0`: a change that lands the counter back on zero
	// therefore fires only the generic output, which is what the authored surface can receive. That
	// is a statement about the corpus, not a guess — every one of the 23 maps carries exactly the
	// five numbered rows.
	FName MasqueradeLevelOutput(int32 Level)
	{
		switch (Level)
		{
		case 1: return ElysiumLaw::Outputs::MasqueradeLevel1();
		case 2: return ElysiumLaw::Outputs::MasqueradeLevel2();
		case 3: return ElysiumLaw::Outputs::MasqueradeLevel3();
		case 4: return ElysiumLaw::Outputs::MasqueradeLevel4();
		case 5: return ElysiumLaw::Outputs::MasqueradeLevel5();
		default: return FName();
		}
	}
}

void FElysiumCombatCharacter::ChangeMasqueradeLevel(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}
	const int32 Before = GetMasqueradeLevel();
	AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade, Delta);
	const int32 After = GetMasqueradeLevel();

	// CHOSEN, and marked: retail's decompiled body is not recovered past "mutation fires the
	// output", so whether it fires on a delta the clamp absorbed is not stated. This runtime fires
	// once per COMMITTED change — a `+1` at the authored ceiling of 5 moves nothing, so nothing is
	// republished and nothing is fired. The alternative would re-run `checkMasquerade()`'s hunter
	// spawns on every subsequent violation at a level the player is already stuck at, which is a
	// behaviour the authored consumer visibly does not expect.
	if (After == Before)
	{
		return;
	}
	// The client republish is the presentation publisher's per-frame sample of this sheet slot
	// (`FElysiumViewState`'s vitals), so there is no push call to make here — the value moved and
	// the next publish carries it.
	if (World != nullptr)
	{
		// The level-specific output first, then the generic one: the recovered order is "the output
		// for the resulting level PLUS the generic level-changed output". The activator is the
		// character whose counter moved, so a script callback and the I/O history both attribute
		// the fire to it rather than to the world entity that carries the row.
		const FName Levelled = MasqueradeLevelOutput(After);
		if (!Levelled.IsNone())
		{
			ElysiumLaw::FireWorldEvent(*World, Levelled, Handle);
		}
		ElysiumLaw::FireWorldEvent(*World, ElysiumLaw::Outputs::MasqueradeLevelChanged(), Handle);
	}

	// "An increment whose resulting value is greater than four loads `sp_masquerade_1`. The retail
	// loss boundary is therefore a native level-5 transaction, not only a UI convention."
	// The rule is the recovered literal — resulting value past four on an INCREMENT — not the
	// stat's authored ceiling, which merely happens to be the same 5 in the shipped `stats.txt`.
	if (Delta > 0 && After > 4)
	{
		OnMasqueradeBreached();
	}
}
// ================================================================================================

void FElysiumCombatCharacter::OnMasqueradeBreached()
{
	// Only the player's masquerade ends a run; an NPC has the counter because the sheet is shared.
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s masquerade at %d"), *DebugString(), GetMasqueradeLevel());
}

void FElysiumCombatCharacter::AddBlood(int32 Delta)
{
	if (Delta != 0)
	{
		// The ceiling is `BloodPool`'s authored Max (15). The per-generation ceiling the file
		// carries as `Generation_Blood_Pool_Max` is **commented out in the shipped data**, so it is
		// not in force and nothing here consults it.
		AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, Delta);
	}
}

int32 FElysiumCombatCharacter::BloodHeal(int32 Blood)
{
	if (Blood <= 0)
	{
		return 0;
	}
	// `VampHeal_Info.VampFeedingHeal_Info` — `UsesRatio 1`, `BloodToHealthRatio 10`, i.e. one blood
	// point buys ten points of damage healed. The ratio is data; only the default is code.
	int32 Ratio = 10;
	if (UElysiumRulebookSubsystem* Rules = CharRulebook(*this))
	{
		Ratio = Rules->Rules().Int(TEXT("VampHeal_Info.VampFeedingHeal_Info"),
			TEXT("BloodToHealthRatio"), Ratio);
	}

	const int32 Spent = FMath::Min(Blood, Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool));
	if (Spent <= 0)
	{
		return 0;
	}
	AddBlood(-Spent);
	return HealDamage(Spent * FMath::Max(1, Ratio));
}

int32 FElysiumCombatCharacter::HealDamage(int32 Points)
{
	if (Points <= 0)
	{
		return 0;
	}
	// `Health` counts damage TAKEN, so healing is a subtraction from it — and a subtraction
	// bypasses the gain gate, which is exactly what makes the floor the authored `Min` of 0.
	const int32 Damage = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health);
	const int32 Healed = FMath::Min(Damage, Points);
	if (Healed <= 0)
	{
		return 0;
	}
	AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, -Healed);
	SyncHealthFromSheet();
	return Healed;
}

void FElysiumCombatCharacter::InputHumanityAdd(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddHumanity(N); }
}

void FElysiumCombatCharacter::InputChangeMasqueradeLevel(const FElysiumInputArgs& Args)
{
	ChangeMasqueradeLevel(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputBloodloss(const FElysiumInputArgs& Args)
{
	AddBlood(-Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputBloodgain(const FElysiumInputArgs& Args)
{
	AddBlood(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputBloodHeal(const FElysiumInputArgs& Args)
{
	// VtMB's internal name is BloodHealIn: the argument is the blood to spend, and the conversion
	// is the rulebook's ratio.
	BloodHeal(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputWillTalk(const FElysiumInputArgs& Args)
{
	bWillTalk = Args.Param.ToInt() != 0;
}

void FElysiumCombatCharacter::InputInventoryRemove(const FElysiumInputArgs& Args)
{
	// The recovered field type is CLASSPTR, so the parameter is an entity and nothing else. A wire
	// carrying a string cannot convert to one in Source either, so a non-handle parameter performs
	// nothing rather than being reinterpreted as a classname — that would be a contract this input
	// does not have. No shipped map fires it (0 wires game-wide), so the handle form is the whole
	// surface a runtime caller reaches.
	FElysiumEntity* Named = (World && Args.Param.IsHandle()) ? World->Resolve(Args.Param.ToHandle()) : nullptr;
	FElysiumItem* Item = Named ? Named->AsItem() : nullptr;
	if (!Item)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s Inventory_Remove — the parameter is not an item entity"),
			*DebugString());
		return;
	}
	Inventory.Detach(*this, *Item);
}

// ============================================================================================
// Gaze — the selection cascade, the saccade layer and the integrator (12.4)
// ============================================================================================

namespace
{
	// The ±30° cone every candidate is gated by, as a dot rather than an angle — retail's own
	// test is `dot(headForward, normalize(p - headPos)) > 0.866`.
	constexpr float GGazeConeDot = 0.866f;

	// Distances, in Source units converted at the one place the project converts them. The scan
	// sweeps a 300-unit sphere centred 300 units ahead of the eyes; the straight-ahead fallback is
	// 500 units out; a fidget cell is projected 25 units from the head.
	constexpr float GScanReach = 300.f * ElysiumMove::U;
	constexpr float GScanRadius = 300.f * ElysiumMove::U;
	constexpr float GAheadReach = 500.f * ElysiumMove::U;
	constexpr float GFidgetReach = 25.f * ElysiumMove::U;

	// A fidget cell is a numeric keypad seen from the character's point of view: 5 is dead ahead,
	// each column is 20° of yaw and each row 20° of pitch.
	//
	//     7 8 9      up
	//     4 5 6
	//     1 2 3      down
	//
	// Cell 0 is not a direction at all — the table's comment defines it as "fall back to normal
	// look behavior", so it is handled by the caller and never reaches here.
	FVector FidgetCellDirection(int32 Cell, const FVector& HeadForward)
	{
		const int32 Clamped = FMath::Clamp(Cell, 1, 9);
		const int32 Column = (Clamped - 1) % 3;   // 0 left, 1 centre, 2 right
		const int32 Row = (Clamped - 1) / 3;      // 0 bottom, 1 middle, 2 top
		FRotator Aim = HeadForward.Rotation();
		Aim.Yaw += static_cast<float>(Column - 1) * 20.f;
		Aim.Pitch += static_cast<float>(Row - 1) * 20.f;
		return Aim.Vector();
	}

	bool InsideGazeCone(const FVector& HeadPos, const FVector& HeadForward, const FVector& Point)
	{
		const FVector To = Point - HeadPos;
		if (To.IsNearlyZero())
		{
			return false;
		}
		return FVector::DotProduct(HeadForward, To.GetSafeNormal()) > GGazeConeDot;
	}
}

FVector FElysiumCombatCharacter::EyePosition() const
{
	// `GetAbsOrigin() + m_vecViewOffset`. The standing view offset is 64 units — the ducked 30 is
	// the player's crouched value and belongs to the player leaf, not to every character.
	return Origin + FVector(0.f, 0.f, ElysiumMove::StandViewZ);
}

void FElysiumCombatCharacter::InputLookAtEntityEye(const FElysiumInputArgs& Args)
{
	EyeLookTargetName = Args.Param.ToString();
	EyeLookMode = 1;
}

void FElysiumCombatCharacter::InputLookAtEntityCenter(const FElysiumInputArgs& Args)
{
	// Reproduced defect. Mode 2 is the one that would resolve `WorldSpaceCenter()`, but the shipped
	// handler pushes the Eye constant and no handler anywhere passes 2 — so all 10 authored
	// `LookAtEntityCenter` firings behave exactly as `LookAtEntityEye`. Kept as its own input so the
	// wire still resolves by name and so the divergence is visible here rather than implied.
	EyeLookTargetName = Args.Param.ToString();
	EyeLookMode = 1;
}

void FElysiumCombatCharacter::InputLookAtEntityOrigin(const FElysiumInputArgs& Args)
{
	EyeLookTargetName = Args.Param.ToString();
	EyeLookMode = 3;
}

void FElysiumCombatCharacter::InputLookAtEntityDefault(const FElysiumInputArgs&)
{
	// Clears the scripted target and restores autonomous behaviour. The smoothed point is left
	// where it is so the eyes glide off the old target rather than snapping.
	EyeLookTargetName.Reset();
	EyeLookMode = 0;
}

FVector FElysiumCombatCharacter::TickGaze(float Now, float DeltaSeconds,
	const FVector& HeadPos, const FVector& HeadForward, const FElysiumEyeTargetTuning& Tuning,
	const FVector* DialogPovPoint)
{
	const FVector Ahead = HeadPos + HeadForward * GAheadReach;

	// --- Selection ---------------------------------------------------------------------------
	// The priority cascade. Three of retail's arms have nothing to read yet and are marked rather
	// than faked: `enemy` needs the combat layer (P13), `navigation goal` needs a move-goal
	// accessor on FElysiumNpc, and `heard sound` needs a sound record. Each would sit here, in this
	// order, between the scripted target and the autonomous scan. Their absence makes a character
	// fall through to the scan, which is the same thing retail does when those arms find nothing.
	FVector Commanded = Ahead;
	bool bResolved = false;

	// 1. The dialogue partner, at their EyePosition() — eye height on the entity, not a head bone,
	//    so the aim holds still through the partner's animation the way retail's does.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		if (DialogOwner.IsSet())
		{
			const FElysiumEntity* Partner = nullptr;
			if (DialogOwner.Index == Handle.Index)
			{
				// This character is the one talking; its partner is the player.
				Partner = World->FindPlayer();
			}
			else if (World->PlayerHandle().Index == Handle.Index)
			{
				Partner = World->Resolve(DialogOwner);
			}
			if (Partner != nullptr && Partner != this)
			{
				// `DialogPOV` on the shot in effect redirects this arm to the camera. It replaces the
				// *player* as the subject and nothing else, so a character being looked at by the
				// player still resolves normally, and every other arm of the cascade is untouched.
				const bool bPartnerIsPlayer = World->PlayerHandle().IsSet()
					&& Partner->Handle.Index == World->PlayerHandle().Index;
				Commanded = (bPartnerIsPlayer && DialogPovPoint != nullptr)
					? *DialogPovPoint
					: Partner->EyePosition();
				bResolved = true;
			}
		}
	}

	// 2. A scripted look-at. It still passes the cone test, and falls back to straight ahead
	//    outside it; it also yields back to autonomous on its own when the entity goes away.
	if (!bResolved && EyeLookMode != 0 && !EyeLookTargetName.IsEmpty() && World != nullptr)
	{
		if (const FElysiumEntity* Scripted = World->FindByName(EyeLookTargetName))
		{
			const FVector Point = (EyeLookMode == 3) ? Scripted->Origin : Scripted->EyePosition();
			Commanded = InsideGazeCone(HeadPos, HeadForward, Point) ? Point : Ahead;
			bResolved = true;
		}
		else
		{
			EyeLookTargetName.Reset();
			EyeLookMode = 0;
		}
	}

	// 3. The autonomous scan: nearest qualifying entity inside a 300-unit sphere centred 300 units
	//    ahead of the eyes, re-picked every 1-5 seconds; nothing found means straight ahead and a
	//    retry in half a second.
	if (!bResolved)
	{
		if (Now >= NextEyeLookTime)
		{
			const FVector Centre = HeadPos + HeadForward * GScanReach;
			const FElysiumEntity* Best = nullptr;
			float BestDistanceSq = TNumericLimits<float>::Max();
			if (World != nullptr)
			{
				for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
				{
					// Retail's filter is `entity->+0x94 != 0 || (GetFlags() & FL_CLIENT)`. The first
					// half is an unrecovered field, so the recovered half stands on its own: the
					// player always qualifies, and beyond that only other characters are treated as
					// worth looking at. Widening this is a content decision, not a maths one.
					const FElysiumEntity* E = Candidate.Get();
					if (E == nullptr || E == this || E->IsInert())
					{
						continue;
					}
					const bool bIsPlayer = World->PlayerHandle().IsSet()
						&& E->Handle.Index == World->PlayerHandle().Index;
					if (!bIsPlayer && const_cast<FElysiumEntity*>(E)->AsCombatCharacter() == nullptr)
					{
						continue;
					}
					const FVector Point = E->EyePosition();
					if (FVector::DistSquared(Point, Centre) > GScanRadius * GScanRadius
						|| !InsideGazeCone(HeadPos, HeadForward, Point))
					{
						continue;
					}
					const float DistanceSq = FVector::DistSquared(Point, HeadPos);
					if (Best == nullptr || DistanceSq < BestDistanceSq
						|| (FMath::IsNearlyEqual(DistanceSq, BestDistanceSq)
							&& E->Handle.Index < Best->Handle.Index))
					{
						Best = E;
						BestDistanceSq = DistanceSq;
					}
				}
			}
			if (Best != nullptr)
			{
				Commanded = Best->EyePosition();
				NextEyeLookTime = Now + static_cast<float>(FMath::RandRange(1, 5));
				FidgetStep = -1;
			}
			else
			{
				Commanded = Ahead;
				NextEyeLookTime = Now + 0.5f;
			}
			EyeLookTarget = Commanded;
		}
		// Between re-picks the commanded point stands, so the scan does not jitter frame to frame.
		// The re-pick above always runs on the first call (NextEyeLookTime starts at zero), so this
		// is never reading an unset value.
		Commanded = EyeLookTarget;
	}

	// --- Fidget ------------------------------------------------------------------------------
	// The saccade layer engages once the eyes have actually converged — retail waits for the
	// smoothed point to come within a unit of the commanded one, so a character crossing a room
	// tracks cleanly and only starts flicking about after it has settled. It applies to whatever
	// the cascade chose, autonomous subject included; it is a layer over the aim, not a mode.
	{
		const bool bConverged = FVector::Dist(CurEyeTarget, Commanded) <= ElysiumMove::U;
		if (bConverged && FidgetStep < 0 && Now >= NextFidgetTime)
		{
			FidgetStep = 0;
			NextFidgetTime = Now + FMath::FRandRange(Tuning.HoldMin, Tuning.HoldMax);
			FidgetCell = Tuning.FidgetPoints[0] < 0 ? FMath::RandRange(1, 9) : Tuning.FidgetPoints[0];
		}
		else if (FidgetStep >= 0 && Now >= NextFidgetTime)
		{
			++FidgetStep;
			if (FidgetStep > 2)
			{
				// Sequence exhausted: back to the default direction and hold for the disposition's
				// own interval before the next one.
				FidgetStep = -1;
				NextFidgetTime = Now + FMath::FRandRange(Tuning.MinInterval, Tuning.MaxInterval);
			}
			else
			{
				NextFidgetTime = Now + FMath::FRandRange(Tuning.HoldMin, Tuning.HoldMax);
				const int32 Authored = Tuning.FidgetPoints[FidgetStep];
				FidgetCell = Authored < 0 ? FMath::RandRange(1, 9) : Authored;
			}
		}
		// Cell 0 means "fall back to normal look behavior", so it leaves the commanded point alone.
		if (FidgetStep >= 0 && FidgetCell > 0)
		{
			Commanded = HeadPos + FidgetCellDirection(FidgetCell, HeadForward) * GFidgetReach;
		}
	}

	EyeLookTarget = Commanded;

	// --- Integration -------------------------------------------------------------------------
	// A fixed-timestep lerp, not a rate: per 0.1 s of accumulated interval,
	// `m_vCurEyeTarget += rate × (m_vEyeLookTarget − m_vCurEyeTarget)`. Reproducing the fixed step
	// matters — folding the rate into a per-frame lerp would make the convergence speed depend on
	// frame rate, which is exactly what the accumulator exists to avoid.
	EyeIntegRate = Tuning.TurnRate;
	if (!bCurEyeTargetSeeded)
	{
		CurEyeTarget = Commanded;
		bCurEyeTargetSeeded = true;
	}
	EyeIntegAccumulator += DeltaSeconds;
	int32 Steps = 0;
	while (EyeIntegAccumulator >= 0.1f && Steps < 16)
	{
		EyeIntegAccumulator -= 0.1f;
		CurEyeTarget += (EyeLookTarget - CurEyeTarget) * EyeIntegRate;
		++Steps;
	}
	if (Steps >= 16)
	{
		// A long hitch would otherwise spin this loop; land on the target and drop the backlog.
		CurEyeTarget = EyeLookTarget;
		EyeIntegAccumulator = 0.f;
	}

	// --- Head turn, which drives nothing -------------------------------------------------------
	// Retail integrates m_flHeadYaw/m_flHeadPitch every think through this 0.8/0.2 filter and
	// applies them with SetBoneController(0, …) and (1, …) — bone controllers, not pose parameters.
	// No shipped model declares a single bone controller, so the lookup fails and the value never
	// reaches the skeleton. Visible head movement in VtMB dialogue is animation and choreography,
	// not this path. Reproduced, including the unclamped filter and its lone `> 360 → 0` guard, so
	// the state is inspectable and so nobody later mistakes its absence for a missing feature.
	const FRotator ToTarget = (EyeLookTarget - HeadPos).Rotation();
	const FRotator HeadNow = HeadForward.Rotation();
	HeadYaw = HeadYaw * 0.8f + (ToTarget.Yaw - HeadNow.Yaw) * 0.2f;
	HeadPitch = HeadPitch * 0.8f + (ToTarget.Pitch - HeadNow.Pitch) * 0.2f;
	if (HeadYaw > 360.f) { HeadYaw = 0.f; }
	if (HeadPitch > 360.f) { HeadPitch = 0.f; }

	return CurEyeTarget;
}

void FElysiumCombatCharacter::SyncHealthFromSheet()
{
	// `CBaseCombatCharacter::HealthToPercent` projects the sheet pair onto Source's engine-space
	// health (`docs/vtmb/game_runtime.md` section 3). Our `health` / `max_health` keyfields ARE that engine
	// space — what the save walk enumerates, what a `.ents` `health` key writes, and what the body
	// reads — so they are derived, never the truth.
	MaxHealth = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth);
	const int32 Damage = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health);
	Health = FMath::Max(0, MaxHealth - Damage);
}

bool FElysiumCombatCharacter::IsKindred() const
{
	// The base answer is the sheet's own clan slot: a character carrying one of the seven playable
	// clans is Kindred, and everything else is mortal. This is the player's real classification —
	// `clandoc000.txt` gives every player template a clan — and the fallback for an NPC with no
	// `stattemplate`, whose authored `Kindred` key the NPC leaf reads instead.
	return FElysiumSheet::IsValidClan(Sheet.Clan());
}

void FElysiumCombatCharacter::TakeDamage(const FElysiumDmg& Dmg, FElysiumCombatCharacter* Attacker,
	bool bDisallowFirearmsToBashing)
{
	if (IsInert() || HasReportedDeath())
	{
		return;
	}
	// B6 — incoming damage while paired tears the feed down BEFORE the damage commits, whichever
	// half of the pair is hit (`docs/vtmb/feeding.md` § "Interruption, completion and outputs").
	BreakFeed();

	FElysiumDmg Resolved = Dmg;
	if (!ElysiumDamage::Apply(Resolved, Attacker, *this, FElysiumDamageContext::FromCharacter(*this),
		bDisallowFirearmsToBashing))
	{
		return;   // Apply reported why
	}
	CommitDamage(Resolved);
}

void FElysiumCombatCharacter::TakeDamage(float Amount)
{
	if (Amount <= 0.f || IsInert())
	{
		return;
	}
	BreakFeed();

	// The scalar fallback: retail's alive path takes its positive damage EITHER from the descriptor
	// apply callback or from here, so this route does not enter the resolver at all. The descriptor
	// exists so the commit has one shape to spend: direct input, no mask (hence no aggravated
	// tracking and no soak bypass), and a forced soak of zero, which is what "the number as given"
	// means in descriptor terms.
	FElysiumDmg Dmg;
	Dmg.Family = EElysiumDmgFamily::Bashing;
	Dmg.Flags = ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = FMath::TruncToInt(Amount);
	Dmg.ForcedSoak = 0;
	Dmg.RolledSuccesses = Dmg.ExtraInput;
	Dmg.Remainder = Dmg.ExtraInput;
	Dmg.AppliedDamage = Dmg.ExtraInput;
	Dmg.bResolved = true;
	CommitDamage(Dmg);
}

void FElysiumCombatCharacter::CommitDamage(const FElysiumDmg& Dmg)
{
	using EC = EElysiumTraitContainer;

	int32 Remaining = Dmg.CommittedDamage();
	if (Remaining <= 0)
	{
		return;
	}
	if (MaxHealth <= 0)
	{
		// No health track: the rulebook did not load, or the character was built without a sheet.
		// Damage is recorded rather than applied — a character with no health model must not die of
		// arithmetic.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s took %d damage with no health track"),
			*DebugString(), Remaining);
		return;
	}

	// 1. HealthBuffer absorbs first. Exhausting it clears the counter and ends Bloodshield, which
	//    is the discipline that filled it; a partial absorption only reduces it.
	const int32 Buffer = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::HealthBuffer);
	if (Buffer > 0)
	{
		const int32 Absorbed = FMath::Min(Buffer, Remaining);
		Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthBuffer, Buffer - Absorbed);
		Remaining -= Absorbed;
		if (Buffer - Absorbed <= 0)
		{
			EndBloodshield();
		}
		RecomputeSheet();
	}

	if (Remaining > 0)
	{
		const int32 Taken = Sheet.GetBase(EC::Attributes, ElysiumSlot::Health);
		// 2. Unkillable caps the damage-TAKEN counter at the retail literal. It is not a one-hit-
		//    point floor and not a percentage: with the default Max_Health of 100 it leaves 25.
		const int32 Cap = bUnkillable ? ElysiumDamage::UnkillableDamageCap : MaxHealth;
		// 3. The remainder lands on the damage counter. The authored ceiling is `Max_Health`, which
		//    the sheet's own clamp applies whenever the rules table is loaded; the clamp here keeps
		//    a bare (rulebook-less) world reading the same numbers.
		const int32 Committed = FMath::Clamp(Taken + Remaining, 0, FMath::Max(Cap, 0));
		Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, Committed);

		// 4. A Kindred victim also accumulates aggravated damage for the mask that takes no soak.
		if (IsKindred() && Dmg.TakesNoSoak())
		{
			const int32 Aggravated = Sheet.GetBase(EC::Attributes, ElysiumSlot::HealthAggDmg);
			Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthAggDmg,
				Aggravated + (Committed - Taken));
		}
		// 5. `HealthToPercent` — the sheet pair projected back onto the engine-space keyfields.
		RecomputeSheet();
	}

	// `NPC_TAKE_DAMAGE`, from its real producer: the noise a body makes when it is hit, emitted only
	// once damage has actually landed on this character. The two early returns above — nothing to
	// commit, and no health track — make no sound, which is right: neither is a hit.
	if (World != nullptr)
	{
		World->EmitGameSound(Origin, ElysiumGameSounds::NpcTakeDamage(),
			/*RadiusCm, table-resolved*/ -1.f, Handle,
			ElysiumStealth::HearingReductionCmFor(this));
	}

	// The senses/memory record the schedule kernel reads. A no-op on the base.
	OnDamageCommitted(Dmg);

	// Cycle 9 — `ShouldRemove_OnTakeDamage`, from the one typed health commit. It also reconciles
	// the Bloodshield teardown above: `EndBloodshield` drops the power's trait group when the buffer
	// exhausts, and this is where the tracked targeted effect that installed it is retired with it.
	ElysiumDisciplines::NotifyDamaged(*this);

	// 6. The outputs, from their real producer. Retail fires them from the NPC alive commit and the
	//    player wires neither, but FireOutput is inert for an output an entity did not wire, so the
	//    shared commit is where they belong. OnHalfHealth is OFFERED on every damaging hit while the
	//    projected health sits at or below half, not only on the crossing edge.
	static const FName OnDamaged(TEXT("OnDamaged"));
	static const FName OnHalfHealth(TEXT("OnHalfHealth"));
	FireOutput(OnDamaged, Dmg.Source);
	if (MaxHealth > 0 && Health * 2 <= MaxHealth)
	{
		FireOutput(OnHalfHealth, Dmg.Source);
	}

	// 7. Death is the RPG comparison, not the engine-space projection: the damage counter reaching
	//    the ceiling is what selects it.
	const int32 Taken = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	const int32 Ceiling = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);
	if (!bUnkillable && Ceiling > 0 && Taken >= Ceiling)
	{
		OnKilled();
	}
}

void FElysiumCombatCharacter::EndBloodshield()
{
	// The exhausted buffer ends the power that filled it. Two spellings name the same power in the
	// shipped data — the discipline's own InternalName and the trait-effect group the discipline
	// installs — and the effect list can legitimately carry either, so both are removed.
	static const TCHAR* const Names[] =
	{
		TEXT("Thaumaturgy_Bloodshield"),
		TEXT("Discipline (Thaumaturgy-Bloodshield)"),
	};
	int32 Removed = 0;
	for (const TCHAR* Name : Names)
	{
		Removed += Effects.RemoveAll([Name](const FString& Entry)
			{ return Entry.Equals(Name, ESearchCase::IgnoreCase); });
	}
	if (Removed > 0)
	{
		RebuildEffects();
	}
}

void FElysiumCombatCharacter::OnKilled()
{
	if (bDeathReported)
	{
		return;
	}
	bDeathReported = true;
	static const FName OnDeath(TEXT("OnDeath"));
	FireOutput(OnDeath, Handle);   // one of CAI_BaseNPC's 16 outputs; the player wires none
	// Preserve producer order: the child's own OnDeath rows enter the queue before its maker's
	// OnNPCDied rows. Retail's relative order is still an open live-capture question; this is the
	// existing producer first, followed by the newly recovered owner notification.
	NotifyOwnerOfTermination(EElysiumOwnedEntityTermination::Died);
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s died"), *DebugString());
}

bool FElysiumCombatCharacter::GetDynamicField(FName Name, FElysiumVariant& Out) const
{
	// Every compiled slot is a registered field, so the R2 walk has already answered by the time
	// this runs. What is left is a `base_*` name the shipped `stats.txt` does not own — which still
	// reads 0 rather than raising, the same default-on-miss `G` has, because the gates that ask
	// (`pc.base_Celerity > 0`) are written against a sheet where every name resolves.
	if (const int32* V = Sheet.Extra.Find(Name))
	{
		Out = FElysiumVariant::Int(*V);
		return true;
	}
	if (Name.ToString().StartsWith(TEXT("base_"), ESearchCase::CaseSensitive))
	{
		// Reads 0 rather than raising (see the header), but a name that lands here is a name the
		// shipped `stats.txt` does not own — either a slot we have not built or a script's
		// misspelling of one we have. Both are silent divergences, so both get reported: the read
		// still answers, and the tally says which names answered on nothing.
		ElysiumStub::Fired(TEXT("field"),
			FString::Printf(TEXT("%s.%s"), *ElysiumCombatCharacterClassName().ToString(), *Name.ToString()),
			DebugString(), FString(),
			TEXT("no compiled sheet slot owns this name — reads 0"));
		Out = FElysiumVariant::Int(0);
		return true;
	}
	return false;
}

bool FElysiumCombatCharacter::SetDynamicField(FName Name, const FElysiumVariant& Value)
{
	if (Sheet.Extra.Contains(Name) || Name.ToString().StartsWith(TEXT("base_"), ESearchCase::CaseSensitive))
	{
		Sheet.Extra.Add(Name, Value.ToInt());
		return true;
	}
	return false;
}

void FElysiumCombatCharacter::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	using EC = EElysiumTraitContainer;
	Out.Emplace(TEXT("Clan"), FString::Printf(TEXT("%d (%s), %s"), Sheet.Clan(),
		FElysiumSheet::ClanName(Sheet.Clan()), Sheet.IsMale() ? TEXT("male") : TEXT("female")));
	Out.Emplace(TEXT("Health"), FString::Printf(TEXT("%d / %d%s"), Health, MaxHealth,
		bUnkillable ? TEXT("  (unkillable)") : TEXT("")));
	Out.Emplace(TEXT("Money"), FString::FromInt(Money));
	Out.Emplace(TEXT("Blood"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::BloodPool)));
	Out.Emplace(TEXT("Humanity"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity)));
	Out.Emplace(TEXT("Masquerade"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Masquerade)));
	Out.Emplace(TEXT("Physical"), FString::Printf(TEXT("str %d  dex %d  sta %d"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Strength),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Dexterity),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Stamina)));
	Out.Emplace(TEXT("Effects"), Effects.IsEmpty()
		? FString(TEXT("(none)"))
		: FString::Printf(TEXT("%s  (%d rows)"), *FString::Join(Effects, TEXT(", ")),
			EffectLayer.IsValid() ? EffectLayer->NumRows() : 0));
	Out.Emplace(TEXT("WillTalk"), bWillTalk ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Disposition"), Disposition.IsEmpty() ? TEXT("(none)") : Disposition);
	// Cycle 9 — the discipline block: which of the thirteen are active, and how many targeted
	// effects this character is currently carrying.
	{
		TArray<FString> Active;
		for (int32 i = 0; i < FElysiumDisciplineState::SlotCount; ++i)
		{
			if (Disciplines.IsActive(i))
			{
				Active.Add(FString::Printf(TEXT("%s %d"), ElysiumDisciplines::InternalName(i),
					Sheet.GetCurrent(EC::ActiveDisciplines, i)));
			}
		}
		Out.Emplace(TEXT("Disciplines"), FString::Printf(TEXT("%s  (%d tracked effect(s))"),
			Active.IsEmpty() ? TEXT("(none active)") : *FString::Join(Active, TEXT(", ")),
			Disciplines.TargetEffects.Num()));
	}
	Out.Emplace(TEXT("Unnamed stats"), FString::FromInt(Sheet.Extra.Num()));
}
