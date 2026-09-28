// CBaseCombatCharacter: the character sheet, its 25 datamap inputs, the damage entries and
// the gaze rig.
//
// The public declaration stays the chain header `Public/ElysiumPlayer.h`, the feed transaction stays
// `Substrate/ElysiumFeed.cpp`, the discipline transactions stay `Substrate/ElysiumDisciplines.cpp`,
// and the class registration stays at the one registration site, `ElysiumPlayerClasses.cpp`.

#include "ElysiumPlayer.h"

#include "ElysiumAnimationIntent.h"    // FElysiumActivityClipRequest — the activity seam's context
#include "ElysiumCameraSolve.h"        // ElysiumCam::CameraClass — the equipped camera publication
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U / StandViewZ — the one units conversion
#include "ElysiumRng.h"                // the session's owned random streams
#include "ElysiumSheetSlots.h"
#include "ElysiumSkeletalBasis.h"      // FromSourceAngles — the entity's facing as an Unreal yaw
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumAnimEvents.h"    // the recovered dispatch bands
#include "Substrate/ElysiumDamage.h"        // FElysiumDmg + the shared apply path
#include "Substrate/ElysiumDisciplines.h"    // the interruption + teardown entries
#include "Substrate/ElysiumDisposition.h"   // FElysiumEyeTargetTuning, the gaze layer's content
#include "Substrate/ElysiumFeed.h"          // EventFeedBegin/EventFeedTeardown — the two ids 0x1032e330 guards
#include "Substrate/ElysiumGameSound.h"     // the sound-event bus + its category names
#include "Substrate/ElysiumItemClasses.h"   // FElysiumItem — Inventory_Remove's parameter, the equipped item's record
#include "Substrate/ElysiumItemTable.h"     // FElysiumItemDef — the equipped item's definition record
#include "Substrate/ElysiumLaw.h"           // FireWorldEvent, the `events_world` bus
#include "Substrate/ElysiumNpc.h"           // FElysiumNpcBase::GetMind — the cast body's own state
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumPlayerLog.h"
#include "Substrate/ElysiumReactions.h"     // the damage flinch's pure rules
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealth.h"
#include "Substrate/ElysiumWeaponClasses.h"  // FElysiumWeapon — the operator hop the weapon band takes

bool FElysiumCombatCharacter::IsObfuscatedForSenses() const
{
	return Sheet.GetCurrent(EElysiumTraitContainer::ActiveDisciplines, 8) > 0
		&& Disciplines.bObfuscateCloaked;
}

bool FElysiumCombatCharacter::CanPerceiveConcealment(const FElysiumCombatCharacter& Candidate) const
{
	// FUN_10146b20(target, observer): these two observer stats bypass concealment.
	if (!Candidate.IsObfuscatedForSenses()
		|| Sheet.GetCurrent(EElysiumTraitContainer::ActiveDisciplines, 0xe) > 0
		|| Sheet.GetCurrent(EElysiumTraitContainer::ActiveDisciplines, 1) > 0)
	{
		return true;
	}
	return Disciplines.bObfuscateDetectionReady
		&& (Disciplines.ObfuscateDetectionRadiusUnits == 0.f
			|| FVector::Dist(Origin, Candidate.Origin) <= Disciplines.ObfuscateDetectionRadiusUnits * 2.54f);
}

bool FElysiumCombatCharacter::HasDisciplineStatus(const FString& Record) const
{
	return Disciplines.TargetEffects.ContainsByPredicate([&Record](const FElysiumActiveDisciplineEffect& Effect)
	{
		return Effect.Record.Equals(Record, ESearchCase::IgnoreCase);
	});
}

// --- FElysiumCombatCharacter — CBaseCombatCharacter ---

void FElysiumCombatCharacter::PendingInput(const TCHAR* Input, const TCHAR* Owner,
	const FElysiumInputArgs& Args, const TCHAR* DeclaringClass) const
{
	// Registered so the name resolves through the class-chain walk, but nothing behind it — the same
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
	// barter/quest paths that check affordability first.
	if (N != 0) { Money = FMath::Max(0, Money - N); }
}

const FElysiumStatTable* FElysiumCombatCharacter::SheetRules() const
{
	UElysiumSessionSubsystem* GameState = World ? World->GetGameState() : nullptr;
	if (GameState != nullptr)
	{
		return GameState->Stats();
	}
	// No GameInstance behind this world (a Substrate-tier run). The bound fallback is
	// null in a real run and in an unbound test alike, so this changes nothing that had a subsystem
	// (`Substrate/ElysiumSheetMath.h` → "The headless table binding").
	return ElysiumSheetRules::BoundTables().Stats;
}

// The rulebook this character reads its rules out of, or null in a bare world.
static UElysiumRulebookSubsystem* CharRulebook(const FElysiumCombatCharacter& Char)
{
	UElysiumSessionSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
	return GameState ? GameState->Rulebook() : nullptr;
}

void FElysiumCombatCharacter::RebuildEffects()
{
	UElysiumRulebookSubsystem* Rules = CharRulebook(*this);
	// The headless fallback, so a Substrate-tier run resolves the same groups a live one
	// does. The subsystem always wins; the bound tables are null in a real run.
	const ElysiumSheetRules::FBoundTables& Bound = ElysiumSheetRules::BoundTables();
	const FElysiumTraitEffects* EffectTable = Rules ? &Rules->TraitEffects() : Bound.TraitEffects;
	const FElysiumFeatTable* FeatTable = Rules ? &Rules->Feats() : Bound.Feats;
	if (Effects.IsEmpty() || EffectTable == nullptr)
	{
		EffectLayer.Reset();
		RecomputeSheet();
		Inventory.WieldUpdate(*this);
		return;
	}
	if (!EffectLayer.IsValid())
	{
		EffectLayer = MakeShared<FElysiumSheetEffects>();
	}
	// `Excluded_Equipment` is one of the traits a group writes (`"Value Clawed_Form"` on both
	// Protean groups), and its names are `system/items.txt` rows rather than a `strings.txt` group —
	// so the wield table is passed in beside the stat and string tables.
	// The stat table comes with it because the resolver keys on the stat's authored `NameFunc`;
	// `strings.txt` is deliberately still omitted, so the order enums stay unresolved here exactly
	// as they were — chargen is what owns those, and this is not the place to start writing them.
	EffectLayer->Build(*EffectTable, Effects, FeatTable, Rules ? &Rules->Stats() : Bound.Stats,
		/*Strings*/ nullptr, Rules ? &Rules->ExcludedEquip() : Bound.ExcludedEquip);
	RecomputeSheet();
	// The tail retail's trait-effect apply (0x101f8620) and remove (0x101f8f30) both run:
	// `Inventory_Wield_Update` (0x10335b80). This is the one place the port's effect list changes,
	// so it is the one place the sweep belongs — a discipline that installs `Clawed_Form` takes the
	// held weapon away here, and ending it gives one back.
	Inventory.WieldUpdate(*this);
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
	// Same headless fallback as the effect layer above.
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
	// One client notification fires after the loop, not per dot (the HUD owns the readout).
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

// --- The Masquerade level outputs ---
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

int32 FElysiumCombatCharacter::BloodPoolCapacity() const
{
	// The same bound `AddTrait`/`IncBase` above gate on, asked for directly. `BoundsFor` is the port
	// of the engine's own bound walk: the stat definition's `Min`/`Max`, a bound authored as another
	// stat's NAME resolved against the base array, then the character's trait-effect layer applied
	// to the bound itself. `BloodPool` authors the literal 15 and no clan effect touches it, so the
	// shipped rulebook answers 15 for every character.
	int32 Min = 0;
	int32 Max = 0;
	Sheet.BoundsFor(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool,
		SheetRules(), SheetEffects(), Min, Max);
	// `Max < Min` is `BoundsFor`'s "unbounded" answer, which only a world with no `stats.txt` in
	// reach gives. There is no pool to draw then, so the honest capacity is nothing.
	return Max >= Min ? Max : 0;
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

void FElysiumCombatCharacter::PublishEquippedCameraClass() const
{
	// **Player only.** An NPC drawing a katana does not force the player's camera to third person;
	// the arbitration is a property of the local player's equipped item and of nothing else.
	if (!World || !(World->PlayerHandle() == Handle))
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;   // a headless logic world has no camera to arbitrate
	}

	// An empty hand, an item whose record the rulebook has not loaded, and an authored `noswitch` all
	// resolve to 0, which is retail's own early-out in `ApplyWeaponCameraPref`.
	int32 CameraClass = ElysiumCam::CameraClass::None;
	if (FElysiumEntity* Ent = const_cast<FElysiumEntityWorld*>(World)->Resolve(Inventory.ActiveWeapon))
	{
		// `m_hActiveWeapon` is a script-writable handle, so what it names is not guaranteed to be an
		// item. Checked, not assumed: a blind downcast would read a `camera_class` off whatever object
		// a level script assigned and arbitrate the player's camera from it.
		const FElysiumItem* Item = Ent->AsItem();
		if (!Item)
		{
			UE_LOG(LogElysiumItem, Warning,
				TEXT("m_hActiveWeapon on %s names %s, which is not an item; the equipped camera class "
				     "resolves to none"),
				*World->DescribeHandle(Handle), *World->DescribeHandle(Inventory.ActiveWeapon));
		}
		else if (const FElysiumItemDef* Record = Item->Data())
		{
			CameraClass = Record->CameraClass;
		}
	}
	Embodiment->SetEquippedCameraClass(CameraClass);
}

void FElysiumCombatCharacter::FillActivityClipRequest(FElysiumActivityClipRequest& Request) const
{
	Request.Stem = ModelStem();

	// The classname a map AUTHORS, which is the key the recovered class bodies are joined to. The
	// registered descriptor answers for a character no def produced, which is the player's case.
	if (Def != nullptr)
	{
		Request.ActorClassname = Def->Classname;
	}
	else if (Class != nullptr)
	{
		Request.ActorClassname = Class->ClassName.ToString();
	}
	else
	{
		Request.ActorClassname.Reset();
	}

	// **A HIDDEN active weapon presents as NO weapon to the whole activity request.**
	//
	// Retail gates the weapon on `m_fEffects & EF_NODRAW` (+0x19c & 0x40) in both places the active
	// weapon can reach an activity:
	//
	//  * `CBaseCombatCharacter::Weapon_TranslateActivity` (vampire.dll 0x10327ec0) calls the
	//    weapon's `ActivityOverride` (`+0x5a4`, 0x1024f210) only when the bit is CLEAR;
	//  * the human/vampire pre-translator `PreTranslate_Human` (0x103854f0, virtual `+0x5dc` for
	//    `CNPC_VHuman`, `CNPC_VVampire` and 37 more classes) takes the same gate a rung earlier: a
	//    NODRAW weapon leaves `m_bAggressiveAnims` at 0 and the request goes to the Troika body
	//    unchanged, so no `ACT_WALK` -> `ACT_WALK_RELAXED` rewrite happens either.
	//
	// Both gates say the same thing, so the port states it ONCE, here, where the whole translation
	// context is built: an empty `WeaponClassname` is exactly "no weapon", and it reaches the class
	// ladder, the weapon ladder, the alert/relaxed branch and the gait fan together. Gating only one
	// of them downstream is how a body comes to walk a relaxed gait while posing an unarmed stand.
	//
	// The weapon is still `m_hActiveWeapon` — `Inventory.Active` keeps answering with it, the
	// magazine and the deadlines are untouched, and a press still fires it. Only the ANIMATION
	// question sees no weapon, which is retail's own shape.
	const FElysiumItem* Active = Inventory.Active(*this);
	const FElysiumWeapon* ActiveWeapon = Active != nullptr
		? const_cast<FElysiumItem*>(Active)->AsWeapon() : nullptr;
	const bool bDrawn = ActiveWeapon == nullptr || !ActiveWeapon->IsHidden();
	Request.WeaponClassname = (bDrawn && Active != nullptr && Active->Def != nullptr)
		? Active->Def->Classname : FString();

	// A character with no mind is not a cast member and has no state to read; idle is what the
	// recovered tree answers for every state that is neither alert nor combat, so it is the honest
	// default rather than a placeholder.
	const FElysiumNpc* Npc = AsNpc();
	Request.ActorState = Npc != nullptr ? Npc->GetMind().State() : EElysiumNpcState::Idle;

	// The direction-keyed selection's own input, and it is the PLAYER's alone: retail's masked
	// selector reads `CBasePlayer`'s current button field, and a cast body has no such field to read.
	// `INDEX_NONE` says so rather than handing an NPC an empty button state, which would read as "no
	// direction held" and select the neutral-mask attack outright.
	Request.StateMask = (World != nullptr && World->PlayerHandle() == Handle)
		? World->PlayerSelectionStateMask() : INDEX_NONE;
}

// --- Gaze — the selection cascade, the saccade layer and the integrator ---

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

// === SC3 — the camera-override source and the four SetAsCameraTarget inputs ===================
// `docs/vtmb/camera-view-modes.md` §"The `camera_track` override channel"; recovery RC7.

FVector FElysiumCombatCharacter::GetCameraViewpointPosition() const
{
	// Slot 50, `CBaseCombatCharacter::GetCameraViewpointPosition` `0x10332010` -> `CalcLookData(&out,
	// NULL)`. Unconditional — the head flag does not reach this one. The port's stand-in for the
	// look point is `EyePosition()`, which is the same fixed view offset the gaze cascade aims at.
	return EyePosition();
}

FVector FElysiumCombatCharacter::GetCameraTargetPosition(const FVector& /*AimFrom*/) const
{
	// Slot 51, `0x103320b0`. Head (`m_bCameraTargetIsHead`) -> `CalcLookData`; body ->
	// `WorldSpaceCenter()` (vfunc `0x300`), the collision OBB's own centre.
	if (bCameraTargetIsHead)
	{
		return EyePosition();
	}
	FBox BodyBounds(ForceInit);
	if (IElysiumEmbodiment* Bodily = World ? World->Embodiment() : nullptr)
	{
		if (Bodily->GetUseBodyWorldBounds(Handle, BodyBounds) && BodyBounds.IsValid != 0)
		{
			return BodyBounds.GetCenter();
		}
	}
	// No rendered body: retail's `WorldSpaceCenter()` is a collision-bounds midpoint a headless
	// world does not have, so the standing view offset's midpoint stands in for it. Same posture
	// `ElysiumTerminal.cpp` takes for the same missing primitive.
	return Origin + FVector(0.f, 0.f, ElysiumMove::StandViewZ * 0.5f);
}

bool FElysiumCombatCharacter::IsCameraSourceAlive() const
{
	// Retail's test is the EHANDLE's own validity, which a killed entity fails.
	return !IsDead();
}

void FElysiumCombatCharacter::SetAsCameraTarget(bool bHead, float FadeTime)
{
	// `0x1000a2d6` / `0x103322e0`, verbatim: write the two fields, then walk every player index and
	// push this entity onto that player's TARGET channel with a crossfade argument of **0.0**. The
	// entity's own `GetCameraFadeInTime()` — i.e. the field just written — is what raises it, which
	// is why `FadeHeadAsCameraTarget 0.5` and `SetHeadAsCameraTarget` differ at all.
	//
	// Setting the target does NOT cancel a live cine shot; only the view setter does.
	bCameraTargetIsHead = bHead;
	CameraOverrideFadeTime = FadeTime;
	if (World != nullptr)
	{
		World->SetCameraOverrideTarget(Handle, 0.0f);
	}
}

void FElysiumCombatCharacter::InputSetHeadAsCameraTarget(const FElysiumInputArgs&)
{
	SetAsCameraTarget(/*bHead*/ true, 0.f);     // `0x10332570` — SetAsCameraTarget(1, 0)
}

void FElysiumCombatCharacter::InputSetBodyAsCameraTarget(const FElysiumInputArgs&)
{
	SetAsCameraTarget(/*bHead*/ false, 0.f);    // `0x10332610` — SetAsCameraTarget(0, 0)
}

namespace
{
	// `inputdata.value.fieldType == 1` (FIELD_FLOAT) else 0 — `0x103323d0` / `0x103324a0`. The
	// datamap declares both Fade inputs FLOAT (`docs/vtmb/script_api.md`), so `AcceptInput`'s own
	// convert has already run by the time the guard is read and a numeric wire passes it; a Void
	// firing is what the guard actually rejects.
	float CameraTargetFadeArg(const FElysiumInputArgs& Args)
	{
		return Args.Param.IsVoid() ? 0.f : Args.Param.ToFloat();
	}
}

void FElysiumCombatCharacter::InputFadeHeadAsCameraTarget(const FElysiumInputArgs& Args)
{
	SetAsCameraTarget(/*bHead*/ true, CameraTargetFadeArg(Args));
}

void FElysiumCombatCharacter::InputFadeBodyAsCameraTarget(const FElysiumInputArgs& Args)
{
	SetAsCameraTarget(/*bHead*/ false, CameraTargetFadeArg(Args));
}
// === end SC3 block ============================================================================

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

FVector FElysiumCombatCharacter::BodyDirection2D() const
{
	return FRotator(0.f, ElysiumSkeletalBasis::FromSourceAngles(Angles).Yaw, 0.f).Vector();
}

FVector FElysiumCombatCharacter::TickGaze(float Now, float DeltaSeconds,
	const FVector& HeadPos, const FVector& HeadForward, const FElysiumEyeTargetTuning& Tuning,
	const FVector* DialogPovPoint)
{
	// `CAI_BaseNPC`'s eye maintainer at slot 333 (`0x1026b810`), the body every VtMB NPC class
	// reaches through `CAI_BaseNPCTroika`'s wrapper (`0x102bff20`: the blink cadence, the
	// disposition fidget driver, then this). The blink lives in the eye pass and the fidget
	// driver is the saccade layer below; the selection, the subject tracking, the direct arms
	// and the integrator are here, in retail's order.
	const FVector Ahead = HeadPos + HeadForward * GAheadReach;

	// The integrator's rate is not one number. Retail's fidget driver (`0x102c0010`) rewrites
	// `m_flEyeIntegRate`@0x0E3C every think from the disposition table: the branch that holds a
	// converged gaze reads index 0 and the branch that steps to the next fidget cell reads index 1
	// (`FUN_100ecdf0`, record fields +0x23C and +0x260 — see FElysiumEyeTargetTuning). So the eyes
	// glide between fidget cells at the disposition's own rate and snap back at the global one.
	auto RateNow = [&Tuning](int32 Step) { return Step >= 0 ? Tuning.StepRate : Tuning.HoldRate; };

	// The tail both the scripted maintainer and the cascade end in: commit the commanded point,
	// then advance the fixed-timestep lerp. Not a rate — per 0.1 s of accumulated interval,
	// `m_vCurEyeTarget += rate × (m_vEyeLookTarget − m_vCurEyeTarget)`. Reproducing the fixed step
	// matters: folding the rate into a per-frame lerp would make the convergence speed depend on
	// frame rate, which is exactly what the accumulator exists to avoid.
	auto Integrate = [this, DeltaSeconds](const FVector& Commanded, float Rate)
	{
		EyeLookTarget = Commanded;
		EyeIntegRate = Rate;
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
	};

	// --- 0. A scripted look-at REPLACES the cascade -------------------------------------------
	// `CBaseCombatCharacter::UpdateCharacter` (`0x103246d0`) branches before anything else:
	// `if (m_scriptedEyeMode@0x0E68 == 0) slot333 MaintainEyeDirection(); else
	// MaintainScriptedEyeDirection(dt)`. The scripted maintainer (`0x10325620`) is a whole
	// alternative body — no dialogue partner, no enemy, no scan and, at the tail, no fidget: the
	// early-out at `0x1026b81b` that lets `m_RelativeEyeTarget`@0x5B94 own the aim belongs to the
	// autonomous maintainer, which is not running at all. It also never calls `SetHeadDirection`
	// (slot 0x864), so the head filter is not advanced either; only slot 0x454 gets the smoothed
	// point.
	if (EyeLookMode != 0)
	{
		const FElysiumEntity* Scripted = (World != nullptr && !EyeLookTargetName.IsEmpty())
			? World->FindByName(EyeLookTargetName) : nullptr;
		if (Scripted == nullptr)
		{
			// `m_hEyeLookTarget` no longer resolves: retail clears the mode and returns *without*
			// touching the eyes, so the cascade takes over on the next think rather than this one.
			EyeLookTargetName.Reset();
			EyeLookMode = 0;
			return CurEyeTarget;
		}
		// Mode 1 → `EyePosition()` (vfunc 0x304), mode 2 → `WorldSpaceCenter()` (0x300), mode 3 →
		// `GetAbsOrigin()` (0x364). Mode 2 is unreachable in the shipped game — see the input
		// handlers above.
		const FVector Aim = (EyeLookMode == 3) ? Scripted->Origin : Scripted->EyePosition();
		// The same ±30° head cone (`FUN_10325da0`), with retail's own fallback: outside it the
		// scripted aim is dropped for straight ahead rather than the cascade being resumed.
		Integrate(InsideGazeCone(HeadPos, HeadForward, Aim) ? Aim : Ahead, RateNow(FidgetStep));
		return CurEyeTarget;
	}

	// --- Selection ---------------------------------------------------------------------------
	// Retail stores the SUBJECT (`m_hEyeLookTarget`) and only the two cases that are not an entity
	// — the camera redirect and a scripted look-at — as a point. The tail below turns the subject
	// into this think's EyePosition(), which is what makes a moving subject followed rather than
	// a stale point held until the next re-pick.
	FVector Point = FVector::ZeroVector;
	bool bHavePoint = false;
	bool bResolved = false;

	// 1. The dialogue partner, at their EyePosition() — eye height on the entity, not a head bone,
	//    so the aim holds still through the partner's animation the way retail's does.
	//
	//    The arm is PLAYER-ONLY. Retail (`0x1026b827`-`0x1026b8ce`) resolves `m_hDialogPartner`@0xFE8
	//    and then immediately dereferences `partner+0xA8` — the partner's player pointer — and skips
	//    the whole arm when it is null (`JZ 0x1026b8d9`). An NPC talking to another NPC therefore
	//    gets no dialogue arm at all and falls straight through to the target-entity arm.
	//
	//    Seam: this runtime's dialogue session carries one owner and its partner is always the
	//    player, so `m_hDialogPartner` is stood for by "this character owns the open session". A
	//    session between two NPCs cannot be expressed here yet; when it can, this is the test that
	//    has to keep answering nothing.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		const FElysiumEntity* Partner = (DialogOwner.IsSet() && DialogOwner.Index == Handle.Index)
			? World->FindPlayer() : nullptr;
		if (Partner != nullptr && Partner != this)
		{
			// **The idle scan is suppressed for the whole conversation** (RC5). Every shipped
			// `CNPC_V*` fills slot 333 with `CAI_BaseNPCTroika::FUN_102bff20`, which — before it
			// calls `CAI_BaseNPC::MaintainEyeDirection` unchanged — pushes the re-scan stamp
			// `+0x5d6c` to `curtime + 2.0` (`_DAT_10452dc4 = 2.0f`) on every think while a dialogue
			// partner is live. It is what makes the fall-through below terminal: a `DialogPOV`
			// target the head cannot reach drops through arms 2-5 and then to straight ahead,
			// rather than to a passer-by the scan happened to find — and never back to the
			// partner's eye, which is the arm that was just refused.
			NextEyeLookTime = Now + 2.0f;

			// `DialogPOV` on the shot in effect redirects this arm to the camera. It replaces the
			// *player* as the subject and nothing else, so every other arm of the cascade is
			// untouched.
			const FVector Aim = (DialogPovPoint != nullptr) ? *DialogPovPoint : Partner->EyePosition();
			// Retail cone-tests whichever of the two it picked (`0x1026b887` for the camera,
			// `0x1026b8b6` for the player) and, on a miss, jumps to `0x1026b8d3` — the NEXT arm.
			// It never falls back from the camera to the partner's eyes, and it never falls back
			// from the partner to straight ahead here: the cascade simply continues.
			if (InsideGazeCone(HeadPos, HeadForward, Aim))
			{
				if (DialogPovPoint != nullptr)
				{
					Point = Aim;
					bHavePoint = true;
				}
				else
				{
					EyeLookTargetHandle = Partner->Handle;
				}
				bResolved = true;
			}
		}
	}

	// 2. The target entity, then 3. the enemy — each taken when its EyePosition() is inside the
	//    cone and otherwise passed over for the next arm, exactly as retail falls through.
	if (!bResolved)
	{
		const FElysiumEntity* GazeEntity = GazeTargetEntity();
		if (GazeEntity != nullptr && GazeEntity != this && !GazeEntity->IsInert()
			&& InsideGazeCone(HeadPos, HeadForward, GazeEntity->EyePosition()))
		{
			EyeLookTargetHandle = GazeEntity->Handle;
			bResolved = true;
		}
	}
	if (!bResolved)
	{
		const FElysiumEntity* Enemy = GazeEnemy();
		if (Enemy != nullptr && Enemy != this && !Enemy->IsInert()
			&& InsideGazeCone(HeadPos, HeadForward, Enemy->EyePosition()))
		{
			EyeLookTargetHandle = Enemy->Handle;
			bResolved = true;
		}
	}

	// 4. The navigation goal, then 5. a heard combat sound. Both are DIRECT: retail hands the point
	//    to the head filter and the eyes and returns before the commanded or smoothed targets are
	//    written, so neither the subject nor the integrator sees these frames. The head filter
	//    still runs because retail's does, and because its state is inspectable.
	if (!bResolved)
	{
		FVector Direct = FVector::ZeroVector;
		const bool bDirect =
			(GazeNavigationGoal(Direct) && InsideGazeCone(HeadPos, HeadForward, Direct))
			|| (GazeHeardSound(Direct) && InsideGazeCone(HeadPos, HeadForward, Direct));
		if (bDirect)
		{
			FilterHeadTurn(Direct, HeadPos, HeadForward);
			return Direct;
		}
	}

	// 6. The autonomous scan: nearest qualifying entity inside a 300-unit sphere centred 300 units
	//    along the BODY's facing from the eyes, re-picked every 1-5 seconds; nothing found means
	//    no subject (straight ahead at the tail) and a retry in half a second. Between re-picks the
	//    subject stands and is followed.
	if (!bResolved)
	{
		if (Now >= NextEyeLookTime)
		{
			const FVector Centre = EyePosition() + BodyDirection2D() * GScanReach;
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
					const FVector Aim = E->EyePosition();
					if (FVector::DistSquared(Aim, Centre) > GScanRadius * GScanRadius
						|| !InsideGazeCone(HeadPos, HeadForward, Aim))
					{
						continue;
					}
					// Retail ranks by distance from a point it reaches through slot 220, which the
					// corpus does not name; the head is the nearest recovered point to it.
					const float DistanceSq = FVector::DistSquared(Aim, HeadPos);
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
				EyeLookTargetHandle = Best->Handle;
				NextEyeLookTime = Now + static_cast<float>(FMath::RandRange(1, 5));
				FidgetStep = -1;
			}
			else
			{
				EyeLookTargetHandle = FElysiumEntityHandle::Invalid();
				NextEyeLookTime = Now + 0.5f;
			}
		}
	}

	// --- The subject becomes this think's point ------------------------------------------------
	// Retail's tail: a subject that resolves is looked at where it is NOW; one that does not — gone,
	// or this character itself — is dropped and the aim is straight ahead of the head.
	FVector Commanded = Ahead;
	if (bHavePoint)
	{
		Commanded = Point;
	}
	else if (EyeLookTargetHandle.IsSet())
	{
		const FElysiumEntity* Subject =
			World != nullptr ? World->Resolve(EyeLookTargetHandle) : nullptr;
		if (Subject != nullptr && Subject != this && !Subject->IsInert())
		{
			Commanded = Subject->EyePosition();
		}
		else
		{
			EyeLookTargetHandle = FElysiumEntityHandle::Invalid();
		}
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

	// --- Integration -------------------------------------------------------------------------
	// The rate is whichever of the two the fidget driver would have published this think: the
	// disposition's own rate while a fidget sequence is walking its cells, the global hold rate
	// otherwise. The step is read AFTER the block above, so the think that enters a sequence
	// already integrates at the step rate.
	Integrate(Commanded, RateNow(FidgetStep));

	FilterHeadTurn(EyeLookTarget, HeadPos, HeadForward);
	return CurEyeTarget;
}

void FElysiumCombatCharacter::FilterHeadTurn(const FVector& LookTarget, const FVector& HeadPos,
	const FVector& HeadForward)
{
	// `SetHeadDirection` — head turn, which drives nothing. Retail integrates m_flHeadYaw and
	// m_flHeadPitch every think through this 0.8/0.2 filter and applies them with
	// SetBoneController(0, …) and (1, …) — bone controllers, not pose parameters. No shipped model
	// declares a single bone controller, so the lookup fails and the value never reaches the
	// skeleton. Visible head movement in VtMB dialogue is animation and choreography, not this
	// path. Reproduced, including the unclamped filter and its lone `> 360 → 0` guard, so the
	// state is inspectable and so nobody later mistakes its absence for a missing feature.
	const FRotator ToTarget = (LookTarget - HeadPos).Rotation();
	const FRotator HeadNow = HeadForward.Rotation();
	HeadYaw = HeadYaw * 0.8f + (ToTarget.Yaw - HeadNow.Yaw) * 0.2f;
	HeadPitch = HeadPitch * 0.8f + (ToTarget.Pitch - HeadNow.Pitch) * 0.2f;
	if (HeadYaw > 360.f) { HeadYaw = 0.f; }
	if (HeadPitch > 360.f) { HeadPitch = 0.f; }
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
	if (IsInert())
	{
		return;
	}
	if (AsNpcBase() != nullptr)
	{
		// Story 8 wave 2 (L13): an NPC's damage is retail's slot-142 transaction. The packet goes to
		// `OnTakeDamage` on the body (Troika `0x102bed30` -> `CAI_BaseNPC::OnTakeDamage`
		// `0x10265e90` -> `CBaseCombatCharacter::OnTakeDamage` `0x1032ef60`), whose alive arm runs
		// slot 390 and the resolver inside `0x103302e0`. The feed break and the held-use drop below
		// are the PLAYER's (`CBasePlayer::OnTakeDamage` 0x10163020 is the damage caller of
		// `FeedInterrupt` 0x1033a9e0 and of the `+use` drop 0x10163126); no NPC body calls either.
		// A corpse takes packets too: `BecomeClientRagdoll` (`0x10090180`) only makes it non-solid,
		// sets render FX 0x17 and clears its think, and `m_lifeState` stays LIFE_DYING, so
		// `0x1032ef60`'s dying arm (`[+0x61c]`) is what a packet meets.
		FElysiumDmg Packet = Dmg;
		DispatchTakeDamagePacket(&Packet, 0.f, Attacker,
			Attacker != nullptr ? Attacker->Handle : Dmg.Source, bDisallowFirearmsToBashing);
		return;
	}
	if (HasReportedDeath())
	{
		return;
	}
	// Incoming damage while paired tears the feed down BEFORE the damage commits
	// (`docs/vtmb/feeding.md` § "Interruption, completion and outputs"; `CBasePlayer::OnTakeDamage`
	// 0x10163020 -> `FeedInterrupt` 0x1033a9e0).
	BreakFeed();
	// And the same position is where retail's player drops a held `+use` session (`0x10163126`).
	OnDamageEntered();

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
	if (IsInert())
	{
		return;
	}
	if (AsNpcBase() != nullptr)
	{
		// The scalar packet (word 0 null, `+0x30` the amount) into slot 142, as above. No amount
		// gate: retail's zero tests are `0x103302e0`'s `<= 0.0` and the Troika's zero-damage arm
		// (`0x102bef4d`), both inside the transaction.
		DispatchTakeDamagePacket(nullptr, Amount, nullptr, FElysiumEntityHandle::Invalid(), false);
		return;
	}
	if (Amount <= 0.f)
	{
		return;
	}
	BreakFeed();
	OnDamageEntered();
	CommitDamage(ElysiumDamage::ScalarDescriptor(Amount));
}

void FElysiumCombatCharacter::DispatchTakeDamagePacket(FElysiumDmg* Dmg, float Scalar,
	FElysiumCombatCharacter* Attacker, const FElysiumEntityHandle& AttackerHandle,
	bool bDisallowFirearmsToBashing)
{
	FElysiumNpcBase::FElysiumTakeDamageInfo Info;
	Info.Dmg = Dmg;                                      // +0x00, null on the scalar route
	Info.Attacker = AttackerHandle;                      // +0x2c
	Info.Damage = Scalar;                                // +0x30
	Info.ResolverAttacker = Attacker;
	Info.bDisallowFirearmsToBashing = bDisallowFirearmsToBashing;
	(void)OnTakeDamage(&Info);                           // slot 142
	// The Troika's slot-390 body cached the packet verbatim (`+0x660c`, `0x102bedab`); its word 0
	// and the port-only resolver pointer named this dispatch's stack, so they are not kept.
	if (FElysiumNpc* Troika = AsNpc())
	{
		Troika->LastTakeDamageInfo.Dmg = nullptr;
		Troika->LastTakeDamageInfo.ResolverAttacker = nullptr;
	}
}

// =================================================================================================
// Slot 142 — `CBaseCombatCharacter::OnTakeDamage` `0x1032ef60`.
// =================================================================================================

namespace
{
	// `m_TeamSymbol` (`+0x10b0`) as `0x10323a70` reads it. The team registry `AddToTeam`
	// (`0x103239a0`) fills is not carried (`Spawn19AddToTeam` is a counted seam), so every character
	// answers the constructor's `0xffff` (`0x10326de0`), which is "no team".
	constexpr uint16 GNoTeamSymbol = 0xffff;
	uint16 CombatTeamSymbolOf(const FElysiumCombatCharacter& /*Character*/)
	{
		return GNoTeamSymbol;
	}

	// A retail body this substrate does not carry, called at its retail position: the stub tally
	// (`elysium.stubs`) records the call, and nothing else happens -- the seam answers "nothing".
	void CombatFireSeam(const FElysiumCombatCharacter& Self, const TCHAR* Surface, const TCHAR* Address,
		const FString& Params)
	{
		ElysiumStub::FSurface Row;
		Row.Kind = TEXT("method");
		Row.Surface = Surface;
		Row.Address = Address;
		ElysiumStub::Fired(Row, Self.DebugString(), Params, TEXT("the NPC kernel"));
	}

	// `_DAT_1044e664`, a float 10.0: the corpse's removal think delay in `CreateCorpse`'s tail.
	constexpr float GCombatCorpseThinkDelaySeconds = 10.f;

	// `CBaseCombatCharacter::SpawnStaticCorpse` `0x1032be80`: `CreateNoSpawn("prop_base", origin,
	// angles)` (slots 219 / 218), `CopyAnimationDataFrom(this)` twice around its slot 103 `Spawn`,
	// `SetOccludesSound(0)`, `SetSolid(SOLID_NONE)`, `SetSolidFlags(0)`, `m_fEffects = this->m_fEffects
	// | 0xb0`, `ForceTransmit`, and the act store's corpse registration `0x102ca6c0(DAT_109253f8, this,
	// corpse)`. Here: a runtime `prop_base` record at this body's origin, angles and model. NAMED GAPS:
	// no class answers `prop_base`, so the record carries no body and no think (the pose copy and the
	// render words are visual-only); the act-store registration is the same unported store the
	// corpse query seam (`ElysiumNpcConditions19.inl`) stands for, and answers nothing.
	FElysiumEntity* CombatSpawnStaticCorpse(FElysiumCombatCharacter& Self)
	{
		if (Self.World == nullptr)
		{
			return nullptr;
		}
		FElysiumEntityDef Def;
		Def.Classname = TEXT("prop_base");
		Def.Origin = Self.Origin;
		FElysiumEntity* const Corpse = Self.World->Resolve(Self.World->SpawnRuntimeEntity(MoveTemp(Def)));
		if (Corpse != nullptr)
		{
			Corpse->Angles = Self.Angles;
			Corpse->Model = Self.Model;
		}
		return Corpse;
	}

	// `0x10207df0`: the character's template (`0x101d5f10` over `DAT_10738d10`) authors
	// `General/Has_Burning_Death` (`+0x98`), OR the character is Kindred (`0x10337f30`) and the
	// template does not author `General/Disallow_Kindred_Death` (`+0x9d`) -- the parse is `0x101d4520`.
	bool CombatCorpseBurnsAway(FElysiumCombatCharacter& Self)
	{
		bool bHasBurningDeath = false;
		bool bDisallowKindredDeath = false;
		const FElysiumNpc* const Npc = Self.AsNpc();
		UElysiumSessionSubsystem* const GameState = Self.World != nullptr ? Self.World->GetGameState() : nullptr;
		UElysiumRulebookSubsystem* const Rules = GameState != nullptr ? GameState->Rulebook() : nullptr;
		FElysiumClanTemplate Resolved;
		if (Npc != nullptr && Rules != nullptr && !Npc->StatTemplate.IsEmpty()
			&& Rules->Clans().Resolve(Npc->StatTemplate, Resolved))
		{
			bHasBurningDeath = Resolved.GeneralInt(TEXT("Has_Burning_Death")) != 0;
			bDisallowKindredDeath = Resolved.GeneralInt(TEXT("Disallow_Kindred_Death")) != 0;
		}
		if (bHasBurningDeath)
		{
			return true;
		}
		return Self.IsKindred() && !bDisallowKindredDeath;
	}

	// `Rules.txt` `VampFrenzy_Info`, which `0x101e6310` loads into the process-global `CVFeatList_t`
	// (`0x10739d08`); the defaults are that loader's immediates (`0x101e644d` / `0x101e6457` /
	// `0x101e647b`): `Dmg_Amount` 0x39 (`+0x348`, getter `0x101e8dc0`), `AggrDmg_Amount` 0x1d
	// (`+0x34c`, `0x101e8de0`), `Default_Difficulty` 5 (`+0x35c`, `0x101e8e60`).
	int32 CombatVampFrenzyRule(const FElysiumCombatCharacter& Self, const TCHAR* Key, int32 ImageDefault)
	{
		UElysiumSessionSubsystem* GameState = Self.World != nullptr ? Self.World->GetGameState() : nullptr;
		UElysiumRulebookSubsystem* Rules = GameState != nullptr ? GameState->Rulebook() : nullptr;
		return Rules != nullptr ? Rules->Rules().Int(TEXT("VampFrenzy_Info"), Key, ImageDefault) : ImageDefault;
	}

	// `0x10323930`: both characters on the same team (a non-`0xffff` symbol, equal on both).
	bool CombatSameTeam(const FElysiumCombatCharacter& Self, const FElysiumCombatCharacter* Other)
	{
		const uint16 Mine = CombatTeamSymbolOf(Self);
		if (Mine == GNoTeamSymbol || Other == nullptr)
		{
			return false;
		}
		const uint16 Theirs = CombatTeamSymbolOf(*Other);
		return Theirs != GNoTeamSymbol && Theirs == Mine;
	}
}

int32 FElysiumCombatCharacter::OnTakeDamage(void* InInfo)
{
	using FInfo = FElysiumNpcBase::FElysiumTakeDamageInfo;
	FInfo* const Info = static_cast<FInfo*>(InInfo);
	// Crash guard (named divergence): retail reads the packet without a test.
	if (Info == nullptr)
	{
		return 0;
	}

	// 1. `m_takedamage == DAMAGE_NO` answers 0 with nothing run.
	if (TakeDamageMode == 0)
	{
		return 0;
	}

	// 2. A teammate's packet is refused (`0x10323930` against the attacker's `+0x9c`
	//    combat-character self-cast), unless the attacker is this body itself.
	FElysiumEntity* const AttackerEntity =
		(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr;
	const FElysiumCombatCharacter* const AttackerCharacter =
		AttackerEntity != nullptr ? AttackerEntity->AsCombatCharacter() : nullptr;
	if (CombatSameTeam(*this, AttackerCharacter) && AttackerEntity != this)
	{
		return 0;
	}

	// 3. `0x101e3cf0` on the effect manager `DAT_10739a4c`: every active effect whose record's
	//    `+0x31` byte (`ShouldRemove_OnTakeDamage`) is set is removed — BEFORE the life-state split,
	//    so it runs on every packet past the two gates, whatever the packet commits.
	ElysiumDisciplines::NotifyDamaged(*this);

	// 4. The life-state split on `m_lifeState` (`+0x200`), the NPC's raw word.
	const FElysiumNpcBase* const NpcBase = AsNpcBase();
	const int32 LifeState = NpcBase != nullptr ? NpcBase->AnimEventLifeStateWord : 0;
	if (LifeState != 0)
	{
		if (LifeState == 1)
		{
			return OnTakeDamage_Dying(Info);                                     // slot 391 (+0x61c)
		}
		const int32 Result = OnTakeDamage_Dead(Info);                            // slot 392 (+0x620)
		// A corpse at or below zero health gibs on a `DMG_GIB_CORPSE`-family (`0xe1`) hit, and the
		// body answers 0 then.
		const uint32 Bits = Info->Dmg != nullptr ? (Info->Dmg->DmgMask | Info->DamageBits) : Info->DamageBits;
		if (Health < 1 && (Bits & 0xe1u) != 0)
		{
			(void)Event_Gibbed();                                                // slot 402 (+0x648)
			return 0;
		}
		return Result;
	}

	// 5. Alive: slot 390 `OnTakeDamage_Alive` (+0x618); its answer is what the body returns.
	const int32 Result = OnTakeDamage_Alive(Info);

	// 6. `Health` (stat 0xf, damage taken) against `Max_Health` (0x11) off the stat list, both read
	//    before the heal-over-time starts.
	using EC = EElysiumTraitContainer;
	const int32 Taken = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);       // GetValue(0xf)
	const int32 Ceiling = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);  // GetValue(0x11)
	// `CBaseCombatCharacter::BeginVampHeal_HOT` `0x10324ba0` (`0x1032f17a`, after both stat reads):
	// `CanVampHeal_HOT` then `m_flLastBloodHealHOTUpdate = now + VampHeal_HOT_Delay`. SEAM: the vampire
	// heal-over-time is not carried by this substrate (no `+0xa84` clock, no HOT update), so the call
	// is tallied and nothing is written.
	CombatFireSeam(*this, TEXT("CBaseCombatCharacter::BeginVampHeal_HOT"), TEXT("0x10324ba0"), FString());

	// 7. Still standing: `CreatePotenceHitEffect` (graded 1/2/3 by the damage bands) when a player
	//    attacker (`+0xa8`) holding a weapon whose slot-360 flags carry `0x18000` has Potence
	//    (stat 9) >= 1. Visual only (a particle and a sound on this body); not carried, named.
	// A body with no health track (`Max_Health` 0, a record that never seeded a sheet) cannot die of
	// the compare: the port's guard, as in the commit (`CommitDamageHealth`).
	if (Ceiling <= 0 || Taken < Ceiling)
	{
		return Result;
	}

	// 8. The kill: slot 144 `Event_Killed(info)`, then slot 399 (`ShouldGib`) — or, when it answers
	//    false, a `DMG_ALWAYSGIB` (0x2000) hit without `DMG_NEVERGIB` (0x1000) — takes slot 402
	//    `Event_Gibbed`, whose answer is what the body returns; a false answer (or no gib) takes
	//    slot 403 `Event_Dying`.
	Event_Killed(Info);                                                          // slot 144 (+0x240)
	const uint32 Bits = Info->Dmg != nullptr ? (Info->Dmg->DmgMask | Info->DamageBits) : Info->DamageBits;
	const bool bGib = Slot399()                                                  // slot 399 (+0x63c)
		|| ((Bits & 0x2000u) != 0 && (Bits & 0x1000u) == 0);
	if (bGib)
	{
		const bool bGibbed = Event_Gibbed();                                     // slot 402 (+0x648)
		if (bGibbed)
		{
			return 1;
		}
		Event_Dying();                                                           // slot 403 (+0x64c)
		return 0;
	}
	Event_Dying();                                                               // slot 403 (+0x64c)
	return Result;
}

// =================================================================================================
// Slot 144 — `CBaseCombatCharacter::Event_Killed` `0x1032b9b0`.
// =================================================================================================

void FElysiumCombatCharacter::Event_Killed(void* InInfo)
{
	using FInfo = FElysiumNpcBase::FElysiumTakeDamageInfo;
	FInfo* const Info = static_cast<FInfo*>(InInfo);

	// Port bookkeeping, ahead of retail's body: a held reaction claim is released by a predicate its
	// producer re-checks, and a dying character re-checks nothing (the body-claim arbiter).
	ReleaseHeldReaction();

	// 1. `m_lifeState = LIFE_DYING` (`+0x200`), the NPC's raw word.
	if (FElysiumNpcBase* const NpcBase = AsNpcBase())
	{
		NpcBase->AnimEventLifeStateWord = 1;
	}

	// 2. Slot 385 `Weapon_Drop(active weapon)` (`+0x604`) — the drop of the held weapon.
	FElysiumEntity* const ActiveWeaponEntity =
		(World != nullptr && Inventory.ActiveWeapon.IsSet()) ? World->Resolve(Inventory.ActiveWeapon) : nullptr;
	Weapon_Drop(ActiveWeaponEntity, nullptr, false);

	// 3. A grapple VICTIM (`+0x1538` resolving, `+0x153c == 1`) tears its partner's feed down:
	//    slot 353 `FeedInterrupt` on the partner.
	if (Grapple.Role == EElysiumGrappleRole::Victim)
	{
		if (FElysiumCombatCharacter* const Partner = ResolveGrapplePartner())
		{
			Partner->FeedInterrupt();
		}
	}

	// 4. `RemoveDisciplineVisuals` and `RemoveFromPresenceList` — UNRECOVERED here (the discipline
	//    visual list and the presence list are not carried); `RemoveFromComfortList` is.
	RemoveFromComfortList();
	// 5. The engine interface call `(*DAT_1070b248)->vfunc6(entindex, 1)` — UNRECOVERED here.

	// 6. The owner's slot 139 `DeathNotice(this)` — the maker's child-died notice.
	NotifyOwnerOfTermination(EElysiumOwnedEntityTermination::Died);

	// 7. The ragdoll force (the packet's force, else `CalcDamageForceVector`, plus the absolute
	//    velocity, clamped) — UNRECOVERED on the port's packet, which carries no force: the corpse
	//    takes none, as `CompleteDeathHandoff` has always stated. Then slot 301 `CreateCorpse`.
	CreateCorpse(FVector::ZeroVector, Info);

	// 8. `m_iCurFrenzyCount = 0` (`0x1032bcba MOV [ESI+0x146c],0`).
	CurFrenzyCount = 0;

	// 9. The attacker's (`info+0x2c`, its `+0x9c` combat character) slot 300 `Event_TookLife(this,
	//    ...)`.
	if (Info != nullptr && World != nullptr && Info->Attacker.IsSet())
	{
		if (FElysiumEntity* const Attacker = World->Resolve(Info->Attacker))
		{
			if (FElysiumCombatCharacter* const Killer = Attacker->AsCombatCharacter())
			{
				// Retail passes the packet's two bytes `+0x48` / `+0x49` (`0x1032bcd9` -> `0x101c2af0`,
				// `0x1032bce3` -> `0x101c2ab0`); the port's packet carries neither (unrecovered words,
				// no current body reads them), so both go as false.
				Killer->Event_TookLife(this, false, false);                      // 0x1032bcec slot 300
			}
		}
	}
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s died"), *DebugString());
}

// =================================================================================================
// Slot 301 — `CBaseCombatCharacter::CreateCorpse` `0x1032c0e0`.
// =================================================================================================

void FElysiumCombatCharacter::CreateCorpse(const FVector& Force, void* InInfo)
{
	using FInfo = FElysiumNpcBase::FElysiumTakeDamageInfo;
	const FInfo* const Info = static_cast<const FInfo*>(InInfo);
	(void)Force;
	(void)Info;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumNpcBase* const NpcBase = AsNpcBase();

	// 1. `OnDeath` once more through the Troika self-cast (`+0x98`, `0x10265a90`), crediting the
	//    self-cast's `m_hLastEnemy` (`+0x1a94`, `0x1032c1aa..0x1032c1d9`) -- not the packet's attacker.
	//    Latched by the first fire in `CAI_BaseNPC::Event_Killed`, so a no-op on that path.
	if (NpcBase != nullptr)
	{
		NpcBase->FireOnDeathOnce(NpcBase->BaseMemory.LastEnemy);
	}
	// 2. The ragdoll seed bone (`0x101c2a30`: the packet's hit bone, else `Bip01 Spine2`) and the
	//    force are the client ragdoll's; neither reaches this runtime's physics handoff.

	// 3. The corpse, by three arms (`0x1032c22d..0x1032c2e4`).
	FElysiumEntity* Corpse = nullptr;
	const bool bIsPlayer = World != nullptr && Handle == World->PlayerHandle();   // `+0xa8`, the player self-cast
	if (bIsPlayer)                                                             // 0x1032c22d / 0x1032c237 JZ
	{
		// A player's body leaves a static corpse (`0x1032c23a`), and its 18 body-fire particle
		// handles (`m_hBodyFireParticles`) are each `UTIL_Remove`d (`0x1032c24c..0x1032c283`) -- the
		// player carries no such handles here, so the loop has nothing to take.
		Corpse = CombatSpawnStaticCorpse(*this);
	}
	else if ((MiscFlags & 0x80000u) != 0)                                      // 0x1032c288 HasMiscFlag(0x80000)
	{
		Corpse = CombatSpawnStaticCorpse(*this);                               // 0x1032c2af 0x10001c03
		Hide();                                                                // 0x1032c2ba slot 66
		// `ThinkSet(SUB_Remove)` (`0x10015b68` -> `0x101c0b10`), `m_flNextThink = curtime + 0.5`
		// (`_DAT_104454d0`): the body is removed and the static corpse stays.
		if (NpcBase != nullptr)
		{
			NpcBase->ThinkSet(TEXT("0x101c0b10"), 0.0);                        // 0x1032c2cb
		}
		NextThink = static_cast<float>(Now + static_cast<double>(ElysiumNpcTunables::Half));
	}
	else
	{
		BecomeClientRagdoll();                                                 // 0x1032c29c 0x10090180
		Corpse = this;                                                         // 0x1032c2a5 slot 137 answers `this`
	}

	// 4. The corpse's own removal (`0x1032c2e4..0x1032c423`), for a non-player corpse. A corpse that
	//    burns away (`0x10207df0`) is removed outright at +10 s with the burning-death visuals and
	//    sound; any other is `SUB_PVSRemove`d at +10 s -- removed once no player can see it.
	if (Corpse != nullptr)
	{
		const bool bCorpseIsPlayer = World != nullptr && Corpse->Handle == World->PlayerHandle();   // corpse `+0xa8`
		FElysiumNpcBase* const CorpseNpc = Corpse->AsNpcBase();
		const bool bBurns = CombatCorpseBurnsAway(*this);                     // 0x1032c2f6 0x10207df0
		if (!bCorpseIsPlayer)
		{
			if (bBurns)
			{
				// `0x1032c30f..0x1032c3fe`: the corpse's slot 243 takes this body's slot 244(1) (the
				// burn material, visual), `ThinkSet(SUB_Remove)` at +10 s, and the
				// `"character/vampire burning death.wav"` emission (`0x1061fff0`, volume 1.0,
				// attenuation 0.8) -- the render and the sound are not carried (visual/audio only).
				if (CorpseNpc != nullptr)
				{
					CorpseNpc->ThinkSet(TEXT("0x101c0b10"), 0.0);              // 0x1032c33c
				}
			}
			else if (CorpseNpc != nullptr)
			{
				CorpseNpc->ThinkSet(TEXT("0x102696f0"), 0.0);                  // 0x1032c40f ThinkSet(0x10009c9b SUB_PVSRemove)
			}
			// `curtime + _DAT_1044e664` (10.0) on either arm (`0x1032c347`, `0x1032c41a..0x1032c423`).
			// A static `prop_base` corpse is a record-only entity here (no class answers
			// `prop_base`), so it carries no think: its SUB_PVSRemove is not run (named gap).
			if (CorpseNpc != nullptr)
			{
				Corpse->NextThink = static_cast<float>(Now + static_cast<double>(GCombatCorpseThinkDelaySeconds));
			}
		}
		// `UTIL_Remove(m_hAnimFollowModel)` and the handle reset (`0x1032c429..0x1032c45e`): the
		// ornament an animation event hung on this body goes with the death.
		if (!AnimFollowModel.IsEmpty())
		{
			IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
			if (Embodiment != nullptr && Visual != nullptr)
			{
				Embodiment->DetachOrnamentModel(Visual);
			}
			AnimFollowModel.Reset();
		}
		// `ForceTransmit(this)` / `ForceTransmit(corpse)`: network transmission, nothing to do here.
	}
}

// `CBaseAnimating::BecomeClientRagdoll` `0x10090180` for a character: the pose goes to physics,
// the entity stops being solid and stops thinking. The base keeps its body.
void FElysiumCombatCharacter::BecomeClientRagdoll()
{
}



// =================================================================================================
// Slot 390 — `CBaseCombatCharacter::OnTakeDamage_Alive` `0x103302e0`.
// =================================================================================================

int32 FElysiumCombatCharacter::OnTakeDamage_Alive(void* InInfo)
{
	using FInfo = FElysiumNpcBase::FElysiumTakeDamageInfo;
	FInfo* const Info = static_cast<FInfo*>(InInfo);
	// Crash guard (named divergence): retail reads the packet without a test. Every exit answers 1
	// (the one exit, `0x10330abe`).
	if (Info == nullptr)
	{
		return 1;
	}

	// UNRECOVERED here, in retail order, each named rather than guessed:
	//  - `InPrayer(this)` -> slot 358 (`+0x598`), the prayer interrupt;
	//  - `0x103300c0` / `0x10330220`, the hit render-FX flash (`m_nRenderFX` 0x1a / 0x25) and the
	//    random one-of-three hit sound off the sound table `DAT_1074e098` (visual/audio only);
	//  - `0x101cebc0` on a player attacker (`+0xa8`) with the whole damage, and `0x101c2b30` /
	//    `0x1023e4b0`, the victim-side write gated on the Troika's `m_bInvincible`;
	//  - `m_bitsDamageType` (the combined bits) and the death-throw direction `_DAT_1070ba40..48`
	//    from the inflictor: the port has no `m_bitsDamageType` word and no inflictor on its packet;
	//  - the global `DAT_10936de4` gate (its slot 1 false and its `+0x2c` non-zero skips the commit).

	// Slot 299 `CreateDamageEffects(info, &bSkip)` (`+0x4ac`): a raised byte ends the body.
	bool bSkipCommit = false;
	CreateDamageEffects(Info, &bSkipCommit);
	if (bSkipCommit)
	{
		return 1;
	}

	// `m_takedamage == DAMAGE_EVENTS_ONLY` commits nothing.
	if (TakeDamageMode == 1)
	{
		return 1;
	}

	// The amount: `CVDmg_t::Apply` on word 0 (the resolver), else the packet's scalar `+0x30`;
	// nothing at or below 0.0 is committed.
	// `CVDmg_t::Apply` takes its attacker from the packet (`0x103306a6 CALL 0x100140b5` ->
	// `0x101c29b0`, `info+0x2c`), so every producer that builds its own packet resolves the same way.
	FElysiumEntity* const PacketAttacker =
		(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr;
	FElysiumCombatCharacter* const AttackerCharacter =
		PacketAttacker != nullptr ? PacketAttacker->AsCombatCharacter() : nullptr;
	FElysiumDmg Scalar;
	const FElysiumDmg* Committed = nullptr;
	if (Info->Dmg != nullptr)
	{
		if (!ElysiumDamage::Apply(*Info->Dmg, AttackerCharacter, *this,
			FElysiumDamageContext::FromCharacter(*this), Info->bDisallowFirearmsToBashing))   // 0x103306b0
		{
			return 1;   // a family-less descriptor: Apply reported why
		}
		Committed = Info->Dmg;
	}
	else
	{
		Scalar = ElysiumDamage::ScalarDescriptor(Info->Damage);
		Committed = &Scalar;
	}
	if (Committed->CommittedDamage() <= 0)
	{
		return 1;
	}

	// The arithmetic: HealthBuffer (0x19), the unkillable `0x4b` cap, `AddBase(0xf)`, Kindred
	// aggravated (0x10) and `m_iHealth = HealthToPercent()` (slot 348). The aggravated test reads
	// `m_bitsDamageType` (+0xe98), the descriptor's mask OR'd with the packet's bits
	// (`0x1033059a..0x103305a7`).
	FElysiumDmg BitsDamageType = *Committed;
	BitsDamageType.DmgMask |= Info->DamageBits;
	if (!CommitDamageHealth(BitsDamageType))
	{
		return 1;
	}

	// The Kindred frenzy arm (`0x103309fb..0x10330aa9`), past the aggravated add: a live attacker
	// whose `+0x9c` combat character stands (`0x103309fb` / `0x10330a06`), and the hit measured as
	// the descriptor's `GetDmg()` or else the packet's float (`0x10330a14..0x10330a31`). At or above
	// `VampFrenzy_Info/Dmg_Amount` (`0x10330a3a`, `FCOMP; TEST AH,0x41; JNP`) the check runs; else at
	// or above `AggrDmg_Amount` (`0x10330a78`, `JP` skips) it runs only for an aggravated hit
	// (`0x10330a90 TEST [+0xe98],0xc8000008`). The check is `FrenzyCheck(Default_Difficulty)`
	// (`0x10330aa1` / `0x10330aa9`).
	if (IsKindred() && AttackerCharacter != nullptr)                             // 0x10330979 / 0x10330a0e
	{
		const float Measured = Info->Dmg != nullptr ? static_cast<float>(Info->Dmg->GetDmg()) : Info->Damage;
		bool bFrenzyCheck = false;
		if (static_cast<float>(CombatVampFrenzyRule(*this, TEXT("Dmg_Amount"), 0x39)) <= Measured)          // 0x10330a50 JNP
		{
			bFrenzyCheck = true;
		}
		else if (static_cast<float>(CombatVampFrenzyRule(*this, TEXT("AggrDmg_Amount"), 0x1d)) <= Measured // 0x10330a8e JP
			&& (BitsDamageType.DmgMask & ElysiumDamage::NoSoakMask) != 0)                                  // 0x10330a90
		{
			bFrenzyCheck = true;
		}
		if (bFrenzyCheck)
		{
			// `CBaseCombatCharacter::FrenzyCheck` `0x1033eb60` (1,270 bytes) -- SEAM: the frenzy roll
			// and its outcome are not carried by this substrate; the call is tallied with the
			// difficulty it was handed and nothing is written.
			const int32 Difficulty = CombatVampFrenzyRule(*this, TEXT("Default_Difficulty"), 5);          // 0x10330aa1 0x101e8e60
			CombatFireSeam(*this, TEXT("CBaseCombatCharacter::FrenzyCheck"), TEXT("0x1033eb60"),
				FString::Printf(TEXT("difficulty=%d"), Difficulty));                                      // 0x10330aa9
		}
	}

	// The damage flinch. NAMED EVENT-ORDER DIVERGENCE, not a modernization (L13 review row 8,
	// deferred): `0x103302e0..0x10330ad4` calls no slot 292; retail reaches `DamageFlinch` from
	// `CAI_BaseNPC::TraceAttack` (`0x10266780`, ported) BEFORE the transaction, and only for a
	// traced hit. The port's hit producers dispatch no slot 141 (they carry no trace and there is no
	// multi-damage accumulator to take `AddMultiDamage`'s packet), so the flinch stands here, after
	// the commit and for every packet, until that wire is built.
	StartDamageFlinch(*Committed);
	return 1;
}

void FElysiumCombatCharacter::CommitDamage(const FElysiumDmg& Dmg)
{
	if (!CommitDamageHealth(Dmg))
	{
		return;
	}

	// `NPC_TAKE_DAMAGE`, from its real producer: the noise a body makes when it is hit, emitted only
	// once damage has actually landed on this character. Nothing to commit, and no health track,
	// make no sound, which is right: neither is a hit.
	if (World != nullptr)
	{
		World->EmitGameSound(Origin, ElysiumGameSounds::NpcTakeDamage(),
			/*RadiusCm, table-resolved*/ -1.f, Handle,
			ElysiumStealth::HearingReductionCmFor(this), ElysiumGameSounds::Combat, 0.2);
	}

	OnDamageCommitted(Dmg);

	// The generic damage flinch, from the one commit and BEFORE the death test below.
	StartDamageFlinch(Dmg);

	// `ShouldRemove_OnTakeDamage`, from the one typed health commit. It also reconciles
	// the Bloodshield teardown above: `EndBloodshield` drops the power's trait group when the buffer
	// exhausts, and this is where the tracked targeted effect that installed it is retired with it.
	ElysiumDisciplines::NotifyDamaged(*this);

	// The outputs. The player wires neither, but FireOutput is inert for an output an entity did not
	// wire. OnHalfHealth is OFFERED on every damaging hit while the projected health sits at or
	// below half, not only on the crossing edge.
	static const FName OnDamaged(TEXT("OnDamaged"));
	static const FName OnHalfHealth(TEXT("OnHalfHealth"));
	FireOutput(OnDamaged, Dmg.Source);
	if (MaxHealth > 0 && Health * 2 <= MaxHealth)
	{
		FireOutput(OnHalfHealth, Dmg.Source);
	}

	// Death is the RPG comparison, not the engine-space projection: the damage counter reaching
	// the ceiling is what selects it.
	using EC = EElysiumTraitContainer;
	const int32 Taken = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	const int32 Ceiling = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);
	if (!bUnkillable && Ceiling > 0 && Taken >= Ceiling)
	{
		OnKilled();
	}
}

bool FElysiumCombatCharacter::CommitDamageHealth(const FElysiumDmg& Dmg)
{
	using EC = EElysiumTraitContainer;

	int32 Remaining = Dmg.CommittedDamage();
	if (Remaining <= 0)
	{
		return false;
	}
	// An NPC's commit is `0x103302e0`'s: its ceiling is the sheet's `Max_Health` (stat 0x11) and its
	// engine-space projection is slot 348 `HealthToPercent` into `m_iHealth` alone — `m_iMaxHealth`
	// stays `NPCInit`'s 100 (`0x1027344f`). The player keeps the sheet-derived pair.
	const bool bRetailNpcCommit = AsNpcBase() != nullptr;
	const int32 SheetCeiling = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);
	auto Project = [this, bRetailNpcCommit]()
	{
		if (bRetailNpcCommit)
		{
			Sheet.RecomputeCurrent(SheetRules(), SheetEffects());
			Health = HealthToPercent();                                          // slot 348
		}
		else
		{
			RecomputeSheet();
		}
	};
	if ((bRetailNpcCommit ? SheetCeiling : MaxHealth) <= 0)
	{
		// No health track: the rulebook did not load, or the character was built without a sheet.
		// Damage is recorded rather than applied — a character with no health model must not die of
		// arithmetic.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s took %d damage with no health track"),
			*DebugString(), Remaining);
		return false;
	}

	// `0x103302e0`'s commit, arm by arm (`0x10330733..0x10330ab8`). The amount is the float retail
	// carries in `[ESP+0x14]`; the descriptor's committed integer here.
	float Amount = static_cast<float>(Remaining);

	// 1. HealthBuffer (stat 0x19, the CURRENT value, `0x10330737 GetValue`): a non-zero buffer
	//    (`0x10330740 JZ`, not `> 0`) absorbs `trunc(DAT_10739a68 * amount * 0.01)` -- the
	//    process-global Bloodshield block percentage (`0x10330746 FILD`, `0x1033074c`/`0x10330750`
	//    FMUL, `0x10330756 __ftol`) -- and the amount loses exactly that, uncapped by the buffer
	//    (`0x1033076f FSUBR`). Less than the buffer spends it (`0x10330847 SubBase(0x19, absorbed)`);
	//    otherwise the buffer is zeroed (`0x103307d9 SetBase(0x19, 0)`) and Bloodshield ends
	//    (`0x103307e9`, `"Thaumaturgy_Bloodshield"`). No projection runs here.
	const int32 Buffer = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::HealthBuffer);
	if (Buffer != 0)                                                                   // 0x10330740
	{
		// Evaluated in single precision, the port's convention for the x87 chain (the precision-control
		// word retail runs under is not recovered, so a product that lands a hair under an integer
		// -- `0.01f` is 0.0099999998 -- is not guaranteed to truncate the way retail's did).
		const int32 Absorbed = static_cast<int32>(static_cast<float>(ElysiumDisciplines::HealthBufferBlockPercent())
			* Amount * 0.01f);                                                         // 0x10330746..0x10330756
		Amount -= static_cast<float>(Absorbed);                                        // 0x1033076f
		if (Absorbed < Buffer)                                                         // 0x1033076d / 0x10330777 JL
		{
			Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthBuffer,
				Sheet.GetBase(EC::Attributes, ElysiumSlot::HealthBuffer) - Absorbed);   // 0x10330847 SubBase
		}
		else
		{
			Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthBuffer, 0);             // 0x103307d9 SetBase(0x19, 0)
			EndBloodshield();                                                          // 0x103307e9
		}
	}
	// 2. Nothing left at or below 0.0 ends the body (`0x1033084c FCOMP 0.0` -> `0x10330abe`), before
	//    `m_iHealth` is projected.
	if (Amount <= 0.f)
	{
		Sheet.RecomputeCurrent(SheetRules(), SheetEffects());
		return false;
	}
	// 3. Unkillable (`+0xfc8`): the damage TAKEN, read as its current value (`0x103308cd GetValue(0xf)`),
	//    plus the truncated amount may not pass the retail literal `0x4b`; past it the amount becomes
	//    `0x4b - taken` (`0x103308e9..0x103308fd`). Not a one-hit-point floor and not a percentage.
	const int32 TakenCurrent = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	if (bUnkillable && static_cast<int32>(Amount) + TakenCurrent > ElysiumDamage::UnkillableDamageCap)
	{
		Amount = static_cast<float>(ElysiumDamage::UnkillableDamageCap - TakenCurrent);
	}
	// 4. `AddBase(0xf, trunc(amount))` (`0x1033096b`, `CVStatList_t::AddBase` `0x10200fc0`): no clamp
	//    here -- the sheet's own rules-table clamp is whatever `AddBase` meets.
	const int32 Committed = static_cast<int32>(Amount);                                // 0x1033095f __ftol
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health,
		Sheet.GetBase(EC::Attributes, ElysiumSlot::Health) + Committed);
	// 5. A Kindred victim (`0x10330972 IsKindred`) whose `m_bitsDamageType` (+0xe98, the descriptor
	//    mask OR'd with the packet's bits, `0x1033059a..0x103305a7`) carries `0xc8000008` also takes
	//    the SAME amount as aggravated damage (`0x103309f6 AddBase(0x10, EBX)`).
	if (IsKindred() && (Dmg.DmgMask & ElysiumDamage::NoSoakMask) != 0)                   // 0x1033097f TEST 0xc8000008
	{
		Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthAggDmg,
			Sheet.GetBase(EC::Attributes, ElysiumSlot::HealthAggDmg) + Committed);
	}
	// 6. `m_iHealth = HealthToPercent()` (slot 348, `0x10330ab2`) -- the sheet pair projected back.
	//    (The Kindred frenzy arm `0x103309fb..0x10330aa9` runs between 5 and 6 in retail; it is the
	//    caller's, `OnTakeDamage_Alive`, because it reads the packet's attacker. `FrenzyCheck` is a
	//    seam that writes nothing, so nothing observes the projection landing first.)
	Project();
	return true;
}

void FElysiumCombatCharacter::StartDamageFlinch(const FElysiumDmg& Dmg)
{
	// **Slot 292's species gate, ahead of everything else** (story 29c-1, family Damage). Retail's
	// `CNPC_VGargoyle` (`0x10378cb0`) and `CNPC_VHengeyokai` (`0x103802a0`) answer slot 292 with a
	// body that tests the hit BEFORE it calls `CBaseCombatCharacter::DamageFlinch` at all: a hit
	// carrying `DMG_BULLET | DMG_BUCKSHOT` (`0x4000002`) or measuring exactly zero never reaches the
	// base body. So it is asked here, in front of the embodiment test, and not inside it — the
	// refusal must not be a function of whether this character happens to have a visual.
	if (SuppressesDamageFlinch(Dmg))
	{
		return;
	}

	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return;   // no body to flinch — an ordinary negative, not a failure
	}

	// **The gate.** Retail derives the direction from the attack's own world position, and a hit with
	// no attacker in the world hands that derivation a zero vector — which orients the flinch at the
	// world origin rather than at anything. `trigger_hurt`, a crushing mover, the scalar
	// `TakeDamage(float)` route and a character hurting itself all reach this commit that way. The
	// conservative reading is taken: a hit with no distinct attacking character flinches nothing,
	// rather than reproducing a world-frame artefact as if it were a direction.
	const FElysiumEntity* SourceEntity = World->Resolve(Dmg.Source);
	const FElysiumCombatCharacter* Attacker =
		SourceEntity != nullptr ? SourceEntity->AsCombatCharacter() : nullptr;
	if (Attacker == nullptr || Attacker == this)
	{
		return;
	}

	// **The yield, and it is OURS rather than retail's** (`MeleeReactionHoldsBaseUntil` on the
	// header states why). A reaction claim owns the base channel our flinch would take, so the flinch
	// stands down while that claim stands instead of replacing a pose that is still on screen.
	//
	// **Both expressions of "a reaction claim is standing" answer here**, because the yield is about
	// the channel and not about which condition will release it: a struck reaction's timed hold, and
	// a HELD claim a predicate releases — the player's own block, which stands for as long as the
	// button does and whose classification is what decided the contact was blocked in the first
	// place.
	//
	// Ahead of BOTH draws, exactly as the coincident-origins refusal is: a flinch that does not
	// happen is not a hit, and must advance the Reaction stream by nothing — otherwise how many
	// blocks a fight contained would silently reshuffle every reaction after it.
	if (bHoldsReactionClaim)
	{
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s yields its flinch: a held reaction claim owns the base pose"), *DebugString());
		return;
	}
	if (World->NowSeconds() < MeleeReactionHoldsBaseUntil)
	{
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s yields its flinch: a melee contact reaction holds the base pose until %.3f"),
			*DebugString(), MeleeReactionHoldsBaseUntil);
		return;
	}

	// Retail's flinch draws at random, so this one does too — off the session's own Reaction stream,
	// whose position is in the save. Every blow is a
	// fresh pick, jitter and weighted choice; nothing here is a function of the victim or of how many
	// times it has been hit.
	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Reaction);
	ElysiumReactions::FElysiumFlinch Pick;
	if (!ElysiumReactions::BuildFlinch(Attacker->Origin, Origin,
		ElysiumSkeletalBasis::FromSourceAngles(Angles).Yaw, Rng, Pick))
	{
		return;   // horizontally coincident origins name no direction on a yaw fan
	}

	// Retail's flinch is a gesture call: `AddGesture` reaches `SelectWeightedSequence` and simply
	// returns on -1, so a body with no reaction in its vocabulary flinches nothing. It never walks the
	// availability probe, the disposition retry or sequence zero — substituting a stance for a hit
	// reaction is exactly the behaviour this clears. The blend pair is hard-coded because the gesture
	// path's is: every other reaction takes the resolved clip's own authored fade.
	//
	// **The envelope IS the flinch's whole life**, which is why it names its own release condition:
	// retail's fade values are compiled constants in seconds with no hold between them, so the weight
	// is a linear triangle that peaks at `FlinchBlendInSeconds` and is gone at the sum of the pair.
	// The clip's own length decides nothing — retail evaluates a static pose under that envelope — so
	// a two-frame hit cell and a long one occupy the channel for exactly the same 0.4 s.
	FElysiumReactionPlayRequest Reaction;
	Reaction.Activity = Pick.Activity();
	Reaction.HitYawDegrees = Pick.HitYawDegrees;
	Reaction.BlendInSeconds = ElysiumReactions::FlinchBlendInSeconds;
	Reaction.BlendOutSeconds = ElysiumReactions::FlinchBlendOutSeconds;
	Reaction.bAllowFallbackLadder = false;
	Reaction.Release = EElysiumReactionRelease::Envelope;
	PlayReactionActivity(Reaction);
}

void FElysiumCombatCharacter::ReleaseHeldReaction()
{
	const bool bHadClaim = bHoldsReactionClaim;
	if (!bHadClaim && !HeldReactionPlay.IsValid())
	{
		return;
	}
	// Cleared FIRST, so a release that cannot reach a body still ends the character's own answer to
	// `IsHoldingReaction`. A flinch yielding forever to a claim nothing can give back is the failure
	// this ordering closes. The cached cell goes with it: a released hold is not resumable, and a
	// record left behind would let the next poll re-take a pose whose predicate is over.
	bHoldsReactionClaim = false;
	HeldReactionPlay = FElysiumOneShotClipRequest();
	// Only when a claim was actually outstanding. A hold that had already been DISPLACED owns nothing
	// on the body, and asking the seam to give back a claim it does not hold would report a release
	// that did not happen.
	if (!bHadClaim)
	{
		return;
	}
	if (IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
		Embodiment != nullptr && Visual != nullptr)
	{
		Embodiment->ReleaseNpcReaction(Visual);
	}
}

bool FElysiumCombatCharacter::TickHeldReaction()
{
	if (!HeldReactionPlay.IsValid())
	{
		// Nothing was ever held, or it has been released. Not a state to report — most characters are
		// in it for their whole lives.
		bHoldsReactionClaim = false;
		return false;
	}
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		// The body went away under a standing hold. The claim went with it, so the character's answer
		// has to follow — otherwise the flinch yields forever to a pose nothing is striking.
		bHoldsReactionClaim = false;
		return false;
	}

	const EElysiumHeldReactionState State = Embodiment->QueryNpcReactionHold(Visual);
	if (State == EElysiumHeldReactionState::Held)
	{
		bHoldsReactionClaim = true;
		return true;
	}

	// Displaced, either way: the claim is gone even though the predicate has not moved.
	bHoldsReactionClaim = false;
	if (State == EElysiumHeldReactionState::Displaced)
	{
		// Another producer owns the base channel. Re-claiming now would take it from a reaction that
		// is still playing — an equal band replaces on `>=` — so the resume waits for the channel
		// rather than fighting for it, and the next think asks again. Silent on purpose: a contested
		// channel is an ordinary state, and reporting it once per think would be a log per frame for
		// as long as a button is held.
		return false;
	}

	// The channel is free and the predicate still stands, so the pose goes back on. **The CACHED
	// cell, replayed straight at the play seam** — no `FillActivityClipRequest`, no
	// `ResolveNpcActivityClip`, no weighted pick, and therefore no `Reaction` stream draw.
	if (!Embodiment->PlayNpcOneShot(Visual, HeldReactionPlay, nullptr))
	{
		// The seam refused or the host would not play it — both already reported at their own owner.
		// The cached cell is kept: the predicate has not moved, so the next think tries again.
		return false;
	}
	bHoldsReactionClaim = true;
	UE_LOG(LogElysiumPlayer, Verbose,
		TEXT("%s resumes its held reaction '%s'@'%s' — the base channel came free while the "
			"predicate still stands"),
		*DebugString(), *HeldReactionPlay.AnimationName, *HeldReactionPlay.OwnerStem);
	return true;
}

bool FElysiumCombatCharacter::PlayReactionActivity(const FElysiumReactionPlayRequest& Request,
	float* OutSeconds)
{
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return false;   // no body to react with — an ordinary negative, not a failure
	}

	FElysiumActivityClipRequest Resolve;
	FillActivityClipRequest(Resolve);
	Resolve.Activity = Request.Activity;
	// Retail's `SelectWeightedSequence` picks among the equal activity's variants with `random()`, so
	// the weighted pick re-rolls with every reaction. Off the session's own Reaction stream, whose
	// position is in the save.
	Resolve.Variant = ElysiumRng::Stream(EElysiumRngStream::Reaction).RandHelper(MAX_int32);
	Resolve.HitYaw = Request.HitYawDegrees;
	Resolve.Source = EElysiumAnimSource::Damage;
	// The chain is the BODY's, and the identity is the world's own player handle — the same test the
	// weapon transaction uses. `AsNpc()` would answer differently for a `scripted_character`
	// stand-in, and a scene's `!playercontroller` duplicate is a whole `CNPC_VPlayerController` NPC —
	// both are cast bodies all the same.
	Resolve.BodyKind = (World->PlayerHandle() == Handle)
		? EElysiumAnimBodyKind::Player : EElysiumAnimBodyKind::Cast;
	Resolve.bAllowFallbackLadder = Request.bAllowFallbackLadder;

	FElysiumActivityClip Clip;
	if (!Embodiment->ResolveNpcActivityClip(Resolve, Clip) || Clip.AnimationName.IsEmpty())
	{
		// The named miss already reads on the resolver's own selection record and its Verbose line,
		// with the class body, weapon ladder and alert branch the request selected through. A second
		// report here would be the same fact once per hit.
		return false;
	}

	FElysiumOneShotClipRequest Play;
	Play.OwnerStem = Clip.OwnerStem;
	Play.AnimationName = Clip.AnimationName;
	Play.Label = Clip.Label;
	// The reaction channel, not the one-shot slot: a reaction REPLACES the pose the body is holding,
	// and a directional one is a fan whose pose is a blend of two authored reactions — which is the
	// thing a montage cannot hold. The body's own stem rides along because a fan is reached through
	// the character's vocabulary rather than through the bank alone.
	Play.Route = EElysiumOneShotRoute::Reaction;
	Play.BodyStem = ModelStem();
	// Both answered by the resolve above and never re-derived here: `bGrid` says the label named a fan
	// at all, and the axis value is where on that fan's own parameter it was sampled.
	Play.bGrid = Clip.bGrid;
	Play.AxisValue = Clip.AxisValue;
	// The release condition the producer stated, and the loop that follows from it: a pose held for a
	// whole predicate repeats, a struck reaction plays once. One decision, taken here, so the seam is
	// never handed a held claim that is also a one-shot.
	Play.Release = Request.Release;
	Play.bLoop = Request.Release == EElysiumReactionRelease::Predicate;
	// The recovered restart rule, answered by the seam that resolved the clip: an activity the
	// restart-ideal task routes request re-fires when it is asked for again, instead of being
	// swallowed as an unchanged ideal.
	Play.bRestart = Clip.bRestart;
	// A stated blend wins; otherwise the resolved clip's own authored fade, which is the ordinary
	// sequence-blend rule and already 0 on a `flags & 0x2` hard cut.
	Play.BlendInSeconds = Request.BlendInSeconds >= 0.0f ? Request.BlendInSeconds : Clip.FadeSeconds;
	Play.BlendOutSeconds = Request.BlendOutSeconds >= 0.0f ? Request.BlendOutSeconds : Clip.FadeSeconds;
	Play.Priority = EElysiumAnimPriority::Reaction;
	Play.Source = EElysiumAnimSource::Damage;
	// The seam claims the base channel before it plays, so a body a choreographed scene owns refuses
	// the reaction outright. A refusal is an ordinary negative it reports on its own Verbose line —
	// the reaction simply does not happen, and the scene keeps the body.
	float Seconds = 0.0f;
	const bool bPlayed = Embodiment->PlayNpcOneShot(Visual, Play,
		OutSeconds != nullptr ? &Seconds : nullptr);
	if (bPlayed && OutSeconds != nullptr)
	{
		*OutSeconds = Seconds;
	}
	// A HELD claim the seam accepted is now this character's to give back. A refused one is not: the
	// channel was never taken, so there is nothing outstanding and the flinch must not yield to it.
	//
	// The resolved cell is cached with it, and that cache is the whole of what makes a resume free of
	// a draw: the weighted pick above ran ONCE, and everything after it replays `Play` as written.
	if (bPlayed && Request.Release == EElysiumReactionRelease::Predicate)
	{
		bHoldsReactionClaim = true;
		HeldReactionPlay = Play;
	}
	return bPlayed;
}

// --- `CBaseCombatCharacter::HandleAnimEvent` (`0x1032e330`) ---
//
// The whole recovered body, in retail's own order: the weapon forward first, then the switch, then
// the base handler as `default:`. `docs/vtmb/animation_events.md` -> "Port status — combat
// character band" records which arms are real and which are mocked.

bool FElysiumCombatCharacter::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	if (ElysiumAnimEvents::IsWeaponBand(Event.Event))
	{
		// The active weapon, and only it. Retail reads `m_hActiveWeapon` and calls the virtual on
		// whatever it names, so a holstered weapon whose clip is still running receives nothing.
		FElysiumItem* Held = Inventory.Active(*this);
		FElysiumWeapon* Weapon = Held ? Held->AsWeapon() : nullptr;
		if (!Weapon)
		{
			// Empty-handed, or holding something with no weapon controller (`item_w_unarmed` authors
			// no `Activation` block at all). An ordinary negative: there is nothing to route to, and
			// the census is what records that the id went unclaimed.
			return false;
		}
		return Weapon->OperatorHandleAnimEvent(*this, Event);
	}

	switch (Event.Event)
	{
	case ElysiumFeed::EventFeedTeardown:      // 4006 -> `FeedInterrupt`  (`+0x584`)
	case ElysiumFeed::EventFeedBegin:         // 4007 -> `FeedBegin`      (`+0x57c`)
		return HandleFeedBoundaryAnimEvent(Event.Event);
	case ElysiumAnimEvents::DisciplineCallbackHit:
		return HandleDisciplineAnimEvent(Event.Options);
	case ElysiumAnimEvents::AttachFollowModel:
	case ElysiumAnimEvents::DetachFollowModel:
	case ElysiumAnimEvents::AttachFollowModelGendered:
		return HandleFollowModelAnimEvent(Event);
	default:
		break;
	}
	return FElysiumAnimating::HandleAnimEvent(Event);
}

bool FElysiumCombatCharacter::HandleFeedBoundaryAnimEvent(int32 EventId)
{
	// Retail's guard, both arms: the paired partner handle at `+0x1538` must resolve to a live
	// entity AND the paired role at `+0x153c` must be 0. The port folds retail's paired-action block
	// into `FeedState` (it has no grapple router), so `Peer` is the partner handle and `bVictim` is
	// the role bit — role 1 is the victim half, which is exactly what `1032e5a9`'s `!= 1` test and
	// the `== 0` tests on both arms exclude.
	const bool bPartnerValid = ResolveFeedPeer() != nullptr;
	if (!bPartnerValid || FeedState.bVictim)
	{
		// SWALLOWED, not forwarded. `1032e5b0` and `1032e630` jump straight to the epilogue, so a
		// guard-failed 4006/4007 never reaches `CBaseAnimating::HandleAnimEvent` and never draws its
		// "Unhandled animation event" warning. Claimed, therefore, and kept off the census — the id
		// HAS a handler here; it declined this occurrence.
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s refused feed anim event %d: %s"), *DebugString(), EventId,
			bPartnerValid ? TEXT("this half is the victim (role 1)") : TEXT("no paired partner"));
		return true;
	}
	OnFeedAnimEvent(EventId);
	return true;
}

bool FElysiumCombatCharacter::HandleDisciplineAnimEvent(const FString& Options)
{
	// UNIMPLEMENTED, and NOT a sound.
	//
	// `FUN_101e3e70(&DAT_10739a4c, this, options)`: `options` names a DISCIPLINE record, the manager
	// resolves the name to a bit (`0x101e1590` -> `0x101e1870`), the CHARACTER's own discipline mask
	// at `+0xF34` is tested, and only then does `0x101e3910` select that record's level block for
	// this character's rating (`0x1033d380`/`0x1033d410`) and run its hit callback — announcing
	// itself as `DevMsg(3, "Discipline<%s> CallbackHit", name)`. The one shipped record is
	// `Thaumaturgy_Purge`, so 4020 is where Blood Purge's cast animation COMMITS its effect rather
	// than where it makes a noise.
	//
	// TODO(anim-events): the port commits a discipline from `FElysiumDisciplineState`'s own path,
	// not from the cast clip's timeline. Routing this id there is a re-timing of an existing commit
	// and would double-apply until that path stands down for it, so the seam answers "nothing" and
	// names the retail call it stands for. The id is still CLAIMED, because retail's arm returns
	// without reaching the base handler.
	//
	// Process-wide and once per NAME, the shape the unclaimed-event census has: a gap in the port is
	// one fact about one discipline, and 4020 re-fires on every cast.
	static TSet<FString> Reported;
	if (!Reported.Contains(Options))
	{
		Reported.Add(Options);
		UE_LOG(LogElysiumPlayer, Warning,
			TEXT("UNIMPLEMENTED anim event 4020 discipline callback '%s' on '%s': retail 0x101e3e70 "
			     "would run that discipline's level-block hit callback on this character"),
			*Options, *DebugString());
	}
	return true;
}

bool FElysiumCombatCharacter::HandleFollowModelAnimEvent(const FElysiumAnimEvent& Event)
{
	// Retail's order, and it is load-bearing: the standing follow model is removed FIRST and
	// unconditionally (`1032e40a` for 4100/4102, `1032e4c6` for 4101), before the path is formatted
	// and before anything can fail. Every way to leave this function with no model created ends at
	// the same `LAB_1032e4ce` — the handle set back to `0xffffffff`.
	//
	// The removal is NOT issued as a separate `DetachOrnamentModel` ahead of an attach. The seam's
	// attach contract is "replace" — it removes first, unconditionally, and its same-path no-op
	// (the named modernization in `ElysiumNpcVisual::InstallOrnamentModel`) can only see a standing
	// component if this handler has not already swept it. The slot itself is reset up front either
	// way: retail writes the handle on every exit — the created entity's at `1032e486`, `0xffffffff`
	// at `1032e4ce` — so the old value never survives the arm.
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	const bool bBodied = Embodiment != nullptr && Visual != nullptr;
	AnimFollowModel.Reset();

	// `IsMale` (`0x10336920`) reads the character's own stat slot 11 — the Gender attribute — and
	// answers true on exactly 1. `FElysiumSheet::IsMale` is that same slot. An empty `options`
	// formats nothing: retail would build `".mdl"`, fail `GetModelPtr` and take the failure tail,
	// which is the same remove-and-clear a 4101 is.
	const FString Path = Event.Event == ElysiumAnimEvents::DetachFollowModel
		? FString()
		: ElysiumAnimEvents::FormatFollowModelPath(Event.Event, Event.Options, Sheet.IsMale());
	if (!bBodied)
	{
		// A bodiless character, or a headless world. The slot is what the transaction is; there is
		// simply nothing to hang it on, which is the supported negative and not a failure.
		return true;
	}
	if (Path.IsEmpty())
	{
		Embodiment->DetachOrnamentModel(Visual);
		return true;
	}
	if (Embodiment->AttachOrnamentModel(Visual, Path))
	{
		AnimFollowModel = Path;
	}
	// Else: the seam removed what was worn, reported its own gap once per path, and the slot stays
	// empty — retail's `DevMsg("Could not create ornament prop model: %s")` tail, which also leaves
	// the handle unset.
	return true;
}

// --- The pending eye-angle snap — `FUN_10178590` / `FUN_10178550` (RC4, SC4) ---

void FElysiumCombatCharacter::LookAtWorldPoint(const FVector& WorldPoint)
{
	// `FUN_10178590`, verbatim: the eye position (vfunc `0x304`), the normalized direction to the
	// point, `VectorAngles`, and the result handed to `FUN_10178550`. Retail normalizes before
	// `VectorAngles`, which the angle solve does not need but which is what makes a zero-length
	// direction produce `(0,0,0)` rather than a NaN — so the degenerate case is guarded here too.
	const FVector Direction = WorldPoint - EyePosition();
	const FRotator EyeAngles = Direction.IsNearlyZero()
		? FRotator::ZeroRotator : Direction.GetSafeNormal().Rotation();
	SetPendingEyeAngles(EyeAngles, WorldPoint);
}

void FElysiumCombatCharacter::SetPendingEyeAngles(const FRotator& InAngles, const FVector& LookPoint)
{
	// `FUN_10178550`: `+0x206c..0x2074 = ang; +0x207c = 1;` — and nothing else. The flag is what the
	// usercmd drain reads, so raising it is the whole of the write.
	PendingEyeAngles = InAngles;
	PendingEyeLookPoint = LookPoint;
	bPendingEyeAngleSnap = true;
	OnPendingEyeAnglesRaised();
}

bool FElysiumCombatCharacter::ConsumePendingEyeAngleSnap(FRotator& OutAngles, FVector& OutLookPoint)
{
	if (!bPendingEyeAngleSnap)
	{
		return false;
	}
	OutAngles = PendingEyeAngles;
	OutLookPoint = PendingEyeLookPoint;
	bPendingEyeAngleSnap = false;
	return true;
}

// --- The grapple pair — `+0x1534`..`+0x1558` (RC13) ---
//
// One transaction in, one transaction out, and nothing in between: retail has **exactly three
// writers** of this block in the whole image — `EnterGrappleState` `0x10329760`,
// `LeaveGrappleState` `0x10329a70` and the `CBaseCombatCharacter` constructor `0x10326de0` — so
// there is no partial state and no per-frame maintenance to reproduce. Every reader (the camera
// anchor resolve `FUN_1006e130`, `CanStartGrappleAttack`, `CPlayerMove::SetupMove`,
// `GetSaveBlockedReason`, `ChooseMeleeAttackSequence`, the two player anim-state selectors) only
// reads.

FElysiumCombatCharacter* FElysiumCombatCharacter::ResolveGrapplePartner() const
{
	FElysiumEntity* Ent = World ? World->Resolve(Grapple.Partner) : nullptr;
	return Ent ? Ent->AsCombatCharacter() : nullptr;
}

bool FElysiumCombatCharacter::EnterGrappleState(const FElysiumEntityHandle& Partner,
	EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	Grapple.Partner = Partner;
	Grapple.Role = Role;
	Grapple.Type = Type;
	Grapple.Position = Position;
	Grapple.EnterOrigin = Origin;                  // `+0x1548..0x1550 = GetAbsOrigin()`
	Grapple.bHolsteredOnEnter = bHolster;
	if (Type == EElysiumGrappleType::StealthKill)
	{
		FElysiumItem* Item = Inventory.Active(*this);
		FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr;
		Grapple.bHolsteredOnEnter = bHolster && Weapon && !Weapon->IsHidden();
		if (Grapple.bHolsteredOnEnter) Weapon->Hide();
	}
	// `+0x1558 = (role != 0) ? partner : -1` — only the victim points at its attacker, because the
	// attacker is the half that drives the paired animation.
	Grapple.AnimDriver = (Role != EElysiumGrappleRole::Attacker)
		? Partner : FElysiumEntityHandle::Invalid();

	// `CBasePlayer::FUN_101695f0`, the player's grapple *enter*, raises the pose lock: while the
	// body is posed by the paired animation the move takes its angles from the entity rather than
	// from the eye (`CPlayerMove::SetupMove` `0x10186120`, RC4). It is a player-only write — an NPC
	// has no `m_iVFlags` — so this arm runs only when the character being entered is the player.
	if (FElysiumPlayer* PlayerEnt = AsGrapplingPlayer())
	{
		PlayerEnt->AddViewFlags(EElysiumViewFlags::MoveAnglesFromEntity);
	}
	return true;
}

void FElysiumCombatCharacter::LeaveGrappleState()
{
	if (Grapple.Type == EElysiumGrappleType::StealthKill)
	{
		if (Grapple.bHolsteredOnEnter)
		{
			FElysiumItem* Item = Inventory.Active(*this);
			if (FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr) Weapon->Unhide();
		}
		if (Grapple.bOwnsStealthAction)
		{
			ReleaseAnimSegment();
			if (!HasReportedDeath())
			{
				SetBodyFrozen(false);
				ResetAnimToIdle();
			}
			NextThink = float(World ? World->NowSeconds() : 0.0);
		}
	}
	// Every field back to retail's `-1`, in one transaction. The exit placement retail computes
	// from `+0x1548` (and, for `role == 0 && (type == 1 || type == 4)`, from the *partner's* saved
	// origin) is the movement half and belongs with whoever placed the bodies; the port's feed pair
	// leaves both origins untouched, which is the divergence `ElysiumFeed.cpp` already records.
	Grapple = FElysiumGrappleState();

	if (FElysiumPlayer* PlayerEnt = AsGrapplingPlayer())
	{
		// `CBasePlayer::LeaveGrappleState` `0x10169660`, the override, does two things beyond the
		// base transaction: it drops the pose lock, and it calls **`SetCineCamera(NULL)`**.
		PlayerEnt->RemoveViewFlags(EElysiumViewFlags::MoveAnglesFromEntity);
		// **RC13 — ending a grapple ends the scripted shot.** The camera the feed/stealth-kill shot
		// was running on is destroyed with it when it is disposable, and the release is a cut (M1):
		// the player's own eye is the next frame's view.
		if (World)
		{
			World->ClearScriptedCamera();
		}
	}
}

FElysiumPlayer* FElysiumCombatCharacter::AsGrapplingPlayer()
{
	// The `m_iVFlags` half of the grapple transaction is `CBasePlayer`'s override, not
	// `CBaseCombatCharacter`'s body, so it must not run for an NPC half of the same pair.
	FElysiumPlayer* PlayerEnt = World ? World->FindPlayer() : nullptr;
	return static_cast<FElysiumCombatCharacter*>(PlayerEnt) == this ? PlayerEnt : nullptr;
}

bool FElysiumCombatCharacter::EnterGrapplePair(FElysiumCombatCharacter& Victim,
	EElysiumGrappleType Type, int32 Position)
{
	if (&Victim == this || !Handle.IsSet() || !Victim.Handle.IsSet())
	{
		return false;
	}
	// The attacker first, then the victim — `StartGrappleAttack`'s two `EnterGrappleState`
	// dispatches in their own order (`0x10329284`, `0x103292d2`), with the attacker rolled back if
	// the victim refuses. The per-mode holster asymmetry is retail's: mode 3 (the stealth kill)
	// holsters the victim only, modes 4 and 7 neither party, everything else both.
	const bool bHolsterAttacker = Type != EElysiumGrappleType::StealthKill
		&& Type != EElysiumGrappleType::StealthKillTwin
		&& Type != EElysiumGrappleType::PayphoneVariant;
	const bool bHolsterVictim = Type != EElysiumGrappleType::StealthKillTwin
		&& Type != EElysiumGrappleType::PayphoneVariant;

	if (!EnterGrappleState(Victim.Handle, EElysiumGrappleRole::Attacker, Type, Position, bHolsterAttacker))
		return false;
	if (!Victim.Handle.IsSet())
	{
		LeaveGrappleState();   // the roll-back arm
		return false;
	}
	if (!Victim.EnterGrappleState(Handle, EElysiumGrappleRole::Victim, Type, Position, bHolsterVictim))
	{
		LeaveGrappleState();
		return false;
	}
	if (World && Victim.AsNpc() && (Type == EElysiumGrappleType::Feed || Type == EElysiumGrappleType::FeedVariant))
	{
		// StartGrappleAttack -> victim GrappleSoundCmd(0) 0x1033b100, once at engagement.
		World->EmitGameSound(Victim.Origin, ElysiumGameSounds::Feed(), -1.f, Victim.Handle,
			ElysiumStealth::HearingReductionCmFor(&Victim), ElysiumGameSounds::Danger, 0.2);
	}
	return true;
}

void FElysiumCombatCharacter::LeaveGrapplePair()
{
	// `EndGrapple` `0x10329560`: the partner leaves first, then this one, so a half that is already
	// clear costs nothing and a re-entrant call is inert.
	if (FElysiumCombatCharacter* Partner = ResolveGrapplePartner())
	{
		Partner->LeaveGrappleState();
	}
	LeaveGrappleState();
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

// The PLAYER's death tail (and any non-NPC character's). An NPC's death is slot 144 since story 8
// wave 2: the Troika / `CAI_BaseNPC::Event_Killed` bodies fire `OnDeath` with the packet's attacker
// (`0x10265a90`) and reach `CBaseCombatCharacter::Event_Killed` `0x1032b9b0` above.
void FElysiumCombatCharacter::OnKilled()
{
	if (bDeathReported)
	{
		return;
	}
	bDeathReported = true;
	// The held reaction claim goes back on the death commit, ahead of every output. A held
	// claim is released by a predicate its producer re-checks, and a dead character re-checks nothing:
	// leaving it standing parks the base channel of a body whose death schedule is about to ask for
	// it. The NPC leaf's wholesale `ReleaseBodyAnimClaims` releases the driver slot too; this is what
	// makes the character's own answer to `IsHoldingReaction` agree with it, on both leaves.
	ReleaseHeldReaction();
	static const FName OnDeath(TEXT("OnDeath"));
	RemoveFromComfortList(); // Event_Killed 0x1032b9b0 removes one occurrence, including duplicates.
	FireOutput(OnDeath, Handle);   // one of CAI_BaseNPC's 16 outputs; the player wires none
	// Preserve producer order: the child's own OnDeath rows enter the queue before its maker's
	// OnNPCDied rows. Retail's relative order is still an open live-capture question; this is the
	// existing producer first, followed by the newly recovered owner notification.
	NotifyOwnerOfTermination(EElysiumOwnedEntityTermination::Died);
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s died"), *DebugString());
}

bool FElysiumCombatCharacter::GetDynamicField(FName Name, FElysiumVariant& Out) const
{
	// Every compiled slot is a registered field, so the class-chain walk has already answered by the time
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
	// `m_iMiscFlags`, the persistent word `AddMiscFlag` ORs into: the discipline tables' misc
	// masks and the touch handler's `Obf_Bumped_Object 0x100`.
	Out.Emplace(TEXT("Misc flags"), FString::Printf(TEXT("0x%x"), MiscFlags));
	Out.Emplace(TEXT("WillTalk"), bWillTalk ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Disposition"), Disposition.IsEmpty() ? TEXT("(none)") : Disposition);
	// The discipline block: which of the thirteen are active, and how many targeted
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
