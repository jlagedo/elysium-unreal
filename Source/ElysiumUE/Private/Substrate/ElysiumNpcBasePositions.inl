// `CAI_BaseNPC`'s declarations of the `Positions` family (story 5 step 5),
// moved from `ElysiumNpcPositions*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBasePositions.cpp`.

// +0x5d48 `m_UnreachableEnts` / +0x5d54 its count — `CAI_BaseNPC::IsUnreachable`'s cache, one 0x14
// byte record per entry. The shape map's row for `+0x5d48` still reads ABSENT ("UnreachableEnt_t has
// no port counterpart"); this family gave it one, so that row wants replacing with a WORD row.
struct FUnreachableEntity
{
	FElysiumEntityHandle Entity;                 // +0x00
	double ExpiresAt = 0.0;                      // +0x04, an absolute curtime, carried as double
	FVector PositionCm = FVector::ZeroVector;    // +0x08..+0x10, where it stood when it was recorded
};

TArray<FUnreachableEntity> UnreachableEnts;

// +0x0fec `m_Activity` — the activity number `CNPC_VTzimisce`'s slot 389 override switches on. Below
// the shape map's band, so 29b did not bind it; this runtime names activities and carries retail's
// registered number beside them, exactly as family Facing's `IdealActivityNumber` (+0x0ff0) does.
int32 ActivityNumber = 0;
