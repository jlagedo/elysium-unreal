// Content-free Substrate automation: the damage flinch — its pure direction rule, the fan cell that
// rule selects, and the producer gate on the one health commit (LIFE5).
//
// Every number here is a fact from `docs/vtmb/npc-ai-reverse-engineering.md` (the head/torso pick,
// the incoming-vector derivation, the +-30 degree jitter, the 0.1/0.3 fades) or from
// `docs/vtmb/animation_and_movers.md` (the nine-cell `hit_torso` fan and its `hit_yaw` parameter).
// Nothing loads a rulebook, a mesh or an export: the rule is a function of two origins, a facing and
// a random stream, and the fan is a fixture on the stack.
//
// The rule DRAWS, as retail does, so the determinism here is the harness's: the pure cases run a
// stream declared on the stack, and the producer case seeds the session's own streams and replays.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumReactions.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumBipedAnimInstance.h"   // FElysiumReactionPlay — the release condition's arithmetic

namespace ElysiumDamageFlinchTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;

namespace
{
	using EC = EElysiumTraitContainer;

	constexpr const TCHAR* GBank = TEXT("cast_bank");
	constexpr const TCHAR* GStem = TEXT("flinch_body");

	// The shipped fan, cell for cell: nine cells over -180..180, `hit_torso` (the back reaction) at
	// both seam ends, and the direction words running LEFT through zero to RIGHT. The order is the
	// whole reason the sign of `hit_yaw` is checkable — a mirrored derivation would resolve every
	// cardinal to the opposite word while still resolving.
	const TCHAR* const GHitCells[9] = {
		TEXT("hit_torso"), TEXT("hit_torso_back_left"), TEXT("hit_torso_left"),
		TEXT("hit_torso_front_left"), TEXT("hit_torso_front"), TEXT("hit_torso_front_right"),
		TEXT("hit_torso_right"), TEXT("hit_torso_back_right"), TEXT("hit_torso")
	};

	FElysiumBlendTable MakeHitTable()
	{
		FElysiumPoseParamDesc MoveYaw;
		MoveYaw.Name = TEXT("move_yaw");
		MoveYaw.Start = -180.0f;
		MoveYaw.End = 180.0f;
		MoveYaw.Loop = 360.0f;

		FElysiumPoseParamDesc HitYaw;
		HitYaw.Name = TEXT("hit_yaw");
		HitYaw.Start = -180.0f;
		HitYaw.End = 180.0f;
		HitYaw.Loop = 360.0f;

		FElysiumBlendGrid Fan;
		Fan.Label = TEXT("hit_torso");
		Fan.GroupSize[0] = 9;
		Fan.GroupSize[1] = 1;
		// The shipped sidecar declares `move_yaw` first and binds this fan to the SECOND parameter.
		Fan.ParamIndex[0] = 1;
		Fan.ParamIndex[1] = INDEX_NONE;
		Fan.ParamStart[0] = -180.0f;
		Fan.ParamEnd[0] = 180.0f;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			FElysiumBlendCell Cell;
			Cell.Axis[0] = Index;
			Cell.Axis[1] = 0;
			Cell.Clip = GHitCells[Index];
			Fan.Cells.Add(Cell);
		}

		FElysiumBlendTable Table;
		Table.Stem = GBank;
		Table.PoseParams.Add(MoveYaw);
		Table.PoseParams.Add(HitYaw);
		Table.Grids.Add(TEXT("hit_torso"), Fan);
		return Table;
	}

	FElysiumNpcClipSet MakeHitBody()
	{
		FElysiumNpcClip Torso;
		Torso.Owner = GBank;
		Torso.Activity = TEXT("ACT_HIT_TORSO");
		Torso.Weight = 30;
		Torso.Flags = 0x0;
		Torso.Frames = 20;
		Torso.Fps = 30.0f;

		FElysiumNpcClip Head = Torso;
		Head.Activity = TEXT("ACT_HIT_HEAD");

		FElysiumNpcClipSet Set;
		Set.Stem = GStem;
		Set.Clips.Add(TEXT("hit_torso"), Torso);
		Set.Clips.Add(TEXT("hit_head"), Head);
		return Set;
	}

	// One angle, resolved all the way through the fan — the record whole, because a cell name alone
	// cannot say what the graph strikes: a fan is sampled BETWEEN two cells and the pair plus the
	// weight is the pose. It is also the only assertion that can catch a mirrored sign, because both
	// signs resolve and only one names the side the attacker stands on.
	FElysiumAnimationSelection FanAt(float HitYawDegrees, const FElysiumNpcClipSet& Body,
		const FElysiumBlendTable& Table)
	{
		FElysiumActivityClipRequest Request;
		Request.Stem = GStem;
		Request.Activity = TEXT("ACT_HIT_TORSO");
		Request.Source = EElysiumAnimSource::Damage;
		Request.BodyKind = EElysiumAnimBodyKind::Cast;
		Request.HitYaw = HitYawDegrees;
		Request.bAllowFallbackLadder = false;

		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Body;
		Catalog.BlendTableFor = [&Table](const FString& Owner) -> const FElysiumBlendTable*
		{
			return Owner.Equals(GBank, ESearchCase::IgnoreCase) ? &Table : nullptr;
		};

		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(ElysiumAnimResolve::ActivityIntentFor(Request), Catalog, Out);
		return Out;
	}

	FString CellFor(float HitYawDegrees, const FElysiumNpcClipSet& Body,
		const FElysiumBlendTable& Table)
	{
		return FanAt(HitYawDegrees, Body, Table).AnimationName;
	}

	// A victim at the origin facing Unreal yaw `Facing`, hit by an attacker standing at `Attacker`.
	// The jitter is excluded: `HitYawFrom` is the derivation on its own, which is what the cardinals
	// are about.
	// A refusal answers a value no cardinal can equal, so a derivation that stopped answering fails
	// the assertions below rather than reading as "straight ahead".
	constexpr float GNoBearing = -999.0f;

	float Bearing(const FVector& Attacker, const FVector& Victim, float Facing)
	{
		float Out = 0.0f;
		return ElysiumReactions::HitYawFrom(Attacker, Victim, Facing, Out) ? Out : GNoBearing;
	}

	FElysiumDmg ResolvedDmg(EElysiumDmgFamily Family, int32 Amount, uint32 Mask = 0)
	{
		FElysiumDmg Dmg;
		Dmg.Family = Family;
		Dmg.Flags = ElysiumDamage::FlagDirectInput;
		Dmg.ExtraInput = Amount;
		Dmg.DmgMask = Mask;
		Dmg.RolledSuccesses = Amount;
		Dmg.Remainder = Amount;
		Dmg.AppliedDamage = Amount;
		Dmg.bResolved = true;
		return Dmg;
	}

	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	FElysiumEntityDefs MakeFlinchTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__flinch_test__");

		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VHumanCombatant");
		Victim.TargetName = TEXT("victim");
		// A model, because a bodiless character has no visual to flinch — `BuildBody` returns early
		// without one and the producer's first gate would be the thing under test.
		Victim.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		FElysiumOutputDef Died;
		Died.Name = TEXT("OnDeath");
		Died.Target = TEXT("deathcount");
		Died.Input = TEXT("Add");
		Died.Param = TEXT("1");
		Victim.Outputs.Add(MoveTemp(Died));
		Defs.Defs.Add(MoveTemp(Victim));

		// The environmental producer: a hurt volume is an entity, and it is not a combat character.
		FElysiumEntityDef Hurt;
		Hurt.Classname = TEXT("trigger_hurt");
		Hurt.TargetName = TEXT("hurtbox");
		Hurt.Keys.Add(TEXT("damage"), TEXT("10"));
		Defs.Defs.Add(MoveTemp(Hurt));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("deathcount");
		Defs.Defs.Add(MoveTemp(Counter));
		return Defs;
	}

	FElysiumCombatCharacter* FindCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent != nullptr ? Ent->AsCombatCharacter() : nullptr;
	}

	int32 CountOneShots(const FElysiumRecordingServices& Services)
	{
		int32 Count = 0;
		for (const FString& Call : Services.Calls)
		{
			if (Call.StartsWith(TEXT("PlayNpcOneShot")))
			{
				++Count;
			}
		}
		return Count;
	}

	FString FirstCall(const FElysiumRecordingServices& Services, const TCHAR* Prefix)
	{
		for (const FString& Call : Services.Calls)
		{
			if (Call.StartsWith(Prefix))
			{
				return Call;
			}
		}
		return FString();
	}

	// One recorded field, from `Key=` to the next space. The recorded line is a flat key/value list,
	// so this is what turns a replay into a comparable tuple.
	FString FieldOf(const FString& Line, const TCHAR* Key)
	{
		const int32 At = Line.Find(Key);
		if (At == INDEX_NONE)
		{
			return FString();
		}
		FString Tail = Line.Mid(At + FCString::Strlen(Key));
		int32 Space = INDEX_NONE;
		return Tail.FindChar(TEXT(' '), Space) ? Tail.Left(Space) : Tail;
	}

	// The flinch a hit produced, as the three fields a replay has to reproduce: which activity, the
	// derived angle, and the weighted-sequence draw.
	FString FlinchSignature(const FElysiumRecordingServices& Services)
	{
		const FString Line = FirstCall(Services, TEXT("ResolveNpcActivityClip"));
		if (Line.IsEmpty())
		{
			return FString();
		}
		const TCHAR* Activity = Line.Contains(TEXT("ACT_HIT_HEAD")) ? TEXT("ACT_HIT_HEAD")
			: (Line.Contains(TEXT("ACT_HIT_TORSO")) ? TEXT("ACT_HIT_TORSO") : TEXT("-"));
		return FString::Printf(TEXT("%s hit=%s var=%s"), Activity,
			*FieldOf(Line, TEXT("hit=")), *FieldOf(Line, TEXT("var=")));
	}
}

// =====================================================================================
// The pure rule: which activity, and at what angle
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageFlinchRuleTest,
	"Elysium.Substrate.DamageFlinch.Rule", GElysiumTestFlags)
bool FElysiumDamageFlinchRuleTest::RunTest(const FString&)
{
	const FElysiumBlendTable Table = MakeHitTable();
	const FElysiumNpcClipSet Body = MakeHitBody();
	const FVector Victim = FVector::ZeroVector;

	// --- The cardinals, and the sign convention they pin -------------------------------------------
	//
	// **The resolved convention: the vector runs from the VICTIM to the ATTACKER**, so `hit_yaw` is
	// the bearing of whoever threw the blow, right-positive with zero forward. That is the reading
	// the fan's own cell names force — an attacker on the left has to land a LEFT-named reaction —
	// and the direction of travel would mirror every one of them.
	{
		// Unreal is left-handed with +X forward and +Y right, so the victim's left is -Y.
		TestEqual(TEXT("an attacker on the victim's left reads -90"),
			Bearing(FVector(0.0f, -100.0f, 0.0f), Victim, 0.0f), -90.0f);
		TestEqual(TEXT("...which is the LEFT cell of the fan"),
			CellFor(-90.0f, Body, Table), FString(TEXT("hit_torso_left")));
		// The angle itself reaches the record, on the axis the fan binds — this is the value the
		// graph's reaction branch steers its blend space by, so a record that lost it would resolve
		// every flinch at the fan's own base cell with nothing reporting it.
		{
			const FElysiumAnimationSelection Left = FanAt(-90.0f, Body, Table);
			TestEqual(TEXT("...with the angle on the record"), Left.AxisValue[0], -90.0f);
			TestEqual(TEXT("...under the parameter's own name"), Left.AxisName[0],
				FString(TEXT("hit_yaw")));
			TestEqual(TEXT("...as a blend space, which is what the branch can play"),
				static_cast<int32>(Left.AssetKind),
				static_cast<int32>(EElysiumAnimAssetKind::BlendSpace));
			TestEqual(TEXT("...naming the cell after it as the pair's second half"),
				Left.NextAnimationName, FString(TEXT("hit_torso_front_left")));
			TestEqual(TEXT("...which a cardinal gives none of its weight"), Left.AxisFraction[0],
				0.0f);
		}

		TestEqual(TEXT("an attacker on the right reads +90"),
			Bearing(FVector(0.0f, 100.0f, 0.0f), Victim, 0.0f), 90.0f);
		TestEqual(TEXT("...which is the RIGHT cell"),
			CellFor(90.0f, Body, Table), FString(TEXT("hit_torso_right")));

		TestEqual(TEXT("an attacker straight ahead reads zero"),
			Bearing(FVector(100.0f, 0.0f, 0.0f), Victim, 0.0f), 0.0f);
		TestEqual(TEXT("...which is the FRONT cell, not the fan's -180 base"),
			CellFor(0.0f, Body, Table), FString(TEXT("hit_torso_front")));

		TestEqual(TEXT("an attacker directly behind reads 180"),
			Bearing(FVector(-100.0f, 0.0f, 0.0f), Victim, 0.0f), 180.0f);
		TestEqual(TEXT("...which is the shared back cell at the seam"),
			CellFor(180.0f, Body, Table), FString(TEXT("hit_torso")));
		TestEqual(TEXT("...and so is its negative twin"),
			CellFor(-180.0f, Body, Table), FString(TEXT("hit_torso")));
	}

	// --- Between two cells: the flinch is a BLEND, not a snap --------------------------------------
	//
	// The fan's cells are 45 degrees apart, so 112.5 is the exact midpoint of the pair
	// `hit_torso_right` / `hit_torso_back_right` — an EVEN MIX of two authored reactions. This is the
	// pose no single-cell answer can strike, and the whole reason a directional flinch is played on
	// the graph's reaction branch rather than as a montage. **One rule for every fan**: the same
	// floor-cell-plus-fraction the `move_yaw` gait fans resolve through, applied here unchanged.
	{
		const FElysiumAnimationSelection Even = FanAt(112.5f, Body, Table);
		TestEqual(TEXT("the exact half-cell midpoint names the lower cell"), Even.AnimationName,
			FString(TEXT("hit_torso_right")));
		TestEqual(TEXT("...and the upper one"), Even.NextAnimationName,
			FString(TEXT("hit_torso_back_right")));
		TestTrue(TEXT("...at an even mix of the two"),
			FMath::IsNearlyEqual(Even.AxisFraction[0], 0.5f, 0.001f));

		// Either side of the midpoint: the PAIR does not move, only the weight does. A quantizing
		// resolver would name two different cells here and a zero fraction on both.
		const FElysiumAnimationSelection Past = FanAt(113.0f, Body, Table);
		TestEqual(TEXT("just past it the pair is unchanged"), Past.AnimationName,
			FString(TEXT("hit_torso_right")));
		TestTrue(TEXT("...leaning onto the upper cell"), Past.AxisFraction[0] > 0.5f);
		const FElysiumAnimationSelection Short = FanAt(111.0f, Body, Table);
		TestEqual(TEXT("just short of it the pair is the same again"), Short.AnimationName,
			FString(TEXT("hit_torso_right")));
		TestTrue(TEXT("...leaning onto the lower cell"), Short.AxisFraction[0] < 0.5f);
	}

	// --- The collapse a body with no reaction branch performs --------------------------------------
	//
	// `NearerCell` is the whole of it: pure arithmetic over an already-made pick, asserted on the
	// stack. It exists only for a body that cannot evaluate a fan — one clip has to be chosen, and
	// flooring would bias every such reaction a cell counter-clockwise.
	{
		const FElysiumBlendGrid& Fan = Table.Grids[TEXT("hit_torso")];

		auto CollapsedAt = [&Fan, &Table](float Degrees) -> FString
		{
			FElysiumPoseParams Pose;
			Pose.Set(TEXT("hit_yaw"), Degrees);
			const FElysiumBlendPick Nearer = ElysiumBlendGrids::NearerCell(
				ElysiumBlendGrids::SelectCell(Fan, Table, Pose), Fan);
			return Nearer.Cell != nullptr ? Nearer.Cell->Clip : FString();
		};

		// Both sides of the half-cell, which is the only boundary the rule has.
		TestEqual(TEXT("a hair short of the half-cell keeps the lower cell"), CollapsedAt(111.0f),
			FString(TEXT("hit_torso_right")));
		TestEqual(TEXT("the exact half-cell steps up"), CollapsedAt(112.5f),
			FString(TEXT("hit_torso_back_right")));
		TestEqual(TEXT("and a hair past it stays there"), CollapsedAt(113.0f),
			FString(TEXT("hit_torso_back_right")));
		// A cardinal has no fraction at all, so it cannot step anywhere.
		TestEqual(TEXT("a cardinal collapses onto itself"), CollapsedAt(-90.0f),
			FString(TEXT("hit_torso_left")));

		// The wrap clamp: the axis has already been folded and clamped, so the last cell is only ever
		// reached with a zero fraction and the step cannot walk off the end of the fan.
		TestEqual(TEXT("the seam cell cannot be stepped past"), CollapsedAt(180.0f),
			FString(TEXT("hit_torso")));
		TestEqual(TEXT("...from either side of the fold"), CollapsedAt(-180.0f),
			FString(TEXT("hit_torso")));
		TestEqual(TEXT("...and just inside it steps to the seam rather than beyond it"),
			CollapsedAt(179.0f), FString(TEXT("hit_torso")));
	}

	// --- Frame relativity: the answer is the bearing IN THE VICTIM'S FRAME ------------------------
	// Rotating the whole scene cannot move the cell, because a hit on the left shoulder is a hit on
	// the left shoulder whichever way the pair is standing. A derivation that dropped the facing term
	// would pass every cardinal above and fail here.
	{
		const float Plain = Bearing(FVector(0.0f, -100.0f, 0.0f), Victim, 0.0f);
		// The same pair, both turned 90 degrees about the victim. Unreal's yaw takes (x, y) to
		// (-y, x), so the attacker on -Y swings round to +X while the victim comes to face +Y.
		const float Rotated = Bearing(FVector(100.0f, 0.0f, 0.0f), Victim, 90.0f);
		TestEqual(TEXT("rotating the pair by 90 degrees does not move the answer"), Rotated, Plain);

		// And a translation of both: the rule reads a difference, never a position.
		const FVector Offset(1234.0f, -567.0f, 89.0f);
		float Moved = 0.0f;
		TestTrue(TEXT("a translated pair still answers"),
			ElysiumReactions::HitYawFrom(FVector(0.0f, -100.0f, 0.0f) + Offset, Victim + Offset,
				0.0f, Moved));
		TestEqual(TEXT("...with the same bearing"), Moved, Plain);

		// Height is not a direction on a yaw fan: a hit from above the same spot reads the same cell.
		float Elevated = 0.0f;
		TestTrue(TEXT("an attacker standing higher still answers"),
			ElysiumReactions::HitYawFrom(FVector(0.0f, -100.0f, 400.0f), Victim, 0.0f, Elevated));
		TestEqual(TEXT("...with the same bearing"), Elevated, Plain);
	}

	// --- The wrap seam ---------------------------------------------------------------------------
	// A hit from just off directly-behind sits on the +-180 fold. The answer is normalized into
	// (-180, 180] so the two sides of the seam are a hair apart in value and one cell apart at most,
	// rather than 360 degrees apart.
	{
		const float JustLeftOfBehind = Bearing(FVector(-100.0f, -1.0f, 0.0f), Victim, 0.0f);
		const float JustRightOfBehind = Bearing(FVector(-100.0f, 1.0f, 0.0f), Victim, 0.0f);
		TestTrue(TEXT("just left of directly-behind is a large negative"), JustLeftOfBehind < -179.0f);
		TestTrue(TEXT("just right of it is a large positive"), JustRightOfBehind > 179.0f);
		TestTrue(TEXT("...and both are inside (-180, 180]"),
			JustLeftOfBehind >= -180.0f && JustRightOfBehind <= 180.0f);
		// The shared back cell dominates from both sides, and each side says so in the shape its own
		// half of the fold takes: below the seam the pair STARTS on it with almost no weight moved off,
		// above the seam the pair ENDS on it with almost all the weight moved onto it. The pose is the
		// same either way, which is what "a hair apart in value" has to mean on the fan.
		{
			const FElysiumAnimationSelection Left = FanAt(JustLeftOfBehind, Body, Table);
			TestEqual(TEXT("just below the fold the pair starts on the shared back cell"),
				Left.AnimationName, FString(TEXT("hit_torso")));
			TestTrue(TEXT("...with almost none of its weight given away"), Left.AxisFraction[0] < 0.05f);

			const FElysiumAnimationSelection Right = FanAt(JustRightOfBehind, Body, Table);
			TestEqual(TEXT("just above it the pair ENDS on the same shared cell"),
				Right.NextAnimationName, FString(TEXT("hit_torso")));
			TestTrue(TEXT("...carrying almost all of the weight"), Right.AxisFraction[0] > 0.95f);
		}

		// The jitter is applied BEFORE the normalize, so a rear hit cannot steer a fan past its own
		// last cell. Every draw at the seam has to land inside the range.
		FRandomStream Seam(0x5EA33333);
		for (int32 Step = 0; Step < 64; ++Step)
		{
			ElysiumReactions::FElysiumFlinch Flinch;
			const bool bBuilt = ElysiumReactions::BuildFlinch(FVector(-100.0f, 0.0f, 0.0f), Victim,
				0.0f, Seam, Flinch);
			if (!bBuilt || Flinch.HitYawDegrees > 180.0f || Flinch.HitYawDegrees <= -180.001f)
			{
				AddError(FString::Printf(
					TEXT("a rear hit at draw %d left the parameter range: %.3f"),
					Step, Flinch.HitYawDegrees));
				break;
			}
		}
	}

	// --- The jitter, the coin, and determinism -----------------------------------------------------
	{
		TestEqual(TEXT("the jitter band is the recovered +-30 degrees"),
			ElysiumReactions::FlinchJitterDegrees, 30.0f);
		TestEqual(TEXT("the fade in is retail's 0.1"), ElysiumReactions::FlinchBlendInSeconds, 0.1f);
		TestEqual(TEXT("...and the fade out its 0.3"),
			ElysiumReactions::FlinchBlendOutSeconds, 0.3f);

		// --- The envelope those two numbers ARE ---------------------------------------------------
		//
		// Retail's `DamageFlinch` weight is a linear triangle over a separate overlay slot: it rises
		// for the first value, peaks, falls for the second, and is gone at their sum. There is no hold
		// anywhere in it and the clip is evaluated as a static pose, so the CELL'S OWN LENGTH cannot
		// enter the arithmetic — which is exactly what the `Envelope` release condition states, and
		// what a shipped two-frame hit cell makes checkable.
		{
			FElysiumReactionPlay Flinch;
			Flinch.LengthSeconds = 1.0f / 30.0f * 2.0f;   // the two frames every shipped hit cell bakes to
			Flinch.BlendInSeconds = ElysiumReactions::FlinchBlendInSeconds;
			Flinch.BlendOutSeconds = ElysiumReactions::FlinchBlendOutSeconds;
			Flinch.Release = EElysiumReactionRelease::Envelope;
			TestEqual(TEXT("the flinch peaks at the fade in"), Flinch.ActiveSeconds(), 0.1f);
			TestEqual(TEXT("...and is gone at the sum of the pair"), Flinch.TotalSeconds(), 0.4f);
			// The same play timed off its clip instead: the two-frame cell is shorter than the out-fade
			// alone, so the branch would be dropped by the update that armed it. Asserted as the
			// contrast, because it is the failure the release condition exists to name.
			Flinch.Release = EElysiumReactionRelease::ClipCompletion;
			TestEqual(TEXT("a clip-timed two-frame cell holds for nothing at all"),
				Flinch.ActiveSeconds(), 0.0f);

			// A struck reaction — a block, a knockback — is the ordinary case: its clip's own length,
			// with the fade completing ON that length rather than after it.
			FElysiumReactionPlay Struck;
			Struck.LengthSeconds = 1.2f;
			Struck.BlendInSeconds = 0.0f;
			Struck.BlendOutSeconds = 0.2f;
			TestEqual(TEXT("a struck reaction stands for its clip less the out-fade"),
				Struck.ActiveSeconds(), 1.0f);
			TestEqual(TEXT("...and its whole life is the clip"), Struck.TotalSeconds(), 1.2f);

			// A HELD play answers neither: it is released by a predicate, so there is no span to state
			// and the negative is the same "never" sentinel every aged readout in this repo uses. A
			// zero here would read to the channel claim as "hold forever" by accident rather than by
			// statement, and to the phase clock as "expire immediately".
			FElysiumReactionPlay Held;
			Held.LengthSeconds = 1.2f;
			Held.Release = EElysiumReactionRelease::Predicate;
			TestTrue(TEXT("a held play states no active span"), Held.ActiveSeconds() < 0.0f);
			TestTrue(TEXT("...and no life at all"), Held.TotalSeconds() < 0.0f);
			TestTrue(TEXT("...and says so by name"), Held.IsHeld());
			TestFalse(TEXT("a struck reaction is not held"), Struck.IsHeld());
		}

		const FVector Left(0.0f, -100.0f, 0.0f);

		int32 Heads = 0;
		int32 Flips = 0;
		float Widest = 0.0f;
		float StepSum = 0.0f;
		float Previous = 0.0f;
		bool bHadPrevious = false;
		bool bPreviousHead = false;
		bool bOutOfBand = false;
		FRandomStream Run(0x4C494645);
		for (int32 Index = 0; Index < 1000; ++Index)
		{
			// **One stream, drawn a thousand times** — exactly the sequence a thousand hits produce,
			// because the producer holds no per-body token any more and every hit is the next draw.
			ElysiumReactions::FElysiumFlinch Flinch;
			if (!ElysiumReactions::BuildFlinch(Left, Victim, 0.0f, Run, Flinch))
			{
				AddError(TEXT("a well-separated pair failed to build a flinch"));
				break;
			}
			Heads += Flinch.bHead ? 1 : 0;
			const float Jitter = Flinch.HitYawDegrees - (-90.0f);
			Widest = FMath::Max(Widest, FMath::Abs(Jitter));
			bOutOfBand = bOutOfBand || FMath::Abs(Jitter) > ElysiumReactions::FlinchJitterDegrees;
			if (bHadPrevious)
			{
				StepSum += FMath::Abs(Flinch.HitYawDegrees - Previous);
				Flips += (Flinch.bHead != bPreviousHead) ? 1 : 0;
			}
			Previous = Flinch.HitYawDegrees;
			bPreviousHead = Flinch.bHead;
			bHadPrevious = true;
		}
		TestFalse(TEXT("no draw leaves the +-30 band"), bOutOfBand);
		TestTrue(TEXT("...and the band is actually travelled"), Widest > 25.0f);
		TestTrue(TEXT("both the head and the torso are reached over 1000 hits"),
			Heads > 0 && Heads < 1000);
		// A coin, not a bias: half of a thousand draws, with room for the sampling spread.
		TestTrue(TEXT("...at roughly even odds"), Heads > 400 && Heads < 600);
		TestTrue(TEXT("consecutive hits draw far apart in the band"),
			(StepSum / 999.0f) > 10.0f);
		TestTrue(TEXT("...and flip the head/torso pick about half the time"),
			Flips > 350 && Flips < 650);

		// The band is CENTRED, not merely bounded: a uniform draw over [-30, +30] has mean zero, so a
		// jitter that leaned (a half-open range read off the wrong end, a coin sharing the draw) shows
		// up here and nowhere in the bounds above. The spread of a uniform band that wide is about
		// 17.3 degrees, so ten thousand draws put the mean inside a degree of zero with room to spare.
		{
			double JitterSum = 0.0;
			FRandomStream Symmetry(0x53594D4D);
			for (int32 Index = 0; Index < 10000; ++Index)
			{
				ElysiumReactions::FElysiumFlinch Flinch;
				if (!ElysiumReactions::BuildFlinch(Left, Victim, 0.0f, Symmetry, Flinch))
				{
					AddError(TEXT("a well-separated pair failed to build a flinch"));
					break;
				}
				JitterSum += static_cast<double>(Flinch.HitYawDegrees) - (-90.0);
			}
			const double MeanJitter = JitterSum / 10000.0;
			TestTrue(FString::Printf(
				TEXT("the jitter is symmetric about the derived angle (mean %.3f)"), MeanJitter),
				FMath::Abs(MeanJitter) < 1.0);
		}

		// **Determinism is the stream's, not the rule's.** Two streams standing at the same position
		// answer the same flinch — which is what makes a saved run replay — while ONE stream drawn
		// twice answers two different flinches, which is the whole point of a hit re-rolling.
		{
			FRandomStream A(0x1234ABCD);
			FRandomStream B(0x1234ABCD);
			ElysiumReactions::FElysiumFlinch First;
			ElysiumReactions::FElysiumFlinch Mirror;
			ElysiumReactions::BuildFlinch(Left, Victim, 0.0f, A, First);
			ElysiumReactions::BuildFlinch(Left, Victim, 0.0f, B, Mirror);
			TestEqual(TEXT("two streams at the same position pick the same activity"),
				FString(Mirror.Activity()), FString(First.Activity()));
			TestEqual(TEXT("...and the same angle"), Mirror.HitYawDegrees, First.HitYawDegrees);

			ElysiumReactions::FElysiumFlinch Second;
			ElysiumReactions::BuildFlinch(Left, Victim, 0.0f, A, Second);
			TestNotEqual(TEXT("a second draw off ONE stream is a different flinch"),
				Second.HitYawDegrees, First.HitYawDegrees);
		}

		// --- The golden draw ORDER -----------------------------------------------------------------
		//
		// One known seed, one flinch, both values pinned. What this catches is the one thing every
		// assertion above is blind to: the ORDER the draws come off the stream in. The coin, the
		// jitter and (in the producer) the weighted-sequence variant are three consecutive values of
		// one `FRandomStream`, so swapping two of them, inserting a fourth, or moving one behind a
		// branch leaves every distribution here identical and every saved run replaying differently.
		// A save records a stream POSITION; that is what makes the order a compatibility surface.
		{
			FRandomStream Golden(0x474F4C44);
			ElysiumReactions::FElysiumFlinch First;
			if (TestTrue(TEXT("the golden seed builds a flinch"),
				ElysiumReactions::BuildFlinch(Left, Victim, 0.0f, Golden, First)))
			{
				TestTrue(TEXT("the golden draw's coin comes up head"), First.bHead);
				TestTrue(FString::Printf(
					TEXT("...and its jittered angle is the golden -77.073 (got %.4f)"),
					First.HitYawDegrees),
					FMath::IsNearlyEqual(First.HitYawDegrees, -77.0731f, 0.01f));
			}
		}

		// The two activity spellings, which the fan fixture above resolves against.
		ElysiumReactions::FElysiumFlinch Head;
		Head.bHead = true;
		TestEqual(TEXT("the head activity is ACT_HIT_HEAD"),
			FString(Head.Activity()), FString(TEXT("ACT_HIT_HEAD")));
		ElysiumReactions::FElysiumFlinch Torso;
		TestEqual(TEXT("and the torso activity ACT_HIT_TORSO"),
			FString(Torso.Activity()), FString(TEXT("ACT_HIT_TORSO")));
	}

	// --- Coincident origins name no direction ------------------------------------------------------
	{
		float Out = 12.0f;
		TestFalse(TEXT("two bodies at one point answer no bearing"),
			ElysiumReactions::HitYawFrom(Victim, Victim, 0.0f, Out));
		TestEqual(TEXT("...and the out parameter is cleared rather than left stale"), Out, 0.0f);

		// Stacked vertically is the same statement: a yaw fan has no cell for straight down.
		TestFalse(TEXT("an attacker directly overhead answers no bearing"),
			ElysiumReactions::HitYawFrom(FVector(0.0f, 0.0f, 300.0f), Victim, 0.0f, Out));

		ElysiumReactions::FElysiumFlinch Flinch;
		Flinch.bHead = true;
		Flinch.HitYawDegrees = 99.0f;
		FRandomStream Refused(0x4E4F4E45);
		const int32 Before = Refused.GetCurrentSeed();
		TestFalse(TEXT("and BuildFlinch refuses the same pair"),
			ElysiumReactions::BuildFlinch(Victim, Victim, 0.0f, Refused, Flinch));
		TestEqual(TEXT("...having reset what it was handed"), Flinch.HitYawDegrees, 0.0f);
		TestFalse(TEXT("...including the pick"), Flinch.bHead);
		// A refusal is not a hit, so it spends no randomness: the stream stands exactly where it did.
		TestEqual(TEXT("...and drew nothing off the stream"), Refused.GetCurrentSeed(), Before);
	}

	return true;
}

// =====================================================================================
// The producer: the gate on the one health commit
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageFlinchProducerTest,
	"Elysium.Substrate.DamageFlinch.Producer", GElysiumTestFlags)
bool FElysiumDamageFlinchProducerTest::RunTest(const FString&)
{
	// One fixture shape for every case: a victim with a body at the origin facing +X, and an attacker
	// standing on its left. `Setup` hands back the two characters with the recording services armed
	// to answer the activity seam.
	auto Stand = [this](FElysiumRecordingServices& Services, FElysiumEntityWorld& World,
		FElysiumCombatCharacter*& OutVictim, FElysiumPlayer*& OutAttacker) -> bool
	{
		// The producer draws off the session's own Reaction stream, so the suite seeds it — the same
		// fixture idiom every other stream-reading suite uses. Nothing restores it afterwards; a
		// seeded stream is the state the next test wants anyway.
		ElysiumRng::SeedAll(0x464C4E43);
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityLabel = TEXT("hit_torso");
		Services.ResolvedNpcActivityClip = TEXT("hit_torso_left");
		Services.ResolvedNpcActivityOwner = TEXT("cast_bank");
		// The shipped shape: `hit_torso` names a nine-cell fan, so the seam answers a grid and the
		// producer has to route it to the branch that can evaluate one.
		Services.bResolvedNpcActivityIsGrid = true;
		Services.ResolvedNpcActivityNextClip = TEXT("hit_torso_front_left");
		Services.ResolvedNpcActivityFraction = 0.25f;
		Services.bNpcOneShotsPlay = true;

		World.Load(MakeFlinchTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		OutVictim = FindCharacter(World, TEXT("victim"));
		OutAttacker = World.FindPlayer();
		if (!TestNotNull(TEXT("the victim exists"), OutVictim)
			|| !TestNotNull(TEXT("the attacker exists"), OutAttacker))
		{
			return false;
		}
		if (!TestNotNull(TEXT("the victim carries a body to flinch"), OutVictim->Visual))
		{
			return false;
		}
		SeedHealth(*OutVictim, 100);
		OutVictim->Origin = FVector::ZeroVector;
		OutVictim->Angles = FVector::ZeroVector;             // Source yaw 0 -> Unreal yaw 0
		OutAttacker->Origin = FVector(0.0f, -200.0f, 0.0f);   // the victim's left
		Services.Calls.Reset();
		return true;
	};

	// --- It fires for every typed producer, and carries the direction and the fades ---------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumPlayer* Attacker = nullptr;
		if (!Stand(Services, World, Victim, Attacker))
		{
			return false;
		}

		FElysiumDmg Melee = ResolvedDmg(EElysiumDmgFamily::Bashing, 5, ElysiumDamage::DmgClub);
		Melee.Source = Attacker->Handle;
		Victim->CommitDamage(Melee);
		TestEqual(TEXT("a melee hit flinches once"), CountOneShots(Services), 1);

		const FString Resolve = FirstCall(Services, TEXT("ResolveNpcActivityClip"));
		TestTrue(TEXT("...asking for one of the two hit activities"),
			Resolve.Contains(TEXT("ACT_HIT_TORSO")) || Resolve.Contains(TEXT("ACT_HIT_HEAD")));
		TestTrue(TEXT("...on the cast chain"), Resolve.EndsWith(TEXT("body=cast")));
		// The attacker stands on the victim's left, so the request has to carry a NEGATIVE hit yaw:
		// the derivation is exercised through the real producer here, not just as a function.
		const int32 HitToken = Resolve.Find(TEXT("hit="));
		if (TestTrue(TEXT("...and the request records the derived hit yaw"), HitToken != INDEX_NONE))
		{
			const float Recorded = FCString::Atof(*Resolve.Mid(HitToken + 4));
			TestTrue(TEXT("...on the LEFT of the body, inside the jitter band"),
				Recorded < -60.0f && Recorded > -120.0f);
		}

		const FString Played = FirstCall(Services, TEXT("PlayNpcOneShot"));
		TestTrue(TEXT("the resolved cell is played, not the vocabulary label"),
			Played.Contains(TEXT("hit_torso_left")));
		TestTrue(TEXT("...off the bank the include DAG named"),
			Played.Contains(TEXT("cast_bank")));
		TestTrue(TEXT("...as a one-shot"), Played.Contains(TEXT("loop=0")));
		TestTrue(TEXT("...with retail's two fades"),
			Played.Contains(TEXT("in=0.10")) && Played.Contains(TEXT("out=0.30")));
		TestTrue(TEXT("...in the reaction band"), Played.Contains(TEXT("prio=reaction")));
		// **And released by that fade pair alone.** Retail's `DamageFlinch` envelope is a linear
		// weight triangle with NO hold between its two compiled fades, evaluated against a static
		// pose — so what ends the flinch is the clock over 0.1 + 0.3, and the cell's own length
		// decides nothing. A producer that left the release at `clip` would time a two-frame hit cell
		// off two frames.
		TestTrue(TEXT("...released by retail's own weight envelope rather than by its clip"),
			Played.Contains(TEXT("release=envelope")));
		// **The route, which is what makes the flinch a blend rather than a montage.** A reaction
		// REPLACES the base pose on the graph's own branch; the one-shot slot rides over it and can
		// play only one cell, so a producer that left the route at its default would snap the body
		// onto one authored direction with nothing reporting it.
		TestTrue(TEXT("...on the reaction route rather than the one-shot slot"),
			Played.Contains(TEXT("route=reaction")));
		TestTrue(TEXT("...carrying the fan the seam resolved"), Played.Contains(TEXT("grid=1")));
		// And the angle with it: the axis value is what steers the fan, so a producer that dropped it
		// would play every flinch at the fan's own resting parameter.
		const FString PlayedAxis = FieldOf(Played, TEXT("axis="));
		if (TestFalse(TEXT("...and the derived angle"), PlayedAxis.IsEmpty()))
		{
			const float Steered = FCString::Atof(*PlayedAxis);
			TestTrue(TEXT("...on the LEFT of the body, inside the jitter band"),
				Steered < -60.0f && Steered > -120.0f);
		}

		// **Which stream it draws from, pinned by bracketing one commit.** The flinch is declared to
		// draw off `Reaction` and nothing else; a producer that reached for `Dice` — or for
		// `FMath::Rand`, which advances neither — would pass every determinism assertion in this suite
		// because both streams are re-seeded together, and would then desynchronise every d10 roll in
		// the run for as long as anything was being hit.
		{
			Services.Calls.Reset();
			const int32 ReactionBefore =
				ElysiumRng::Stream(EElysiumRngStream::Reaction).GetCurrentSeed();
			const int32 DiceBefore = ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed();
			Victim->CommitDamage(Melee);
			TestNotEqual(TEXT("a flinch advances the Reaction stream"),
				ElysiumRng::Stream(EElysiumRngStream::Reaction).GetCurrentSeed(), ReactionBefore);
			TestEqual(TEXT("...and leaves the Dice stream exactly where it stood"),
				ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed(), DiceBefore);
		}

		// A second hit re-rolls: the variant moves, so the weighted pick is not the same draw twice.
		const FString FirstVariant = FieldOf(Resolve, TEXT("var="));
		Services.Calls.Reset();
		Victim->CommitDamage(Melee);
		const FString Second = FirstCall(Services, TEXT("ResolveNpcActivityClip"));
		TestNotEqual(TEXT("two hits draw two different variants"),
			FieldOf(Second, TEXT("var=")), FirstVariant);

		// Ranged and discipline damage reach the same commit, so both flinch.
		Services.Calls.Reset();
		FElysiumDmg Ranged = ResolvedDmg(EElysiumDmgFamily::Lethal, 4, ElysiumDamage::DmgBullet);
		Ranged.Source = Attacker->Handle;
		Victim->CommitDamage(Ranged);
		TestEqual(TEXT("a ranged hit flinches too"), CountOneShots(Services), 1);

		Services.Calls.Reset();
		FElysiumDmg Discipline = ResolvedDmg(EElysiumDmgFamily::Aggravated, 3, ElysiumDamage::DmgFaith);
		Discipline.Source = Attacker->Handle;
		Victim->CommitDamage(Discipline);
		TestEqual(TEXT("and so does discipline damage"), CountOneShots(Services), 1);
	}

	// --- The gate: no attacking character, no flinch ------------------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumPlayer* Attacker = nullptr;
		if (!Stand(Services, World, Victim, Attacker))
		{
			return false;
		}

		// The scalar route builds a descriptor with no source at all.
		Victim->TakeDamage(6.0f);
		TestEqual(TEXT("the scalar TakeDamage route flinches nothing"), CountOneShots(Services), 0);

		// A hurt volume is an entity and is not a combat character, which is the shape every
		// environmental producer has.
		Services.Calls.Reset();
		FElysiumEntity* Hurt = World.FindByName(TEXT("hurtbox"));
		if (TestNotNull(TEXT("the hurt volume exists"), Hurt))
		{
			FElysiumDmg Environmental = ResolvedDmg(EElysiumDmgFamily::Bashing, 5);
			Environmental.Source = Hurt->Handle;
			Victim->CommitDamage(Environmental);
			TestEqual(TEXT("a trigger_hurt-shaped source flinches nothing"),
				CountOneShots(Services), 0);
		}

		// Self-damage names a character, and it is this one: there is no direction between a body and
		// itself, so the gate refuses it before the derivation can.
		Services.Calls.Reset();
		FElysiumDmg Self = ResolvedDmg(EElysiumDmgFamily::Bashing, 5);
		Self.Source = Victim->Handle;
		Victim->CommitDamage(Self);
		TestEqual(TEXT("self-damage flinches nothing"), CountOneShots(Services), 0);

		// A hit that commits nothing is not a hit: the commit returns before the reaction.
		Services.Calls.Reset();
		FElysiumDmg Soaked = ResolvedDmg(EElysiumDmgFamily::Bashing, 0);
		Soaked.Source = Attacker->Handle;
		Victim->CommitDamage(Soaked);
		TestEqual(TEXT("a fully soaked hit flinches nothing"), CountOneShots(Services), 0);

		// A body standing exactly on its attacker has no bearing to steer the fan by.
		Services.Calls.Reset();
		Attacker->Origin = Victim->Origin;
		FElysiumDmg Stacked = ResolvedDmg(EElysiumDmgFamily::Bashing, 5);
		Stacked.Source = Attacker->Handle;
		Victim->CommitDamage(Stacked);
		TestEqual(TEXT("coincident origins flinch nothing"), CountOneShots(Services), 0);
	}

	// --- A body whose vocabulary carries no reaction: the seam misses, nothing is substituted ------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumPlayer* Attacker = nullptr;
		if (!Stand(Services, World, Victim, Attacker))
		{
			return false;
		}
		Services.bNpcActivitiesResolve = false;

		FElysiumDmg Hit = ResolvedDmg(EElysiumDmgFamily::Bashing, 5, ElysiumDamage::DmgClub);
		Hit.Source = Attacker->Handle;
		Victim->CommitDamage(Hit);
		TestTrue(TEXT("the seam is still asked"),
			Services.Saw(TEXT("ResolveNpcActivityClip")));
		TestEqual(TEXT("...and a miss plays nothing at all"), CountOneShots(Services), 0);
	}

	// --- The killing blow still flinches, before its OnDeath output is delivered -------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumPlayer* Attacker = nullptr;
		if (!Stand(Services, World, Victim, Attacker))
		{
			return false;
		}

		FElysiumDmg Killing = ResolvedDmg(EElysiumDmgFamily::Lethal, 500, ElysiumDamage::DmgBullet);
		Killing.Source = Attacker->Handle;
		Victim->CommitDamage(Killing);

		TestTrue(TEXT("the killing blow reported the death"), Victim->HasReportedDeath());
		TestEqual(TEXT("...and still flinched"), CountOneShots(Services), 1);
		// Death is a schedule task, so the flinch is committed synchronously while the OnDeath output
		// is still in the queue — which is what makes "the death family's claim replaces it" true
		// rather than a race.
		TestEqual(TEXT("...with OnDeath not yet delivered"),
			SaveTestCounterValue(World.FindByName(TEXT("deathcount"))), 0.0f);
		World.Tick(0.0);
		TestEqual(TEXT("...and delivered on the next queue pass"),
			SaveTestCounterValue(World.FindByName(TEXT("deathcount"))), 1.0f);
	}

	// --- Determinism is the SESSION's, not the rule's ----------------------------------------------
	// The flinch draws at random, the way retail's does — so a repeat is only reproducible when the
	// stream it draws from is put back. Re-seeding the session's streams and replaying the same two
	// hits reproduces both reactions exactly, which is what makes a save's recorded stream position
	// worth carrying (S8).
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumPlayer* Attacker = nullptr;
		if (!Stand(Services, World, Victim, Attacker))
		{
			return false;
		}

		FElysiumDmg Hit = ResolvedDmg(EElysiumDmgFamily::Bashing, 5, ElysiumDamage::DmgClub);
		Hit.Source = Attacker->Handle;

		auto TwoHits = [&Services, Victim, &Hit](FString& OutFirst, FString& OutSecond)
		{
			ElysiumRng::SeedAll(0x52504C59);
			Services.Calls.Reset();
			Victim->CommitDamage(Hit);
			OutFirst = FlinchSignature(Services);
			Services.Calls.Reset();
			Victim->CommitDamage(Hit);
			OutSecond = FlinchSignature(Services);
		};

		FString RunOneFirst, RunOneSecond;
		FString RunTwoFirst, RunTwoSecond;
		TwoHits(RunOneFirst, RunOneSecond);
		TwoHits(RunTwoFirst, RunTwoSecond);

		TestFalse(TEXT("the replayed run actually flinched"), RunOneFirst.IsEmpty());
		TestNotEqual(TEXT("the two hits in one run are two different flinches"),
			RunOneSecond, RunOneFirst);
		TestEqual(TEXT("a re-seeded session reproduces the first flinch exactly"),
			RunTwoFirst, RunOneFirst);
		TestEqual(TEXT("...and the second"), RunTwoSecond, RunOneSecond);
	}

	// --- The player is a victim like any other -----------------------------------------------------
	// The chain is one pass: `CommitDamage` is `FElysiumCombatCharacter`'s, so the player flinches off
	// the same producer. The one thing that differs is the classification the request carries — the
	// body chain is the PLAYER's, which is the world's own player-handle test rather than `AsNpc()`.
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumCombatCharacter* Npc = nullptr;
		FElysiumPlayer* Player = nullptr;
		if (!Stand(Services, World, Npc, Player))
		{
			return false;
		}

		// The player entity spawns bodiless without a rulebook to name its clan body, so the model is
		// set through the ordinary runtime writer — the same door `SetModel` uses — which rebuilds the
		// visual. Without a body there is nothing to flinch and the producer's first gate would be the
		// thing under test.
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		if (!TestNotNull(TEXT("the player carries a body to flinch"), Player->Visual))
		{
			return false;
		}
		SeedHealth(*Player, 100);
		Player->Origin = FVector::ZeroVector;
		Player->Angles = FVector::ZeroVector;          // Source yaw 0 -> Unreal yaw 0
		Npc->Origin = FVector(0.0f, -200.0f, 0.0f);    // the player's left
		Services.Calls.Reset();

		FElysiumDmg Swing = ResolvedDmg(EElysiumDmgFamily::Bashing, 5, ElysiumDamage::DmgClub);
		Swing.Source = Npc->Handle;
		Player->CommitDamage(Swing);

		const FString Resolve = FirstCall(Services, TEXT("ResolveNpcActivityClip"));
		TestTrue(TEXT("the player's hit asks for one of the two hit activities"),
			Resolve.Contains(TEXT("ACT_HIT_TORSO")) || Resolve.Contains(TEXT("ACT_HIT_HEAD")));
		TestTrue(TEXT("...on the PLAYER chain, not the cast one"), Resolve.EndsWith(TEXT("body=player")));
		const int32 HitToken = Resolve.Find(TEXT("hit="));
		if (TestTrue(TEXT("...carrying the derived hit yaw"), HitToken != INDEX_NONE))
		{
			const float Recorded = FCString::Atof(*Resolve.Mid(HitToken + 4));
			TestTrue(TEXT("...on the LEFT of the player, inside the jitter band"),
				Recorded < -60.0f && Recorded > -120.0f);
		}
		TestEqual(TEXT("and the reaction plays on the player's own visual"),
			CountOneShots(Services), 1);
		TestTrue(TEXT("...as a Reaction-band one-shot with retail's two fades"),
			FirstCall(Services, TEXT("PlayNpcOneShot")).Contains(TEXT("prio=reaction")));
	}

	return true;
}

}

#endif   // WITH_DEV_AUTOMATION_TESTS
