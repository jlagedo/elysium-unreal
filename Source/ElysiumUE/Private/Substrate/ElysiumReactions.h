#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

#include "ElysiumSwingRecord.h"   // the authored per-attack candidate table the cell is drawn from

// The defender half of the `rules.txt` margin classifier, declared in
// `Substrate/ElysiumWeaponClasses.h`. Forward-declared rather than included: that header pulls in
// the entity chain and the item catalogue, and this one is world-free by design (see below). A
// scoped enum states its own underlying type, so the declaration is complete for a by-value
// parameter and the definition arrives in the `.cpp`.
enum class EElysiumMeleeDefenderReaction : uint8;

// The damage reaction's pure rules — `CBaseCombatCharacter::DamageFlinch` (`0x103229d0`) stated as a
// function over two origins, a facing and a random stream (LIFE5).
//
// World-free, the same shape as `Substrate/ElysiumDice.h`: no entity, no clock, no engine service.
// The stream is the caller's — `FRandomStream` is engine-core math, not a world — so a test drives
// the whole rule off a stream on its own stack with no body at all
// (`Elysium.Substrate.DamageFlinch`), and the producer passes the session's own owned stream.
//
// The recovered behaviour is `docs/vtmb/npc-ai-reverse-engineering.md` -> "Damage condition is not
// animation": the routine randomly selects the head or torso hit activity, derives `hit_yaw` from the
// incoming vector relative to actor yaw, adds a random [-30,+30] degrees, and starts the gesture with
// 0.1/0.3 fade values.
//
// What it does NOT decide is whether a flinch happens at all, nor which clip realizes one. The gate
// is the producer's (`FElysiumCombatCharacter::StartDamageFlinch`) and the clip is the activity
// seam's; this namespace answers only "which activity, at what angle".

namespace ElysiumReactions
{
	// The random spread retail adds to the derived angle, degrees each way. It is wider than the
	// +-22.5 a nine-cell `hit_torso` fan quantizes to, so a hit near a cell edge genuinely reaches
	// either neighbour.
	inline constexpr float FlinchJitterDegrees = 30.0f;

	// The two fades retail hands its gesture call. The ORDER is what the RE recovered (fade in, then
	// fade out) and the numbers are read off that call rather than measured against a capture — so a
	// capture that disagrees corrects one line each and nothing else.
	inline constexpr float FlinchBlendInSeconds = 0.1f;
	inline constexpr float FlinchBlendOutSeconds = 0.3f;

	// One flinch, whole: which of the two hit activities, and the angle the `hit_yaw` pose parameter
	// is steered to.
	struct FElysiumFlinch
	{
		bool bHead = false;
		// (-180, 180], right-positive with zero forward — the `hit_yaw` parameter's own convention,
		// the same one `move_yaw` uses.
		float HitYawDegrees = 0.0f;

		// The ACT_* literal, spelled once for the whole slice so the producer, the record and the test
		// cannot drift into three spellings of one activity.
		const TCHAR* Activity() const;
	};

	// The recovered `AngleDiff(actorYaw, VecToYaw(incoming))` restated in Unreal terms: the BEARING TO
	// THE ATTACKER, measured in the victim's own facing frame.
	//
	// **The vector points from the victim to the attacker, and that sign is load-bearing.** `hit_yaw`
	// steers a fan whose cells are named for where the blow LANDS (`hit_torso_left`,
	// `hit_torso_front_right`, ...), so an attacker standing on the victim's left has to select the
	// left-named cell. Taking the direction the damage TRAVELS instead — victim minus attacker —
	// negates every answer and mirrors the whole fan: an attacker on the left would select
	// `hit_torso_right`. The retail spelling calls its vector `incoming`, which is the same reading:
	// where the hit came FROM, not where it is going.
	//
	// The fan's own wrap corroborates it. A nine-cell fan spans -180..180 with `hit_torso` (the back
	// reaction) at both ends, so the two seam cells are the attacker standing directly behind — which
	// is what a bearing of +-180 means and what the direction of travel would put at zero.
	//
	// Resolved: attacker-left -> -90 (cell 2, `hit_torso_left`); attacker-right -> +90 (cell 6,
	// `hit_torso_right`); attacker-in-front -> 0 (cell 4, `hit_torso_front`); attacker-behind -> +-180
	// (cells 0/8, the shared `hit_torso`).
	//
	// `VictimUnrealYawDegrees` is the victim's UNREAL yaw. An entity-space producer passes
	// `ElysiumSkeletalBasis::FromSourceAngles(Angles).Yaw`, which is `-Angles.Y`; a body-space one
	// passes its actor yaw. Both origins are centimetres in the repo's Unreal-native frame.
	//
	// False when the two origins are horizontally coincident: a hit from directly above or below names
	// no direction on a yaw fan, so there is nothing to answer rather than a zero to invent.
	bool HitYawFrom(const FVector& AttackerOriginCm, const FVector& VictimOriginCm,
		float VictimUnrealYawDegrees, float& OutHitYawDegrees);

	// The random spread: one uniform draw across the whole [-30, +30] band.
	float FlinchJitter(FRandomStream& Rng);

	// The head/torso coin: one uniform draw, even odds either way.
	bool FlinchPicksHead(FRandomStream& Rng);

	// The whole rule in one call: pick the activity, derive the angle, add the jitter, normalize.
	//
	// Two draws off `Rng`, in this order — the coin, then the jitter. They are separate draws because
	// one value driving both would tie the head/torso pick to the sign of the jitter: every head
	// flinch would land on one side.
	//
	// The draws happen only once the direction resolves, so a refused build advances the stream by
	// nothing. False for the same reason `HitYawFrom` is: coincident origins name no direction. `Out`
	// is reset either way.
	bool BuildFlinch(const FVector& AttackerOriginCm, const FVector& VictimOriginCm,
		float VictimUnrealYawDegrees, FRandomStream& Rng, FElysiumFlinch& Out);

	// --- The block family (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions") -----

	// What the ATTACKER plays when its own swing sequence names no blocked reaction. Retail's
	// fallback at `0x10160D00` is this one literal, not a coin: the left/right split is authored per
	// swing in the sequence descriptor's `+0x2E0` and enters from there or not at all.
	inline constexpr const TCHAR* DefaultBlockedReaction = TEXT("ACT_BLOCKED_REACTION_RIGHT");

	// What the DEFENDER plays for a classified melee reaction. The defender callback at
	// `0x10160BC0` classifies the record and branches once: class 3 — the block-stagger band, which
	// is VtMB's whole melee "stagger" — plays `ACT_BLOCK_HEAVY`, and the other blocked classes play
	// `ACT_BLOCK`.
	//
	// Null for `HitKnockback` and `Unclassified`: neither is a blocked class, so neither names a
	// block activity. That is an ordinary answer — a knockback takes the separate normal-hit
	// callback, and an unclassified record means the margin table never loaded, which the classifier
	// itself already reported.
	const TCHAR* BlockActivityFor(EElysiumMeleeDefenderReaction Reaction);

	// CHOSEN, NOT RECOVERED. `WasMeleeBlocked` (`0x10345AB0`) applies a frontal/facing test whose
	// constant is not decoded, so the whole forward hemisphere stands in for it: a blow arriving
	// anywhere in front of the defender can be blocked. The hemisphere is the conservative direction
	// — a tighter cone would REFUSE blocks retail allows, which is a visible loss of a player
	// action, while a wider one only allows blocks retail might refuse.
	inline constexpr float BlockFrontalHalfAngleDegrees = 90.0f;

	// Whether the attacker stands within `BlockFrontalHalfAngleDegrees` of the victim's facing.
	//
	// Built on `HitYawFrom` so the sign convention is the flinch fan's own — one bearing rule for
	// the whole reaction family rather than two derivations that could disagree about which side
	// the attacker is on. False when the two origins are horizontally coincident, for the same
	// reason: a blow from directly above names no direction on a yaw fan, so there is no facing
	// relationship to test rather than a zero to invent.
	bool IsFrontalContact(const FVector& AttackerOriginCm, const FVector& VictimOriginCm,
		float VictimUnrealYawDegrees);

	// --- The grounded knockback family (`docs/vtmb/combat-and-damage.md` § "The authored knockback
	// inputs", `docs/vtmb/animation_and_movers.md` § "The knockback and death corpus") ------------
	//
	// The GROUNDED cells only, and an NPC victim only. The nine-activity flying chain is a separate
	// outcome: its contract is recovered whole — a two-stage velocity assignment with a one-think
	// delay, and a land/wall terminator — but reproducing it needs a motor verb that carries a
	// ballistic body, which the service seam does not have yet. Nothing here moves a body; this
	// namespace answers which cell plays and which yaw the body is turned to, exactly as the flinch
	// half above answers which activity at what angle.
	//
	// **The classification is deterministic; the CELL may draw.** Every geometric function here is a
	// pure function of two origins and a facing and spends nothing. Retail then picks among the
	// candidates the attack's own swing record lists for the selected direction bucket, with
	// `RandomInt` — so a bucket naming one candidate spends nothing and a bucket naming several
	// spends exactly one draw off the caller's stream (`SelectKnockbackActivity`).

	// The two authored sizes. Ten cells exist as `ACT_KNOCKBACK_{SMALL,NORMAL}_HIGH_{FORWARD,BACK,
	// LEFT,RIGHT}` plus `ACT_KNOCKBACK_{SMALL,NORMAL}_LOW_BACK`, each on 155 bodies.
	enum class EKnockbackSize : uint8
	{
		Small,
		Normal,
	};

	// The two authored heights. `Low` exists only for `Back` — the corpus carries no `LOW_FORWARD`,
	// `LOW_LEFT` or `LOW_RIGHT` cell on any body.
	enum class EKnockbackHeight : uint8
	{
		High,
		Low,
	};

	// Retail's four direction buckets, at retail's own indices — the activity table is indexed by
	// them and the yaw-snap offset table below is keyed on the same number, so the values are stated
	// rather than left to declaration order.
	//
	// A bucket answers where `away` points in the victim's own frame, and the token names the CELL
	// that plays. Those are the same direction: the cell token names where the body GOES, and the
	// body goes along `away`. (Body-goes is CONFIRMED, not inferred — a blow to the face plays
	// `..._BACK` because that is the way the struck body travels.)
	enum class EKnockbackDirection : uint8
	{
		Back = 0,      // bucket 0 — `away` points behind the victim
		Left = 1,      // bucket 1 — `away` points to its left
		Forward = 2,   // bucket 2 — `away` points in front of it
		Right = 3,     // bucket 3 — `away` points to its right
	};

	// The four directions in the order the record's buckets rotate through them, which is retail's
	// own cycle: bucket `k` answers direction `(B8 + k) mod 4` over BACK, LEFT, FORWARD, RIGHT. The
	// enum's values ARE those indices, so the cycle is the enum's own order and this constant only
	// names how long it is.
	inline constexpr int32 KnockbackDirectionCount = 4;

	// The record's rotation byte is unstated. `+0xB8` reads `0xFF` on the 639 shipped records that
	// fill fewer than four buckets, and the byte is decoded unsigned, so 255 rather than -1 is what
	// reaches here. A record stating no rotation names no bucket for any direction — there is
	// nothing to rotate — and its cell comes from the fallback instead.
	inline constexpr int32 KnockbackRotationUnset = 0xFF;

	// The record's `+0xBA == 2` unconditional marker, which admits the knockback past the victim's
	// hit-buildup counter however drained it is. 104 shipped records state it, and they are a legible
	// set: every shared weapon's dedicated heavy and every combo finisher, plus `Fists_attack_W2`,
	// `fists_attack_Roundhouse`, `Knife_attack_Kick_Spin` and `manbat`'s four
	// (`docs/vtmb/animation_and_movers.md` § "The knockback and death corpus").
	inline constexpr int32 KnockbackUnconditionalMarker = 2;

	// Retail's recovered fallback when no candidate list is consulted: activity `0x8b`, the
	// flying-into-forward cell, which on a GROUNDED body downgrades to this one. Three routes reach
	// it — a degenerate classification (see `KnockbackRelativeYaw`), an entry carrying no swing
	// record at all, and a record whose rotation byte or selected bucket states nothing.
	inline constexpr const TCHAR* FallbackGroundedKnockbackActivity =
		TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD");

	// Retail's own degeneracy epsilon on the horizontal `away` vector, applied after the z is zeroed.
	// It is tight enough that only exactly-coincident origins reach it.
	inline constexpr float KnockbackDegenerateLength = 1.0e-7f;

	// One knockback, whole: which cell plays, and the yaw the victim is turned to so that cell's
	// model-space direction reads true.
	struct FElysiumKnockback
	{
		EKnockbackDirection Direction = EKnockbackDirection::Back;

		// The Unreal world yaw of `away`, degrees in [0, 360) — the classifier's own input, carried
		// for the log.
		float AwayWorldYawDegrees = 0.0f;
		// `AngleMod(awayYaw - victimYaw)`, degrees in [0, 360) — the value the four bands are cut on.
		float RelativeYawDegrees = 0.0f;
		// The Unreal world yaw the victim's facing is SET to, degrees in [0, 360). Not an offset and
		// not a facing-frame angle: retail snaps the absolute yaw (see `KnockbackSnapYaw`).
		float SnapYawDegrees = 0.0f;

		// Whether the classification degenerated. It is one of the routes to
		// `FallbackGroundedKnockbackActivity`, and the only one this struct can report: the other
		// two are properties of the record, which the classifier never sees.
		bool bFallbackCell = false;
	};

	// The ten cells' literals, one function. Null for a `Low` height on any direction but `Back`:
	// the corpus authors no such cell, so there is nothing to name rather than a spelling to invent.
	//
	// **This is the vocabulary, not the selector.** A melee knockback's cell comes off the attack's
	// own swing record (`SelectKnockbackActivity`); this names what the corpus authors, which is
	// what the record's candidates are drawn from and what the no-record entries resolve against.
	const TCHAR* KnockbackActivity(EKnockbackSize Size, EKnockbackHeight Height,
		EKnockbackDirection Direction);

	// Which of the record's four buckets answers `Direction`, or `INDEX_NONE`.
	//
	// **The buckets are a ROTATION, not a fixed direction order.** Bucket `k` answers direction
	// `(RotationByte + k) mod 4` over the cycle BACK, LEFT, FORWARD, RIGHT — which holds on 948 of
	// 948 records that fill every bucket — so this inverts that: `k = (Direction - RotationByte) mod
	// 4`. A consumer that assumed bucket 0 was one particular way round would be right on a quarter
	// of the corpus and silently wrong on the rest.
	//
	// `INDEX_NONE` when the byte states no rotation (`KnockbackRotationUnset`, or any value outside
	// the cycle). That is an authored absence on 639 records, not a defect.
	int32 KnockbackBucketFor(int32 RotationByte, EKnockbackDirection Direction);

	// The authored cell this record answers `Direction` with, drawn from its own candidate list.
	//
	// Retail's own selection (`CBaseCombatCharacter::GetKnockbackActivity`, `0x103449B0`), in its
	// own order: classify the direction into a bucket, fall back to bucket 0 if that one is empty,
	// then draw. **The candidate NAME is the answer** — it already states its own size, height and
	// direction, so nothing here re-derives them, and this is the path by which the `SMALL` family
	// and the two `LOW_BACK` cells are reached at all.
	//
	// **One draw off `Rng` whenever a bucket holds anything, including a single candidate.** Retail
	// spends `RandomInt(0, count - 1)` unconditionally, so a one-candidate bucket still advances the
	// stream; skipping it would answer the same activity and leave every later reaction in the run
	// reading a different position. A refusal advances the stream by nothing, because the draw
	// happens only once a candidate list is in hand.
	//
	// False when the record states no rotation, or when the selected bucket AND bucket 0 both list
	// nothing. Retail answers that last case with a per-class default activity by direction; nothing
	// here carries one, so the caller's `FallbackGroundedKnockbackActivity` stands in for it — a
	// DIFFERENT fallback from the no-record one, which is the `0x8B` cell.
	bool SelectKnockbackActivity(const FElysiumSwingRecord& Record, EKnockbackDirection Direction,
		FRandomStream& Rng, FString& OutActivity);

	// Whether this victim may be knocked back at all — retail's eligibility, whole. The recovered
	// rule is `docs/vtmb/combat-and-damage.md` → "Who may be knocked back".
	//
	// `bVictimAlive` is retail's own third term, not an approximation of it: retail refuses on
	// `Health == Max_Health`, and because `Health` is damage taken rather than health remaining,
	// that equality is death. Reads are clamped to the max, so it cannot diverge from the death
	// test on an overkill. `bTemplateDisallowsKnockbacks` is the NPC template's authored
	// `General/Disallow_Knockbacks`, set on 17 templates across eight `npctemplate*.txt` files —
	// zombies, cabbies, the tutorial cast and the other bodies that must not be thrown.
	//
	// `bClassBypass` is retail's class-level term, and it comes FIRST because it skips both of the
	// others: a stub returning 0 for every class in the game except `CNPC_VTzimisceRunner`.
	//
	// `bBuildupAdmits` is the victim's hit-buildup gate, already reduced to its answer by the caller
	// — `counter <= npc_hit_buildup_amount` OR the landing swing record's `+0xBA == 2`
	// unconditional marker. It is passed as the answer rather than as the counter because the
	// marker half is a property of the ATTACK and the counter half a property of the VICTIM, and a
	// world-free rule cannot reach either.
	//
	// The order below is retail's own, and it is the whole of the recovered gate: no term is
	// omitted any more.
	bool IsKnockbackAllowed(bool bVictimAlive, bool bTemplateDisallowsKnockbacks,
		bool bBuildupAdmits, bool bClassBypass);

	// The direction the victim is thrown, unnormalized and with its z zeroed: victim minus attacker.
	// **CONFIRMED**, and the sign is the whole of it — the body travels AWAY from the blow.
	FVector KnockbackAwayFrom(const FVector& AttackerOriginCm, const FVector& VictimOriginCm);

	// The same direction when there is no attacker at all — world damage, a script, a logic entity.
	// **CONFIRMED**: retail takes the negated forward of the victim's OWN facing, so a body with
	// nothing to be thrown away from is thrown straight backwards.
	FVector KnockbackAwayWithoutAttacker(float VictimUnrealYawDegrees);

	// `away` and the victim's facing reduced to the one angle the bands are cut on.
	//
	// `OutRelativeYawDegrees` is `AngleMod(awayYaw - victimYaw)` in [0, 360). **The subtraction is
	// that way round because Unreal yaw is Source yaw negated**: retail computes
	// `AngleMod(victimYaw - awayYaw)` in its own frame, and the repo's Y reflection turns that into
	// this. Getting it backwards mirrors the LEFT and RIGHT buckets and nothing else, which is
	// exactly the failure that would survive a forward/back test.
	//
	// False when `away` degenerates below `KnockbackDegenerateLength`. The caller still has a
	// knockback — retail's classifier answers bucket 0 there and its yaw snap still runs off a zero
	// `away` yaw — but the cell comes from `FallbackGroundedKnockbackActivity` instead of the bucket.
	// Both out-parameters are zeroed on that path.
	bool KnockbackRelativeYaw(const FVector& AwayCm, float VictimUnrealYawDegrees,
		float& OutAwayWorldYawDegrees, float& OutRelativeYawDegrees);

	// **CONFIRMED** — retail's four bands over that angle, kept exactly as cut. They are NOT
	// symmetric: the front band runs (316, 360) + [0, 45] and is 89 degrees wide, the right flank
	// and the rear are 90 each, and the left flank absorbs the remaining 91. The asymmetry is
	// reproduced rather than rounded off.
	//
	//   rel > 316 or rel <= 45   -> bucket 2, `Forward`
	//   rel <= 135               -> bucket 3, `Right`
	//   135 < rel <= 225         -> bucket 0, `Back`
	//   otherwise (225 < rel<=316) -> bucket 1, `Left`
	EKnockbackDirection KnockbackDirectionForRelativeYaw(float RelativeYawDegrees);

	// **CONFIRMED, and it applies to grounded NPCs too.** After classifying, retail SETS the victim's
	// yaw to `AngleMod(awayYaw + offset)` so the authored clip's model-space direction points along
	// `away`: a `..._LEFT` cell only reads as a body thrown leftwards if the body's left IS the way
	// it is going.
	//
	// Retail's offsets are `{0: +180, 1: +270, 2: +0, 3: +90}` in SOURCE yaw. Unreal yaw is Source
	// yaw negated, so each offset negates with it and the table here is `{Back: +180, Left: +90,
	// Forward: +0, Right: +270}`. Answers degrees in [0, 360).
	float KnockbackSnapYaw(float AwayWorldYawDegrees, EKnockbackDirection Direction);

	// The whole rule in one call: classify, snap, select. **No stream, no draw, no refusal** — a
	// contact that reached this call is a knockback, and every term above is deterministic. `Out` is
	// fully written.
	void BuildKnockback(const FVector& AwayCm, float VictimUnrealYawDegrees, FElysiumKnockback& Out);
}
