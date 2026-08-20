#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

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
}
