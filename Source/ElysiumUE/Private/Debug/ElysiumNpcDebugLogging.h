#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntity;
class FElysiumEntityWorld;
class FElysiumNpc;
enum class EElysiumScheduleId : uint8;

// Requirement 23: Visual Logger events attached to the NPC's body actor. Compiled to empty
// inlines outside a Visual Logger build so the Substrate callers stay unguarded and pay nothing.
#define ELYSIUM_NPC_VLOG (!UE_BUILD_SHIPPING && ENABLE_VISUAL_LOG)

// One sighting that passed the whole Senses::Tick chain: range (or one of its two bypasses),
// cone and visibility. The flags are the ones Senses computed for its own gate, not re-derived.
struct FElysiumNpcSightingDebug
{
	// VisionDistanceCm times the target's stealth vision scalar; the radius the range test used.
	float EffectiveRadiusCm = 0.0f;
	// The target's stealth cone scalar, applied to the cosine inside the cone test.
	float TargetConeScalar = 1.0f;
	bool bOuterBand = false;
	bool bRangeBypass = false;
	bool bDamageOverride = false;
};

namespace ElysiumNpcDebugLogging
{
#if ELYSIUM_NPC_VLOG
	void Sighting(const FElysiumNpc& Npc, const FElysiumEntity& Candidate,
		const FElysiumNpcSightingDebug& Sighting);
	void EnemyChoice(const FElysiumNpc& Npc, const FElysiumEntityWorld& World,
		const FElysiumEntityHandle& OldEnemy, const FElysiumEntityHandle& NewEnemy);
	void ScheduleInstalled(const FElysiumNpc& Npc, EElysiumScheduleId Schedule);
#else
	inline void Sighting(const FElysiumNpc&, const FElysiumEntity&,
		const FElysiumNpcSightingDebug&) {}
	inline void EnemyChoice(const FElysiumNpc&, const FElysiumEntityWorld&,
		const FElysiumEntityHandle&, const FElysiumEntityHandle&) {}
	inline void ScheduleInstalled(const FElysiumNpc&, EElysiumScheduleId) {}
#endif
}
