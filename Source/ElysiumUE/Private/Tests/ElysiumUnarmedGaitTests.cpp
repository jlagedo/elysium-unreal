#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h"
#include "ElysiumGaitSpeeds.h"               // the per-direction speed table the fan fills
#include "ElysiumNativeCharacterTestData.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumNpcClips.h"

// The unarmed cast gait.
//
// `PreTranslate_Human` (`vampire.dll 0x103854f0`) carries two rows that fire under `NotArmedAlert`:
// `ACT_WALK` -> `ACT_WALK_RELAXED` (9 -> 22) and `ACT_RUN` -> `ACT_RUN_RELAXED` (19 -> 23). An
// unarmed body satisfies that predicate unconditionally -- the recovered tree early-outs on "no
// active weapon" before it reads any state -- and **no shipped body carries either sequence**;
// the census over `out/npc/clips` finds `ACT_WALK_RELAXED` on none of the 293 exported bodies.
//
// So on every unarmed cast body the only thing that hands the gait back is the four-way
// availability probe at the tail of `CAI_BaseNPC::TranslateActivity` (`0x10271ff0`), whose rung 4
// is the untranslated request. That probe is unconditional in retail: its one early return is
// `ACT_SCRIPT_CUSTOM_MOVE` (`0x18`), and nothing else skips it.
//
// The distinction these two tests hold is that the probe never crosses activities -- its four rungs
// are the same request as the weapon table left it, as the class table left it, as the first weapon
// pass left it, and untranslated -- while three other rungs do: the `ACT_RUN` -> `ACT_WALK` last
// resort (`piStack_4 == 0x13` -> `9`), the ACT_DISPOSITION retry and sequence zero. A speed reader
// wants the first and must refuse the second, which is `bAllowSubstituteActivity`.

static constexpr EAutomationTestFlags GElysiumUnarmedGaitFixtureFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The substitution gate, on a fixture, both ways.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGaitSubstitutionGateTest,
	"Elysium.Substrate.GaitSubstitutionGate", GElysiumUnarmedGaitFixtureFlags)
bool FElysiumGaitSubstitutionGateTest::RunTest(const FString&)
{
	// A body carrying a walk and no run at all: retail's last resort is the only thing that can
	// answer an `ACT_RUN` request on it, so the gate's two arms are separable by construction.
	FElysiumNpcClipSet Body;
	Body.Stem = TEXT("gate_body");
	FElysiumNpcClip Walk;
	Walk.Owner = TEXT("gate_bank");
	Walk.Activity = TEXT("ACT_WALK");
	Walk.Weight = 1;
	Walk.Flags = 0x1;
	Walk.Frames = 30;
	Walk.Fps = 30.0f;
	Body.Clips.Add(TEXT("Walk"), Walk);

	FElysiumAnimationCatalog Catalog;
	Catalog.Clips = &Body;
	Catalog.BlendTableFor = [](const FString&) -> const FElysiumBlendTable* { return nullptr; };

	auto ResolveRun = [&Catalog, &Body](bool bSubstitute)
	{
		FElysiumAnimationIntent Intent;
		Intent.Stem = Body.Stem;
		Intent.Activity = TEXT("ACT_RUN");
		Intent.Source = EElysiumAnimSource::Npc;
		Intent.BodyKind = EElysiumAnimBodyKind::Cast;
		Intent.bAllowFallbackLadder = true;
		Intent.bAllowSubstituteActivity = bSubstitute;
		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		return Out;
	};

	const FElysiumAnimationSelection Substituted = ResolveRun(true);
	TestEqual(TEXT("the per-frame publish takes retail's ACT_RUN -> ACT_WALK last resort"),
		Substituted.ResolvedActivity, FString(TEXT("ACT_WALK")));

	const FElysiumAnimationSelection Refused = ResolveRun(false);
	TestNotEqual(TEXT("a speed reader refuses it rather than publishing walk speeds as run"),
		Refused.ResolvedActivity, FString(TEXT("ACT_WALK")));

	// The four rungs above it are not substitutions and must survive the cleared flag. This is the
	// exact shape of the defect: an unarmed cast body whose class pre-translates the walk into a
	// relaxed walk it does not carry, rescued by rung 4 and nothing else.
	FElysiumAnimationIntent Unarmed;
	Unarmed.Stem = Body.Stem;
	Unarmed.Activity = TEXT("ACT_WALK");
	Unarmed.Source = EElysiumAnimSource::Npc;
	Unarmed.BodyKind = EElysiumAnimBodyKind::Cast;
	Unarmed.ActorClassname = TEXT("npc_VHumanCombatant");
	Unarmed.ActorState = EElysiumNpcState::Idle;
	Unarmed.bAllowFallbackLadder = true;
	Unarmed.bAllowSubstituteActivity = false;
	FElysiumAnimationSelection Probed;
	ElysiumAnimResolve::Resolve(Unarmed, Catalog, Probed);
	TestEqual(TEXT("the availability probe still hands an unarmed body its plain walk back"),
		Probed.ResolvedActivity, FString(TEXT("ACT_WALK")));
	TestFalse(TEXT("...naming a real label"), Probed.SequenceLabel.IsEmpty());
	TestTrue(TEXT("...through a rung of the probe rather than the translation's own answer"),
		Probed.AvailabilityRung > 1);
	return true;
}


// The same claim against the real corpus, on the bodies `sp_tutorial_1` actually stands.
//
// A fixture proves the rule; it cannot fail when the export regresses. Every stem below resolved
// NOTHING and commanded zero gait speed while the per-frame publish, which keeps the probe, posed
// the plain walk it had -- the anchor case being `malkavian_female_armor_0`, the player's own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUnarmedCastGaitFanTest,
	"Elysium.Content.UnarmedCastGaitFan", GElysiumUnarmedGaitFixtureFlags)
bool FElysiumUnarmedCastGaitFanTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!ElysiumNativeTest::Load(Index, Error) || !Index.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native cast view (run: uv run elysium import characters)"));
		return true;
	}

	// A blend-table lookup over the real sidecars, cached per owner. The owner may be a character or
	// a bank, so both halves of the index are consulted -- that is the include DAG's own answer.
	TMap<FString, TSharedPtr<FElysiumBlendTable>> Cache;
	auto TableFor = [&Index, &Cache](const FString& Owner) -> const FElysiumBlendTable*
	{
		if (const TSharedPtr<FElysiumBlendTable>* Found = Cache.Find(Owner))
		{
			return Found->Get();
		}
		const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Owner);
		if (Entry == nullptr) { Entry = Index.Banks.Find(Owner); }
		TSharedPtr<FElysiumBlendTable> Table;
		if (Entry != nullptr && !Entry->Blends.IsEmpty())
		{
			Table = MakeShared<FElysiumBlendTable>();
			FString LoadError;
			if (!ElysiumNativeTest::Load(*Table, Entry->Blends, LoadError))
			{
				Table.Reset();
			}
		}
		Cache.Add(Owner, Table);
		return Table.Get();
	};

	// The bodies the tutorial stands, with the entity classname each is stood under. The classname
	// is load-bearing: it is what finds the `+0x5dc`/`+0x5e0` class bodies whose pre-translation
	// produces the relaxed request in the first place, so a sweep that left it empty would drive
	// none of the work this asserts. A stem the export does not carry is skipped rather than
	// failed -- which corpus a machine has is the owner's business.
	struct FStand { const TCHAR* Stem; const TCHAR* Classname; };
	static const FStand Stands[] = {
		{ TEXT("malkavian_female_armor_0"),  TEXT("npc_VPlayerController") },
		{ TEXT("smiling_jack"),              TEXT("npc_VVampire")          },
		{ TEXT("sheriff"),                   TEXT("npc_VVampire")          },
		{ TEXT("shovelhead"),                TEXT("npc_VVampire")          },
		{ TEXT("sabbat_henchman"),           TEXT("npc_VVampire")          },
		{ TEXT("gangmember_male_2"),         TEXT("npc_VHumanCombatant")   },
		{ TEXT("gangmember_male_2_alt"),     TEXT("npc_VHumanCombatant")   },
		{ TEXT("buch"),                      TEXT("npc_VHumanCombatant")   },
		{ TEXT("vdor"),                      TEXT("npc_VHumanCombatant")   },
		{ TEXT("average_vampire_hunter"),    TEXT("npc_VHumanCombatant")   },
		{ TEXT("elite_hunter"),              TEXT("npc_VHumanCombatant")   },
		{ TEXT("vampire_hunter_chick"),      TEXT("npc_VHumanCombatant")   },
		{ TEXT("blueblood_male"),            TEXT("npc_VPedestrian")       },
	};

	int32 Measured = 0;
	int32 Skipped = 0;
	for (const FStand& Stand : Stands)
	{
		FElysiumNpcClipSet Body;
		FString LoadError;
		if (!ElysiumNativeTest::Load(Body, Stand.Stem, LoadError))
		{
			++Skipped;
			continue;
		}
		// The premise, asserted rather than assumed. If a body genuinely declared no walk then the
		// miss would be an authored absence and the runtime's report would be honest.
		if (!Body.HasActivity(TEXT("ACT_WALK")) || !Body.HasActivity(TEXT("ACT_RUN")))
		{
			AddError(FString::Printf(TEXT("'%s' carries no plain ACT_WALK/ACT_RUN to resolve"),
				Stand.Stem));
			continue;
		}
		TestFalse(*FString::Printf(
			TEXT("'%s' carries no ACT_WALK_RELAXED, so only the probe can answer"), Stand.Stem),
			Body.HasActivity(TEXT("ACT_WALK_RELAXED")));

		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Body;
		Catalog.BlendTableFor = TableFor;

		for (const TCHAR* Activity : { TEXT("ACT_WALK"), TEXT("ACT_RUN") })
		{
			// The gait seam's own request, spelled here rather than reached through the subsystem:
			// the subsystem needs a game instance and this claim needs none.
			FElysiumAnimationIntent Intent;
			Intent.Stem = Body.Stem;
			Intent.Activity = Activity;
			Intent.Source = EElysiumAnimSource::Npc;
			Intent.BodyKind = EElysiumAnimBodyKind::Cast;
			Intent.ActorClassname = Stand.Classname;
			Intent.ActorState = EElysiumNpcState::Idle;   // state 0, as a body stands at load
			Intent.bAllowFallbackLadder = true;
			Intent.bAllowSubstituteActivity = false;

			FElysiumAnimationSelection Out;
			ElysiumAnimResolve::Resolve(Intent, Catalog, Out);

			TestTrue(*FString::Printf(TEXT("%s '%s' resolves a label"), Stand.Stem, Activity),
				!Out.SequenceLabel.IsEmpty() && !Out.OwnerStem.IsEmpty());
			// And it resolves as ITSELF. A body answering ACT_DISPOSITION here would be the
			// substitution the cleared flag exists to refuse, not the gait.
			TestEqual(*FString::Printf(TEXT("%s '%s' resolves as itself, not a substitute"),
				Stand.Stem, Activity), Out.ResolvedActivity, FString(Activity));
			++Measured;

			// And the speed the seam publishes off it. The fan belongs to the bank the pick landed
			// in, not to the body -- one body's walk and its run routinely come from different
			// banks -- so it is fetched by the selection's own owner, exactly as `ResolveGaitSpeeds`
			// does. A body whose selected clip carries no grid is the separate, still-unported
			// non-fanned ground-speed path and is reported rather than failed here.
			if (Out.OwnerStem.IsEmpty()) { continue; }
			const FElysiumBlendTable* Owner = TableFor(Out.OwnerStem);
			const FElysiumBlendGrid* Grid = Owner != nullptr ? Owner->Find(Out.SequenceLabel) : nullptr;
			if (Grid == nullptr)
			{
				AddInfo(FString::Printf(
					TEXT("%s '%s' selected '%s'@'%s', which carries no grid (non-fanned clip)"),
					Stand.Stem, Activity, *Out.SequenceLabel, *Out.OwnerStem));
				continue;
			}
			FElysiumGaitSpeedTable Fan;
			const bool bFan = ElysiumBlendGrids::SpeedFan(*Grid, *Owner, 1.0f, Fan);
			TestTrue(*FString::Printf(TEXT("%s '%s' produces a speed fan"), Stand.Stem, Activity),
				bFan && Fan.IsValid());
			TestTrue(*FString::Printf(TEXT("%s '%s' commands a forward speed above zero"),
				Stand.Stem, Activity), Fan.Forward() > 0.0f);
		}
	}

	TestTrue(TEXT("at least one stood body was measured"), Measured > 0);
	AddInfo(FString::Printf(TEXT("%d unarmed cast gaits resolved over %d stands (%d absent)"),
		Measured, static_cast<int32>(UE_ARRAY_COUNT(Stands)), Skipped));
	return true;
}


// The non-fanned half of retail's one speed pipeline.
//
// `ResetSequenceInfo` (`0x10090950`) calls `GetSequenceGroundSpeed` (`0x10091490`) for every
// sequence it commits; that is `GetSequenceMoveDist` (`0x1008fbe0`, the magnitude of
// `GetSequenceLinearMotion`) over `SequenceDuration`. The mover underneath (`0x100c5d10`)
// accumulates `weight * motion` across four bilinear corners, and `0x100c1c60` returns fraction 0
// and index 0 the instant `poseparamindex[axis]` reads `-1` -- so for a sequence that binds no pose
// parameter the weights are `{1,0,0,0}` and the speed is one number no direction can change.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFlatGaitFanTest,
	"Elysium.Substrate.FlatGaitFan", GElysiumUnarmedGaitFixtureFlags)
bool FElysiumFlatGaitFanTest::RunTest(const FString&)
{
	// A rat's walk, as the corpus authors it: 11 frames at 30 fps over 71.9109 cm.
	FElysiumClipMotion Motion;
	Motion.CycleSeconds = 10.0f / 30.0f;
	Motion.GroundDistanceCm = 71.9109f;
	Motion.GroundSpeedCmPerSecond = Motion.GroundDistanceCm / Motion.CycleSeconds;

	FElysiumGaitSpeedTable Flat;
	TestTrue(TEXT("a clip with authored motion fills a fan"),
		ElysiumBlendGrids::FlatFan(Motion, 1.0f, Flat));
	// `IsValid` refuses a single-cell table, which is why the flat answer is spread over the cells
	// rather than written once.
	TestTrue(TEXT("...that the consumer accepts"), Flat.IsValid());
	TestEqual(TEXT("...spanning the shipped move_yaw geometry"), Flat.Count,
		FElysiumGaitSpeedTable::MaxCells);

	// The claim itself: no direction can change it.
	const float Forward = Flat.Forward();
	TestTrue(TEXT("the forward speed is the authored one"),
		FMath::IsNearlyEqual(Forward, Motion.GroundSpeedCmPerSecond, 0.01f));
	for (const float Yaw : { -180.0f, -90.0f, -22.5f, 0.0f, 45.0f, 90.0f, 179.0f })
	{
		TestTrue(*FString::Printf(TEXT("...and it is the same at %.1f degrees"), Yaw),
			FMath::IsNearlyEqual(Flat.SpeedAt(Yaw), Forward, 0.01f));
	}
	TestTrue(TEXT("the peak is that same speed, so nothing reads it as a ceiling to clamp under"),
		FMath::IsNearlyEqual(Flat.Peak(), Forward, 0.01f));

	// Symmetrize is what the gait seam runs over every table; on a flat one it must change nothing.
	FElysiumGaitSpeedTable Symmetric = Flat;
	Symmetric.Symmetrize();
	TestTrue(TEXT("symmetrizing a flat fan is a no-op"),
		FMath::IsNearlyEqual(Symmetric.Forward(), Forward, 0.01f));

	// The gait's own multiplier still applies, exactly as it does to a fanned table.
	FElysiumGaitSpeedTable Scaled;
	TestTrue(TEXT("the scale is carried, not folded"),
		ElysiumBlendGrids::FlatFan(Motion, 2.3f, Scaled));
	TestTrue(TEXT("...so a sneak commands 2.3x the authored speed"),
		FMath::IsNearlyEqual(Scaled.Forward(), Forward * 2.3f, 0.01f));

	// A sequence that authors no movement is retail's zero, and it is refused rather than published.
	FElysiumClipMotion Motionless;
	Motionless.CycleSeconds = 1.0f;
	FElysiumGaitSpeedTable Refused;
	TestFalse(TEXT("a clip with no authored movement fills no fan"),
		ElysiumBlendGrids::FlatFan(Motionless, 1.0f, Refused));
	TestFalse(TEXT("...and leaves the table absent rather than zeroed-but-present"),
		Refused.IsValid());
	return true;
}


// The same claim against the real corpus, plus the no-regression half.
//
// `monster/rat/rat` is the clean case: a standalone model with no include banks whose `rat_walk`
// and `rat_run` are plain 11-frame clips carrying authored movement and no grid at all.
// `npc/unique/downtown/sheriff/sheriff` is the case this does NOT fix, and the assertion says so:
// its local `Run` authors no movement, so retail's own ground speed for it is zero.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNonFannedGaitSpeedTest,
	"Elysium.Content.NonFannedGaitSpeed", GElysiumUnarmedGaitFixtureFlags)
bool FElysiumNonFannedGaitSpeedTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!ElysiumNativeTest::Load(Index, Error) || !Index.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native cast view (run: uv run elysium import characters)"));
		return true;
	}

	TMap<FString, TSharedPtr<FElysiumBlendTable>> Cache;
	auto TableFor = [&Index, &Cache](const FString& Owner) -> const FElysiumBlendTable*
	{
		if (const TSharedPtr<FElysiumBlendTable>* Found = Cache.Find(Owner)) { return Found->Get(); }
		const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Owner);
		if (Entry == nullptr) { Entry = Index.Banks.Find(Owner); }
		TSharedPtr<FElysiumBlendTable> Table;
		if (Entry != nullptr && !Entry->Blends.IsEmpty())
		{
			Table = MakeShared<FElysiumBlendTable>();
			FString LoadError;
			if (!ElysiumNativeTest::Load(*Table, Entry->Blends, LoadError)) { Table.Reset(); }
		}
		Cache.Add(Owner, Table);
		return Table.Get();
	};

	// The gait seam's whole answer for one body and one activity, run exactly as
	// `UElysiumAnimSubsystem::ResolveGaitSpeeds` runs it: resolve, then ask the owner for a grid,
	// then for a scalar motion. Returns false when the gait commands zero, which is a real answer.
	auto GaitSpeed = [&TableFor](const FElysiumNpcClipSet& Set, const TCHAR* Activity,
		const TCHAR* Classname, float& OutForward, FString& OutLabel, FString& OutOwner) -> bool
	{
		OutForward = 0.0f;
		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Set;
		Catalog.BlendTableFor = TableFor;

		FElysiumAnimationIntent Intent;
		Intent.Stem = Set.Stem;
		Intent.Activity = Activity;
		Intent.Source = EElysiumAnimSource::Npc;
		Intent.BodyKind = EElysiumAnimBodyKind::Cast;
		Intent.ActorClassname = Classname;
		Intent.ActorState = EElysiumNpcState::Idle;
		Intent.bAllowFallbackLadder = true;
		Intent.bAllowSubstituteActivity = false;

		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		OutLabel = Out.SequenceLabel;
		OutOwner = Out.OwnerStem;
		if (Out.SequenceLabel.IsEmpty() || Out.OwnerStem.IsEmpty()) { return false; }

		const FElysiumBlendTable* Owner = TableFor(Out.OwnerStem);
		if (Owner == nullptr) { return false; }
		FElysiumGaitSpeedTable Fan;
		const FElysiumBlendGrid* Grid = Owner->Find(Out.SequenceLabel);
		const bool bFilled = Grid != nullptr
			? ElysiumBlendGrids::SpeedFan(*Grid, *Owner, 1.0f, Fan)
			: [&]{ const FElysiumClipMotion* M = Owner->FindMotion(Out.SequenceLabel);
			       return M != nullptr && ElysiumBlendGrids::FlatFan(*M, 1.0f, Fan); }();
		if (!bFilled) { return false; }
		OutForward = Fan.Forward();
		return true;
	};

	// --- The rat: the case this ports ----------------------------------------------------------
	FElysiumNpcClipSet Rat;
	FString LoadError;
	if (ElysiumNativeTest::Load(Rat, TEXT("rat"), LoadError))
	{
		for (const TCHAR* Activity : { TEXT("ACT_WALK"), TEXT("ACT_RUN") })
		{
			float Forward = 0.0f;
			FString Label, Owner;
			const bool bMoved = GaitSpeed(Rat, Activity, TEXT("npc_VRat"), Forward, Label, Owner);
			TestTrue(*FString::Printf(TEXT("rat %s commands a speed"), Activity), bMoved);
			TestTrue(*FString::Printf(TEXT("rat %s commands MORE than zero (got %.2f cm/s)"),
				Activity, Forward), Forward > 0.0f);
			// The label carries no grid -- if it ever gains one this test is measuring the fanned
			// path instead and its premise is gone, so the shape is asserted rather than assumed.
			const FElysiumBlendTable* Table = Owner.IsEmpty() ? nullptr : TableFor(Owner);
			TestTrue(*FString::Printf(TEXT("rat %s resolved a non-fanned label ('%s')"),
				Activity, *Label), Table != nullptr && Table->Find(Label) == nullptr);
			AddInfo(FString::Printf(TEXT("rat %s -> '%s'@'%s' at %.2f cm/s"),
				Activity, *Label, *Owner, Forward));
		}
	}
	else
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the export carries no rat"));
	}

	// --- A fanned body: the no-regression half -------------------------------------------------
	// A body whose walk resolves into the shared `move_and_ranged` bank still reads its 9-cell fan,
	// and reads it through `SpeedFan` rather than the new arm.
	FElysiumNpcClipSet Player;
	if (ElysiumNativeTest::Load(Player, TEXT("malkavian_female_armor_0"), LoadError))
	{
		for (const TCHAR* Activity : { TEXT("ACT_WALK"), TEXT("ACT_RUN") })
		{
			float Forward = 0.0f;
			FString Label, Owner;
			const bool bMoved = GaitSpeed(Player, Activity, TEXT("npc_VPlayerController"),
				Forward, Label, Owner);
			TestTrue(*FString::Printf(TEXT("the fanned path still answers %s"), Activity), bMoved);
			TestTrue(*FString::Printf(TEXT("...with a real speed (%.2f cm/s)"), Forward),
				Forward > 0.0f);
			const FElysiumBlendTable* Table = Owner.IsEmpty() ? nullptr : TableFor(Owner);
			TestTrue(*FString::Printf(TEXT("...off an actual grid ('%s'@'%s')"), *Label, *Owner),
				Table != nullptr && Table->Find(Label) != nullptr);
		}
	}

	// --- The sheriff: the case this does NOT port, stated rather than worked around -------------
	// Its local `Run` (flags `1`, no `0x80`) shadows the shared bank's fanned one under
	// `Studio_GetSequencesForActivity` (`0x10427df0`), and that local sequence authors no movement
	// at all -- so retail's `Studio_AnimMovement` returns false and `m_flGroundSpeed` is zero. The
	// zero is the retail answer for the clip we land on; whether retail lands on that clip is the
	// unmodelled `0x80` rule's business, not this one's.
	FElysiumNpcClipSet Sheriff;
	if (ElysiumNativeTest::Load(Sheriff, TEXT("sheriff"), LoadError))
	{
		float Forward = 0.0f;
		FString Label, Owner;
		const bool bMoved = GaitSpeed(Sheriff, TEXT("ACT_RUN"), TEXT("npc_VVampire"),
			Forward, Label, Owner);
		const FElysiumBlendTable* Table = Owner.IsEmpty() ? nullptr : TableFor(Owner);
		const bool bLocalRun = Owner.Contains(TEXT("sheriff"));
		if (bLocalRun)
		{
			TestNull(TEXT("the sheriff's own Run declares no grid"),
				Table != nullptr ? Table->Find(Label) : nullptr);
			TestNull(TEXT("...and no authored movement, which is retail's own zero"),
				Table != nullptr ? Table->FindMotion(Label) : nullptr);
			TestFalse(TEXT("so its run commands zero, faithfully"), bMoved);
			AddInfo(TEXT("sheriff ACT_RUN: zero is retail-correct for its local Run; whether retail "
				"picks that clip over the shared bank's fanned one is the unmodelled 0x80 "
				"include-shadowing rule"));
		}
		else
		{
			// The draw landed in the shared bank instead, which has a fan. Also a real answer --
			// and the one modelling `0x80` would rule out.
			TestTrue(TEXT("a sheriff run drawn from the shared bank commands a speed"), bMoved);
			AddInfo(FString::Printf(TEXT("sheriff ACT_RUN drew '%s'@'%s' (shared bank)"),
				*Label, *Owner));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
