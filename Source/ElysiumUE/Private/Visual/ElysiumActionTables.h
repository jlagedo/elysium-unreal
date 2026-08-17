#pragma once

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "Templates/Function.h"

// The recovered VtMB activity-translation tables, as project source (LIFE2).
//
// A translation table is a game *rule*, the same category as the `CGameMovement` constants in
// `ElysiumMoveSolve.h` and the compiled slot tables in `ElysiumSheetSlots.h`: it is written down
// once, reviewed, and maintained here. `research/tooling/gen_action_tables.py` produces the data
// from the pinned retail binary as owner-run archaeology; nothing in the build or the export reads
// `vampire.dll`, and no runtime path re-derives a row.
//
// The recovered behaviour is `docs/vtmb/animation_and_movers.md` A.3, and the design is
// `docs/architecture/animation-architecture.md` section 3.4. This header declares the shape; the
// generated data and its accessors live in `ElysiumWeaponActivityTables.cpp`, and the pure
// functions over them live in `ElysiumActionTables.cpp`.
//
// **Rows are never materialised.** Retail's `ActivityOverride` walks a weapon's ladder front to
// back and takes the first translated activity the model can play, so `Resolve` synthesizes one
// candidate per rung and tests it against the body's own clip vocabulary. Expanding the 9,214 rows
// would produce ~9,000 target strings with no consumer.
namespace ElysiumActionTables
{
	// One rung of a weapon's ladder: an ordered base sequence walked under one animation family.
	// The same base sequence backs many blocks — 110 blocks draw on 18 sequences — because block 1
	// is the weapon's own animation set, block 2 the shared class set and block 3 a cousin weapon,
	// and those share their base vocabulary.
	struct FActionBlock
	{
		const TCHAR* const* Bases = nullptr;
		int32 BaseCount = 0;
		// Positions in `Bases` carrying the authored `required` bit, ascending. Provenance only:
		// the pinned server translator never reads the flag, so a flagged row follows the same
		// path as any other.
		const int32* RequiredPositions = nullptr;
		int32 RequiredCount = 0;
		// The animation family this block decorates its bases with. Empty on the blocks that
		// translate a base to a literal and nothing else, where every row is an exception.
		const TCHAR* Family = nullptr;
	};

	// A literal target the block's family does not produce.
	struct FActionException
	{
		int32 Block = 0;
		const TCHAR* Base = nullptr;
		const TCHAR* Target = nullptr;
	};

	// One weapon class's whole ladder.
	struct FWeaponLadder
	{
		// The retail C++ class the table hangs off, which is the identity the RTTI decode recovers.
		const TCHAR* CppClass = nullptr;
		// The entity classnames a map's `additionalequipment`/`alternateequipment` can name
		// (`item_w_katana`). This is the key authored content actually spells.
		const TCHAR* const* EntityClassnames = nullptr;
		int32 EntityClassnameCount = 0;
		const FActionBlock* Blocks = nullptr;
		int32 BlockCount = 0;
		// Sorted by (Block, Base).
		const FActionException* Exceptions = nullptr;
		int32 ExceptionCount = 0;
	};

	// A rename replaces the whole base literal rather than decorating it.
	struct FActionRename
	{
		const TCHAR* Base = nullptr;
		const TCHAR* Prefix = nullptr;
	};

	// What the generator stored, so a malformed regeneration fails a test rather than a pose.
	struct FActionTableCensus
	{
		int32 WeaponClasses = 0;
		int32 BaseSequences = 0;
		int32 BaseEntries = 0;
		int32 Blocks = 0;
		int32 Exceptions = 0;
		int32 RequiredFlags = 0;
		// What the model must expand back into: the recovered row stream's length, its `required`
		// count, and an FNV-1a 64 digest of it. The digest is computed from the *retail decode* at
		// generation time and re-computed from the *committed model* by the round-trip test, so the
		// two can only agree if the compression is lossless.
		int32 RetailRows = 0;
		int32 RetailRequired = 0;
		uint64 RetailRowDigest = 0;
	};

	// --- the generated data -------------------------------------------------------------------
	TArrayView<const FWeaponLadder> WeaponLadders();
	TArrayView<const FActionRename> RenameRules();
	TArrayView<const TCHAR* const> SubstituteBases();
	const FActionTableCensus& Census();

	// --- the rules over it --------------------------------------------------------------------

	// Which shape a base takes under a family. Reported so a conformance run can show that no kind
	// is dead: a misdecoded rule shows up as a kind that resolves nothing.
	enum class ERewriteKind : uint8
	{
		// The block carries no family; the base translates to a literal or to itself.
		Identity,
		// `ACT_RUN` + `KATANA` -> `ACT_RUN_KATANA`. The ordinary case.
		Append,
		// The base's trailing token is a family slot the family replaces:
		// `ACT_SNEAKATTACK_..._BACK` + `KATANA` -> `ACT_SNEAKATTACK_..._KATANA`. The mirror case
		// `ACT_KNOCKBACK_..._BACK` is a literal direction and appends.
		Substitute,
		// The whole base is replaced: `ACT_AIM` -> `ACT_READY_<F>`.
		Rename,
		// The row states its target outright.
		Exception,
	};

	// `base` + `family` -> the translated activity. Pure, and the only place the recovered rewrite
	// is spelled.
	FString Rewrite(const FString& Base, const FString& Family);
	ERewriteKind KindOf(const FString& Base, const FString& Family);

	// The ladder for a retail class name, or for an entity classname a map can author. Null when
	// the class carries no table, which is the ordinary answer for 108 of the 169 subclasses and
	// means "translate nothing", exactly as an unarmed body does.
	const FWeaponLadder* FindLadder(const FString& CppClass);
	const FWeaponLadder* FindLadderByEntityClass(const FString& EntityClassname);

	// The literal a ladder states for one (block, base), or null.
	const TCHAR* FindException(const FWeaponLadder& Ladder, int32 Block, const FString& Base);

	// What one translation answered.
	struct FTranslation
	{
		// The translated activity, or the untranslated base when no rung could be played.
		FString Activity;
		bool bTranslated = false;
		// Which rung answered, counted over the rungs that declare this base rather than over every
		// block — a base absent from block 1 is not a rung the walk skipped, it is a rung the walk
		// never had. 1-based; 0 when nothing resolved.
		int32 Rung = 0;
		// How many rungs declared the base at all.
		int32 ApplicableRungs = 0;
		int32 Block = INDEX_NONE;
		ERewriteKind Kind = ERewriteKind::Identity;
		// The answering row's authored `required` bit. Reported, never acted on.
		bool bRequired = false;
	};

	// Retail's `ActivityOverride`, expressed over the ladder: walk front to back and take the first
	// translated activity `HasActivity` says the body can play. A miss returns the base untranslated
	// and `bTranslated == false`, which is the same answer retail's own empty table gives.
	FTranslation Translate(const FWeaponLadder& Ladder, const FString& Base,
		TFunctionRef<bool(const FString&)> HasActivity);

	// Every base a ladder can be asked for, in first-declared order.
	void CollectBases(const FWeaponLadder& Ladder, TArray<FString>& OutBases);

	// One expanded row, for the round-trip proof. Nothing in the runtime consumes these.
	struct FExpandedRow
	{
		FString CppClass;
		FString Base;
		FString Target;
		bool bRequired = false;
	};

	// The whole recovered stream, in the recovered walk order. Only the round-trip test calls this.
	void ExpandAll(TArray<FExpandedRow>& OutRows);
	// FNV-1a 64 over the expanded stream, byte for byte what the generator hashes.
	uint64 DigestOf(const TArray<FExpandedRow>& Rows);

	// =============================================================================================
	// The player action rules.
	//
	// VtMB's player animation is not a button-to-clip table. `CBasePlayer::PostThink` asks a compact
	// classifier for one of seventeen `PLAYER_*` codes, the `SetAnimation` router gives protected
	// activities first refusal, and the ordinary selector writes three pose parameters, runs an
	// unconditional gait ladder and then lets the code's own arm override what the ladder answered.
	// That selector is what is stored here, as rules: the ladder, one ordered arm per code, the pose
	// writes, the two player-side translations and the effective `Player_Anim` fields.
	//
	// The generated data lives in `ElysiumPlayerActionRules.cpp`. The behaviour is
	// `docs/vtmb/animation_and_movers.md` A.3.
	// =============================================================================================

	enum class EPlayerReach : uint8
	{
		Reachable,
		// Compiled vocabulary with no classifier edge, no native `SetAnimation` caller, no
		// retained-latch writer and no effective `Player_Anim` value. Four of the seventeen, and a
		// measured absence over the whole pinned surface rather than a branch a trace missed.
		Dormant,
	};

	// What one rule row tests. The predicate is the recovered rule; the realized state it reads is
	// the runtime's to supply, which is why a row is data rather than a lambda — a reviewer reads it
	// against the RE record, and the field each one names is in its comment. The offsets are into
	// `CBasePlayer`; the `FL_*` bit meanings are recovered from behaviour and their spellings are
	// conventional, as the image carries no such strings.
	enum class EPlayerPredicate : uint8
	{
		Always,
		// `m_fFlags` (+0x434, `CBaseEntity::GetFlags` at `0x100b3700`) bit 1.
		Ducking,
		// ... bit 0 clear.
		Airborne,
		// Realized 2D speed above zero. Only the `move_yaw` write is gated on it.
		Moving,
		// Realized 2D speed at or below `FPlayerActionTuning::MoveThreshold`.
		BelowMoveThreshold,
		// `speed2D > T || cmdMoveMag > T`, disjunctively over two speeds: `speed2D` is realized
		// velocity and `cmdMoveMag` (+0x19ec) is `hypot(forwardmove, sidemove)` refreshed per user
		// command, so a full-throttle input selects the run on its first frame rather than once the
		// body has accelerated past `T`.
		AboveGaitThreshold,
		// An active weapon whose classname is not `item_w_unarmed`, `IsInCombatStance()` (virtual
		// +0x66c, `0x1015ff40`) true, and not morphed (+0x1edc). The gate on `ACT_AIM`.
		CombatReady,
		// An active weapon with `IsInCombatStance()` false — the gate on the relaxed gaits. Not the
		// negation of `CombatReady`: a morphed body in stance satisfies neither, and so keeps the
		// plain gait.
		Relaxed,
		// The gait ladder already answered a walk or a run, in either form. The landing phases defer
		// to it rather than dropping a moving body into a land.
		GaitIsWalkOrRun,
		// The realized jump/landing phase (+0x1db4) equals the row's `Operand`.
		JumpPhase,
		// `GetActiveWeapon()` is non-null.
		HasActiveWeapon,
		// The active weapon's capability mask (virtual +0x5a0) intersects `0x18000`.
		MeleeCapable,
		// ... intersects `0x6000`, or the weapon's own +0x464 predicate holds.
		RangedCapable,
		// The recovered swim discriminator: realized speed over the swim floor with water level
		// above two, or over the wading floor while airborne, in both cases with velocity past the
		// stroke floor. Its absence is what treads water.
		SwimStroke,
		// Vertical velocity at or above the climb floor.
		ClimbingUp,
		// The live interaction handle (+0x1040) resolves and its entity answers its own activity
		// virtual (+0x84) with something other than `-1`. The row names no literal because the
		// entity supplies one.
		InteractionSupplies,
		// The grapple release word (+0x1d60) carries bit 1.
		ReleasedFeeding,
		// ... bit 2.
		ReleasedSeductive,
		// `m_bSequenceFinished` (+0x65c) is clear, so the current one-shot is still playing.
		SequenceUnfinished,
		// `CBaseCombatCharacter::InPrayer` (`0x1033b7c0`) is false: the current activity (+0xfec)
		// is outside the prayer range.
		NotInPrayer,
		// The prayer hold byte (+0x14a8) is clear, which is what releases the loop into
		// `ACT_PRAYING_END`.
		PrayerReleased,
		// The last applied compact code (+0x1cb4) is not `PLAYER_VOMIT`, so the purge chain has not
		// been entered.
		VomitNotEntered,
		// The purge has drained the blood pool and applied its discipline effect (+0x1cb1), which is
		// what releases the loop into `ACT_VOMIT_GETOUT`.
		PurgeComplete,
		// The stored ideal activity (+0xff0) equals the row's `Operand`.
		IdealActivityIs,

		Count,
	};

	// One row of the gait ladder or of a code's arm. First match wins in both.
	struct FPlayerRule
	{
		// AND-ed, `Always`-padded. Three is the deepest conjunction the recovered selector uses.
		EPlayerPredicate Predicates[3] = { EPlayerPredicate::Always, EPlayerPredicate::Always,
			EPlayerPredicate::Always };
		// What `JumpPhase` and `IdealActivityIs` compare against. Unused by every other predicate.
		int32 Operand = 0;
		// The base activity the row names, or null when the row holds what the selector was already
		// given — `PLAYER_USE` takes the interaction entity's answer, and a retained chain holds its
		// stored ideal while its clip is still playing.
		const TCHAR* Activity = nullptr;
		// The registered activity ID, so a row is joined to the binary's own vocabulary rather than
		// to a spelling. Zero when `Activity` is null.
		int32 ActivityId = 0;
		// The additive layer the row adds beside the base, or null. The attack and reload arms are
		// the only writers, and both leave the base the ladder chose.
		const TCHAR* Layer = nullptr;
		int32 LayerId = 0;
	};

	struct FPlayerAction
	{
		int32 Code = 0;
		const TCHAR* Name = nullptr;
		EPlayerReach Reach = EPlayerReach::Reachable;
		// Where the pinned binary produces this code. Provenance: the remake's own producers are its
		// own, and this is what names the retail path each one stands in for.
		const TCHAR* Producer = nullptr;
		// First match wins; no match leaves the gait ladder's answer standing. Empty for codes `0`
		// and `1`, whose whole selection *is* the ladder, for code `4`, where protected ownership
		// precedes the selector, and for the four dormant codes.
		const FPlayerRule* Rules = nullptr;
		int32 RuleCount = 0;
	};

	enum class EPlayerPoseSource : uint8
	{
		// `UTIL_AngleDiff(anglemod(Facing.Yaw), UTIL_VecToYaw(Velocity))`. Facing is the minuend, so
		// the parameter is right-positive with zero forward — the reverse of `aim_yaw`'s handedness,
		// and a decoder that assumes one convention for both mirrors one of them.
		VelocityAgainstFacing,
		// The row's `Value`, written on every call in every state with every weapon.
		Literal,
		// The player's absolute aim pitch (+0x206c), wrapped by 360.
		AimPitchField,
	};

	struct FPlayerPoseWrite
	{
		const TCHAR* Parameter = nullptr;
		EPlayerPoseSource Source = EPlayerPoseSource::Literal;
		float Value = 0.0f;
		// `Moving` for the one write that is gated: a stationary body holds its last `move_yaw`
		// rather than returning it to zero.
		EPlayerPredicate Gate = EPlayerPredicate::Always;
		// Non-zero only on `move_yaw`, which approaches rather than snaps when the previous write
		// was within `FPlayerActionTuning::PoseSlewWindowSeconds`.
		float SlewDegreesPerSecond = 0.0f;
	};

	// `CBasePlayer::NPC_TranslateActivity` (`0x101647a0`), the player-side translation after the
	// weapon hook. Two rows, and they are what makes an unarmed gait resolve at all: a player body
	// carries no `ACT_WALK_RELAXED` or `ACT_RUN_RELAXED` sequence, only weapon-suffixed ones.
	struct FPlayerTranslation
	{
		const TCHAR* From = nullptr;
		int32 FromId = 0;
		const TCHAR* To = nullptr;
		int32 ToId = 0;
	};

	// A targeted discipline's `Player_Anim` field, resolved through the same seventeen-name table at
	// `0x101ddfb0` before `0x101de660` calls the player's `SetAnimation`. Scanned from the effective
	// (patch-first) vdata search path, so a loose patch file wins over the packed one.
	struct FPlayerAnimField
	{
		const TCHAR* Path = nullptr;
		const TCHAR* Value = nullptr;
		int32 Code = 0;
	};

	struct FPlayerActionTuning
	{
		// The gait ladder's idle floor, in VtMB units: at or below this realized 2D speed the ladder
		// answers idle, aim or crouch.
		float MoveThreshold = 0.0f;
		// The walk/run discriminator is `speed > T`, with `T` the body's own `ACT_WALK` forward cell
		// scaled by `sv_walkscale`, plus this offset. `T` is therefore per-model and only the offset
		// is a constant — and the offset is applied identically in both directions, so it is a fixed
		// bias rather than a hysteresis band. The ladder has no gait memory.
		float GaitThresholdOffset = 0.0f;
		// `move_yaw`'s slew, and the window within which the previous write makes it slew at all.
		float PoseSlewDegreesPerSecond = 0.0f;
		float PoseSlewWindowSeconds = 0.0f;
		// The one compact code the action latch (+0x1cb0 / +0x1cb4) retains. The classifier clears
		// any other retained value, constructors and spawn clear the latch, and the vomit owner is
		// its only setter.
		int32 LatchedCode = 0;
		// An unfinished melee swing refuses an idle or aim request: the selector returns without
		// applying rather than cutting the attack short.
		const TCHAR* MeleeHoldIdeal = nullptr;
		int32 MeleeHoldIdealId = 0;
		const TCHAR* const* MeleeHoldRefuses = nullptr;
		int32 MeleeHoldRefuseCount = 0;
	};

	// What the generator emitted, so a malformed regeneration or a hand-edit fails a test.
	struct FPlayerActionCensus
	{
		int32 Actions = 0;
		int32 Dormant = 0;
		int32 GaitRules = 0;
		// Arm rules, the gait ladder excluded.
		int32 ArmRules = 0;
		int32 PoseWrites = 0;
		int32 Translations = 0;
		int32 AnimFields = 0;
		// Distinct base and layer activities the whole surface names.
		int32 Activities = 0;
		// The predicate vocabulary the emission was generated against.
		int32 Predicates = 0;
	};

	// --- the generated data -------------------------------------------------------------------
	TArrayView<const FPlayerAction> PlayerActions();
	// The idle/aim/crouch/sneak/walk/run choice, which is **not** inside any code's arm: the
	// selector computes it unconditionally right after the pose writes, and codes `0`, `1` and the
	// classifier's `-1` match no arm, so it survives to the apply path verbatim.
	TArrayView<const FPlayerRule> PlayerGaitLadder();
	TArrayView<const FPlayerPoseWrite> PlayerPoseWrites();
	TArrayView<const FPlayerTranslation> PlayerTranslations();
	TArrayView<const FPlayerAnimField> PlayerAnimFields();
	const FPlayerActionTuning& PlayerTuning();
	const FPlayerActionCensus& PlayerCensus();

	// --- the rules over it --------------------------------------------------------------------

	const FPlayerAction* FindPlayerAction(int32 Code);
	const FPlayerAction* FindPlayerAction(const FString& Name);

	// Answers one predicate against realized player state. `Operand` is meaningful only for the two
	// predicates that read it.
	using FPlayerStateQuery = TFunctionRef<bool(EPlayerPredicate Predicate, int32 Operand)>;

	// Whether every predicate on the row holds.
	bool RuleApplies(const FPlayerRule& Rule, FPlayerStateQuery State);
	// The first row of an ordered list whose predicates all hold, or null. Null from an arm means
	// the gait ladder's answer stands; null from the ladder is impossible, since its last row is
	// unconditional.
	const FPlayerRule* SelectRule(TArrayView<const FPlayerRule> Rules, FPlayerStateQuery State);

	// `ACT_WALK_RELAXED` -> `ACT_WALK`, `ACT_RUN_RELAXED` -> `ACT_RUN`, everything else unchanged.
	FString TranslatePlayerActivity(const FString& Activity);

	// Every distinct base and layer activity the player surface names, ladder included, in
	// first-named order. The conformance walk's input.
	void CollectPlayerActivities(TArray<FString>& OutActivities);
}
