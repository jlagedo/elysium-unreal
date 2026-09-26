// `CAI_BaseNPC`'s declarations of the `Senses10` family (story 5 step 5),
// moved from `ElysiumNpcSenses10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSenses10.cpp`.

/** SEAM for `0x10272f40`, the shadow-physics builder slot 223 guards. Retail: refuse when slot 94
 *  answers `7`, destroy any existing object, `VPhysicsInitShadow(true, false, NULL)`, set the mass
 *  from the model's own `mass` keyvalue or — at or below `_DAT_104454c4` (**0.0**) — `90` for a male
 *  body and `65` for a female one, and set the damping from the summed hull extents times
 *  `_DAT_10449270` (**0.5**) squared. This runtime stands no physics object (`m_pPhysicsObject`
 *  `+0x36c` has no port word), so the builder RECORDS the mass it chose and creates nothing; the
 *  slot's own answer is `true` either way, which is retail's. */
struct FVPhysicsShadowBuild
{
	bool bBuilt = false;
	float MassKg = 0.f;
	float Damping = 0.f;
};


/** `CAI_MoveAndShootOverlay+0x18`, the re-arm stamp slot 445 writes. `0x102e8250` disables the
 *  overlay by storing `FLT_MAX` (`0x7f7fffff`) into it; `0x102e8270` re-arms it to
 *  `curtime + overlay+0x2c` after re-deriving the shot counts from the active weapon.
 *
 *  **SEAM**: this runtime stands no `CAI_MoveAndShootOverlay`. The stamp, the two pause bounds and
 *  the disable/arm decision ARE what slot 445 decides, so they are recorded; the overlay's own
 *  weapon-data re-derivation (`+0x3a4`/`+0x3a8`) is the overlay's story. `0x102e8270`'s own fallback
 *  — state 4, no weapon, or neither the `0x11` nor the `0x15` activity sequence — lands on the same
 *  disable, and this runtime has no activity-sequence table, so the fallback is named and the arm
 *  is taken. */
struct FMoveAndShootOverlay
{
	// +0x18. `FLT_MAX` is retail's own "disabled" value, and it is the shipping default.
	float NextShotTime = MAX_flt;
	float PauseMin = 0.f;    // +0x24, from m_flBurstShootPauseMin (+0x5bbc)
	float PauseMax = 0.f;    // +0x28, from m_flBurstShootPauseMax (+0x5bc0)
	int32 Disables = 0;      // how many times 0x102e8250 ran
	int32 Arms = 0;          // how many times 0x102e8270 ran
	int32 UpdateCalls = 0;   // 0x102e8560, reached by RunTaskOverlay 0x10289c90
};

FMoveAndShootOverlay MoveAndShootOverlay;

FVPhysicsShadowBuild VPhysicsShadow;

bool bHasPhysicsObject = false;   // +0x36c m_pPhysicsObject, as a "is one standing" answer

/** SEAM for `CBaseCombatCharacter::CreateSecondaryDiscParticles(m_vDiscBloodType)` — the explosive
 *  gib's particle burst, keyed on `m_vDiscBloodType` (`+0xfd0`). Visual only and no particle system
 *  reaches this substrate; counted so the arm is observable. */
int32 SecondaryDiscParticleBursts = 0;

/** `m_iSquadDisconnected` (`+0x5bb0`) and the squad word (`+0x5da4`) that slot 544's first gate
 *  reads off BOTH this NPC and the candidate. **SEAM**: this substrate stands no squad object
 *  (`ConnectedSquad()` answers null), so the squad word answers `0` — retail's own "no squad" value,
 *  which makes the gate's third term false and lets every candidate through. That is the admitting
 *  arm, so nothing is silently refused. `m_iSquadDisconnected` is a real per-NPC word with no
 *  port producer and ships at `0`, which is retail's connected state. */
int32 SquadDisconnected = 0;    // +0x5bb0
