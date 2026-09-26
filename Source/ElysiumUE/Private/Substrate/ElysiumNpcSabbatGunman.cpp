#include "Substrate/ElysiumNpcSabbatGunman.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpcKernelShape.h"

const FElysiumNpcClass* FElysiumNpcSabbatGunman::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 465: `0x103a56f0`, ending in a direct call into `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`).
// `0x103a56f0`
void FElysiumNpcSabbatGunman::OnChangeActivity(int32 Activity)
{
	// `CNPC_VSabbatGunman::OnChangeActivity`, 160 bytes. The three convars are
	// `sabbat_gunman_speed_threshold` ("0.1", `+0x28`), `sabbat_gunman_speed_trails` ("3",
	// `+0x2c`) and `sabbat_gunman_speed_scalar` ("3.0", `+0x28`). **SEAM** on the fourth input,
	// `m_flGroundSpeed` (+0x0654): no ground-speed word stands here, so it answers 0 and the
	// stopped arm is the one taken.
	const FMotionTrailPick Pick = SabbatGunmanMotionTrail(/*GroundSpeed*/ 0.f,
		ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::SabbatGunmanSpeedThreshold),
		ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::SabbatGunmanSpeedTrails),
		ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::SabbatGunmanSpeedScalar));
	MotionTrail = Pick.MotionTrail;   // +0x1484

	FElysiumNpc::OnChangeActivity(Activity);   // `0x10295a60`, direct
}

// Slot 546: `0x103a5240`, the class's own schedule id space.
const TCHAR* FElysiumNpcSabbatGunman::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093c1d8`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VSabbatGunman"), TEXT("0x103a5240"), TEXT("0x1093c1d8") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcKernelFacing.cpp` (story 5 step 4) ---

FElysiumNpc::FMotionTrailPick FElysiumNpcSabbatGunman::SabbatGunmanMotionTrail(float GroundSpeed,
	float SpeedThreshold, int32 TrailId, float TrailScalar)
{
	// `CNPC_VSabbatGunman::OnChangeActivity` `0x103a56f0`: at or below the threshold the trail is
	// cleared and the playback scalar is -1.0 — retail's "no scalar" literal, not a speed; above it
	// both come from the other two convars.
	if (GroundSpeed <= SpeedThreshold)
	{
		return FMotionTrailPick{ 0, -1.0f };
	}
	return FMotionTrailPick{ TrailId, TrailScalar };
}

