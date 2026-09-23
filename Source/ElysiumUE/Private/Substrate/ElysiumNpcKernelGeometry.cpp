#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Geometry** — the six eye/anchor slots (193, 194, 195, 197, 533 and
// `CNPC_Crow`'s override of 192), the hull-bit query (slot 337), `SetSize` (slot 213) and the four
// bodies that push one body out of another. The declarations, the family's four standing facts and
// every seam are `Substrate/ElysiumNpcKernelGeometry.inl`; the walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// Every constant below was read out of retail `vampire.dll`'s `.rdata` at the cell the decompiled C
// names (image base `0x10000000`, `.rdata` VA `0x10445000` at file offset `0x445000`), the method
// families Species and Senses used. Where a cell is a DOUBLE the listing says so with
// `FCOMP double ptr` and the comment repeats it — `_DAT_10449260` read as a float is 0.0 and the
// tentacle scatter cone would admit the whole forward half-plane instead of a 76-degree one.

namespace
{
	// --- Retail `.rdata`, one line per constant -----------------------------------------------------

	// `_DAT_1044bef8` = 0.25f. `BodyTarget`'s anchor drop: the fraction of the centre-to-origin
	// delta subtracted from the bounds centre to get the point the blend starts at.
	constexpr float GBodyTargetAnchorFraction = 0.25f;

	// The pooled half, read by both of this family's bodies that need one — `BodyTarget`'s plain
	// midpoint arm and `StandingOnPlayer`'s half-diagonal.
	constexpr float GRetailHalf = ElysiumNpcTunables::Half;

	// `BodyTarget`'s two noise draws, `RandomFloat(0, 0.5)` twice (`PUSH 0x3f000000; PUSH 0x0`). The
	// arm adds BOTH, so the blend parameter spans 0..1 with a triangular distribution rather than
	// the uniform 0..0.5 a single draw would give.
	constexpr float GBodyTargetNoiseMin = 0.f;
	constexpr float GBodyTargetNoiseMax = 0.5f;

	// `_DAT_1046bac0` = 6.0f, SOURCE units — `CNPC_Crow::vfunc192`'s Z lift over `GetOrigin()`.
	constexpr float GCrowCentreLiftUnits = 6.0f;

	// The fixed eye-offset override `CAI_BaseNPC::FUN_10274db0` answers when the debug-overlay bit is
	// set: an immediate `0x41c00000` on Z with X and Y zeroed, i.e. 1.5 SOURCE units.
	constexpr float GDebugEyeOffsetZUnits = 1.5f;
	constexpr uint32 GDebugOverlayEyeOffsetBit = 0x8000000u;
	constexpr int32 GDebugEyeOffsetActivityA = 0x57;
	constexpr int32 GDebugEyeOffsetActivityB = 8;

	// The image's shared zero. `ResolveStandingOnHead` tests the XY delta against it for EXACT
	// equality, which is what selects the four-diagonal arm.
	constexpr float GGeometrySharedZero = ElysiumNpcTunables::Zero;

	// The two cells, with the immediates `0x3f34fdf4` and `0xbf34fdf4` (the same two numbers) stored
	// to the X slot. Not 1/sqrt(2) to full precision — retail's is the three-digit 0.707, and the
	// vector it makes is 0.99985 long, not 1.
	constexpr float GDiagonalPlus = ElysiumNpcTunables::StandOnHeadSpringPositive;
	constexpr float GDiagonalMinus = ElysiumNpcTunables::StandOnHeadSpringNegative;

	// `RandomFloat(-0.1, 0.1)` (`PUSH 0xbdcccccd; PUSH 0x3dcccccd`), the jitter added to the
	// normalised away-direction before it is normalised a SECOND time.
	constexpr float GStandingOnHeadJitter = 0.1f;

	// `_DAT_1049a1f4` = 0.1f SOURCE units, the Z lift applied to the trace start and subtracted back
	// off the destination; `_DAT_1049a1f8` = 40.0f, the push speed in SOURCE units per second.
	constexpr float GStandingOnHeadLiftUnits = 0.1f;
	constexpr float GStandingOnHeadSpeedUnits = 40.0f;

	// Seconds — the ceiling `m_flStandingOnHeadTimer` ramps to.
	constexpr float GStandingOnHeadTimerCeiling = ElysiumNpcTunables::Five;

	// The pooled 1.0f, read twice as a CLEAR trace fraction (`ResolveStandingOnHead`) and once as a
	// one-second interval (`UpdateFakeHull`'s damage gate).
	constexpr float GGeometryTraceClearFraction = ElysiumNpcTunables::One;
	constexpr double GFakeHullPushIntervalSeconds = static_cast<double>(ElysiumNpcTunables::One);

	// `0x202400b` — the trace mask both of `ResolveStandingOnHead`'s hull traces use, the same one
	// family Motor records for `CheckOnGround`, `ValidateNavGoal` and `GetGroundpoint`.
	constexpr int32 GStandingOnHeadTraceMask = 0x202400b;

	// `_DAT_10462914` = 1.25f — `UpdateFakeHull` scales the hull's own mins and maxs by it before it
	// builds the box around the `Bip01` bone point.
	constexpr float GFakeHullExtentScale = 1.25f;

	// `_DAT_10457f5c` = 500.0f — the force scale on the damage packet, applied to the bone point's
	// movement since the previous call. SOURCE units.
	constexpr float GFakeHullForceScale = 500.0f;

	// The packet `UpdateFakeHull` builds: `CTakeDamageInfo(this, this, 20.0, 1, 0, 0, -1)` with
	// `0x41a00000` = 20.0 as the scalar, then `0x101c2a10(2)`, `0x101c2b10(1)` and `0x101c2a50(1.0)`.
	constexpr float GFakeHullDamage = 20.0f;

	// The three knockback activities slot 323's classification picks between (`0x79` default, `0x7a`
	// for class 1, `0x7b` for class 3) and the bone `UpdateFakeHull` measures.
	constexpr int32 GFakeHullActivityDefault = 0x79;
	constexpr int32 GFakeHullActivityClassOne = 0x7a;
	constexpr int32 GFakeHullActivityClassThree = 0x7b;
	const TCHAR* const GFakeHullBoneName = TEXT("Bip01");

	// `CPayphone::vfunc193`'s one bone name, `s_Phone_bone_01_1059537c`.
	const TCHAR* const GPayphoneBoneName = TEXT("Phone_bone_01");

	// `_DAT_1046dcd0` = 128.0f SOURCE units — the range gate on `CoordinateTroops`' severed-tentacle
	// scatter. Compared against the LENGTH `VectorNormalize` answers, inclusively (`AND EAX,0x4100`
	// keeps both the below and the equal flags).
	constexpr float GScatterRangeUnits = 128.0f;

	// `_DAT_10449260`, a **DOUBLE** (`1039997d  FCOMP double ptr [0x10449260]`) = 0.25. The 2-D dot
	// floor on the same gate: a 75.5-degree half-angle in front of `m_vecForward`. Read as a float
	// the cell is 0.0 and the gate becomes the whole forward half-plane.
	constexpr double GScatterForwardDotFloor = 0.25;

	// The two forced-schedule ids `0x103998d0` refuses to scatter a tentacle out of. What each one
	// IS is not a fact of this family's rows; they are carried by number, as retail compares them.
	constexpr int32 GScatterRefusedScheduleA = 0x163;
	constexpr int32 GScatterRefusedScheduleB = 0x165;

	// `m_ePhase` (+0x6670) must read exactly this before a severed tentacle will scatter.
	constexpr int32 GScatterRequiredPhase = 2;

	// `m_rhSeveredTentacles` is a fixed SIX-entry array in retail and both scatter bodies walk all
	// six unconditionally (`iVar4 = 6; do { … } while (--iVar4)`).
	constexpr int32 GSeveredTentacleCount = 6;
}

// =================================================================================================
// Slot 193 — `EyePosition`, `0x100b4b40` with `0x101aae60` and `0x1025e8e0` in front of it
// =================================================================================================

const FElysiumNpc::FEyePositionSpecies* FElysiumNpc::EyePositionSpeciesRows(int32& OutCount)
{
	// Slot 193 is filled by 713 classes across both modules; these are the two whose body is not
	// `CAISound::FUN_100b4b40` AND whose class is in the `CAI_BaseNPC` census this leaf dispatches
	// over. `CBaseCineCam` (`0x1006d910`) and `CBasePlayer` (`0x100b7f70`) are not NPC classes and
	// `CItemContainerLock` (`0x102243c0`) is an item; none of the three is reachable from here.
	static const FEyePositionSpecies Rows[] =
	{
		{ TEXT("CPayphone"),        TEXT("0x101aae60") },
		{ TEXT("CAI_BaseHumanoid"), TEXT("0x1025e8e0") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FEyePositionSpecies* FElysiumNpc::EyePositionSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FEyePositionSpecies* Rows = EyePositionSpeciesRows(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FCString::Strcmp(Rows[Index].RetailClass, InRetailClass) == 0)
		{
			return &Rows[Index];
		}
	}
	return nullptr;
}

FVector FElysiumNpc::EyePosition() const
{
	// The dispatch, walking the census chain exactly as the vtable does: the nearest class at or
	// above mine that replaces slot 193 wins, and a class that replaces nothing lands on the Troika
	// line's `CAISound::FUN_100b4b40`.
	const FElysiumNpcClass* Cls = RetailClass();
	const TCHAR* SlotBody = ElysiumNpcKernelClass::BodyOf(Cls, 193);

	// `CPayphone::vfunc193` `0x101aae60`, 95 bytes:
	//
	//     bone = LookupBone("Phone_bone_01");
	//     if (bone == -1) { return CAISound::FUN_100b4b40(this, out); }
	//     GetBonePosition02(bone, &pos, &ang);
	//     return pos;
	//
	// The bone's own world position, with NO view offset added and the angles thrown away. The
	// payphone's "eye" is its handset, which is what a dialogue camera and a `LookAtEntityEye` aim
	// at. `npc_payphone` is both a census classname and a registered spawn leaf, so this arm is
	// reachable from a map.
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x101aae60")) == 0)
	{
		FVector BoneCm = FVector::ZeroVector;
		if (BoneWorldPosition(GPayphoneBoneName, BoneCm))
		{
			return BoneCm;
		}
		// `LookupBone` answered -1 — retail's own fall-through to the base body.
		return FElysiumCombatCharacter::EyePosition();
	}

	// `CAI_BaseHumanoid::vfunc193` `0x1025e8e0`, 40 bytes: `thunk_FUN_1025e7b0(this)` then the three
	// cached words at `+0x5f50`/`+0x5f54`/`+0x5f58`. The refresh (`0x1025e7b0`) is lazily latched on
	// two bits of `+0x5f4c`: bit 1 guards the eye point itself, which comes from a NAMED attachment
	// and falls back to `CBaseEntity::EyePosition()` plus `GetAngles()` when the model has no such
	// attachment, and bit 0 guards a second cached vector from that eye to slot 278. No class in the
	// spawnable census derives from `CAI_BaseHumanoid` — the Troika line does not — so this arm is
	// reached only by retail class name.
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x1025e8e0")) == 0)
	{
		FVector CachedCm = FVector::ZeroVector;
		if (HumanoidEyeCache(CachedCm))
		{
			return CachedCm;
		}
		// The attachment is missing, which is the arm `0x1025e7b0` itself takes: it seeds the cache
		// from `CAISound::FUN_100b4b40`, so the cached read answers the base body.
		return FElysiumCombatCharacter::EyePosition();
	}

	// `CAISound::FUN_100b4b40` `0x100b4b40`, 85 bytes, the Troika line's own and the body 21 classes
	// in this family and 24 call sites reach: `GetAbsOrigin()` (slot 217) plus `m_vecViewOffset`
	// (`+0x0184`), component by component. `FElysiumCombatCharacter::EyePosition()` is that sum with
	// the standing view offset as the port's `m_vecViewOffset`.
	return FElysiumCombatCharacter::EyePosition();
}

bool FElysiumNpc::BoneWorldPosition(const TCHAR* /*BoneName*/, FVector& /*OutPositionCm*/) const
{
	// SEAM. `CBaseAnimating::LookupBone` + `GetBonePosition02` for the payphone, and
	// `GetBoneTransform` + two `VectorTransform`s for the werewolf's `Bip01`. Nothing in this
	// substrate hands the kernel a bone table.
	++BoneWorldPositionCalls;
	return false;
}

bool FElysiumNpc::HumanoidEyeCache(FVector& /*OutPositionCm*/) const
{
	// SEAM for `0x1025e7b0`'s attachment read. The attachment NAME is **unrecovered**: the string
	// pointer is `&DAT_105c8ed8` and the decompiler folded the `PUSH` away.
	return false;
}

// =================================================================================================
// Slots 194 and 195 — `EyeAngles` `0x100b4bc0` and `LocalEyeAngles` `0x100b4be0`
// =================================================================================================

void* FElysiumNpc::EyeAngles()
{
	// `0x100b4bc0`, EIGHT bytes and no frame:
	//
	//     100b4bc0  MOV EAX,dword ptr [ECX]
	//     100b4bc2  JMP dword ptr [EAX + 0x36c]
	//
	// `+0x36c` is slot 219, `GetAbsAngles()`. An NPC's eye angles ARE its body angles in this
	// engine — there is no separate head orientation at this slot — and 82 classes fill 194 with
	// this exact tail call.
	return GetAbsAngles();
}

void* FElysiumNpc::LocalEyeAngles()
{
	// `0x100b4be0`, the same eight bytes with `+0x374` — slot 221, `GetAngles()`, the LOCAL angles.
	// 80 classes fill 195 with it.
	return GetAngles();
}

// =================================================================================================
// Slot 197 — `BodyTarget(const Vector&, bool, bool)`, `0x102789c0`
// =================================================================================================

FVector FElysiumNpc::BodyTargetAnchor(const FVector& CentreCm, const FVector& OriginCm)
{
	// `0x102789c0`'s head, from the listing (`102789c6`..`10278a23`): slot 192 `WorldSpaceCenter()`,
	// slot 217 `GetAbsOrigin()`, the delta between them scaled by `_DAT_1044bef8` (0.25), and then
	// slot 192 dispatched a SECOND time and the scaled delta subtracted from THAT.
	//
	// Both dispatches answer the same vector, so the anchor is the bounds centre pulled a quarter of
	// the way back down toward the feet. The second dispatch is not redundant in retail — a class
	// whose `WorldSpaceCenter` reads an animated bound could answer differently between the two —
	// but on every body in this family it is the same point, and it is written once here.
	const FVector Delta = (CentreCm - OriginCm) * GBodyTargetAnchorFraction;
	return CentreCm - Delta;
}

FVector FElysiumNpc::BodyTargetBlend(const FVector& AnchorCm, const FVector& EyeCm, bool bNoisy,
	bool bAimAtEyeExactly, float Noise1, float Noise2)
{
	// The three arms, in the listing's order (`10278a84` tests the SECOND bool first).
	const FVector Span = EyeCm - AnchorCm;

	if (bNoisy)
	{
		// `10278a8a`..`10278b32`: TWO independent `RandomFloat(0, 0.5)` draws, and the span is added
		// once scaled by each. `Anchor + Span*(r1 + r2)` — a triangular 0..1 blend whose mode is the
		// midpoint, NOT one uniform 0..0.5 draw. Aim spread on this engine's NPCs is that sum.
		return AnchorCm + Span * Noise1 + Span * Noise2;
	}

	if (bAimAtEyeExactly)
	{
		// `10278b3c`: three word copies straight out of the slot-193 result. The anchor is not
		// consulted at all on this arm.
		return EyeCm;
	}

	// `10278b56`: `Anchor + Span * _DAT_104454d0` — the plain midpoint between the lowered centre
	// and the eye.
	return AnchorCm + Span * GRetailHalf;
}

FVector FElysiumNpc::BodyTarget(const FVector& /*PosSrc*/, bool bNoisy, bool bAimAtEyeExactly)
{
	// `0x102789c0`, 518 bytes. **`posSrc` is never read.** The retail signature takes a
	// `const Vector&` (the caller's own eye point, in Valve's SDK the thing the spread cone is
	// measured from) and the body's 0x10 bytes of stack arguments are the return buffer, that
	// reference, and the two bools — of which only the two bools and `this` reach an instruction.
	// A shipped program was tuned against that, so the parameter stays and stays unread.
	//
	// Slot 192 `WorldSpaceCenter()` is another story's row and is still a generated stub here, so
	// the anchor it feeds is the zero vector until that lands. The wiring is the deliverable; the
	// formula is asserted through `BodyTargetAnchor` / `BodyTargetBlend`.
	const FVector CentreCm = WorldSpaceCenter();
	const FVector AnchorCm = BodyTargetAnchor(CentreCm, Origin);
	const FVector EyeCm = EyePosition();

	float Noise1 = 0.f;
	float Noise2 = 0.f;
	if (bNoisy)
	{
		// `(**(code **)(*DAT_1070b244 + 4))(0, 0x3f000000)` twice. Drawn here rather than inside
		// `BodyTargetBlend` so the blend stays measurable, and drawn BOTH times even though the two
		// products are added, because retail draws twice and a stream's position is observable.
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		Noise1 = Stream.FRandRange(GBodyTargetNoiseMin, GBodyTargetNoiseMax);
		Noise2 = Stream.FRandRange(GBodyTargetNoiseMin, GBodyTargetNoiseMax);
	}
	return BodyTargetBlend(AnchorCm, EyeCm, bNoisy, bAimAtEyeExactly, Noise1, Noise2);
}

// =================================================================================================
// Slot 192 — `CNPC_Crow::vfunc192`, `0x10357760`
// =================================================================================================

FVector FElysiumNpc::SpeciesWorldSpaceCenter() const
{
	// `0x10357760`, 69 bytes. Three chained dispatches of slot 220 `GetOrigin()` (vtable `+0x370`),
	// whose results are kept in EDI, EBX and EAX, and then:
	//
	//     out.x = EBX[0]          (the SECOND call's X)
	//     out.y = EDI[1]          (the FIRST call's Y)
	//     out.z = EAX[2] + 6.0    (the THIRD call's Z, plus `_DAT_1046bac0`)
	//
	// All three answer the same vector, so the whole of it is `GetOrigin() + (0, 0, 6)`. The crow
	// does not use the base body's bounds midpoint: its centre is a fixed lift off its own origin,
	// which is what keeps a flying bird's centre from breathing with the wing animation.
	if (ElysiumNpcKernelClass::DerivesFrom(RetailClass(), TEXT("CNPC_Crow")))
	{
		// Slot 220 `GetOrigin()` is the LOCAL origin, which is this chain's `Origin`.
		return Origin + FVector(0.f, 0.f, GCrowCentreLiftUnits * ElysiumMove::U);
	}

	// Every other class takes the Troika line's `0x10027160`, slot 192's own body, which is another
	// story's row and is still a generated stub.
	return const_cast<FElysiumNpc*>(this)->WorldSpaceCenter();
}

// =================================================================================================
// Slot 213 — `SetSize(const Vector&)`, `0x100b1890`
// =================================================================================================

void FElysiumNpc::SetSize(const FVector& InSizeCm)
{
	// `0x100b1890`, 146 bytes, of which 132 are the scope-trace push and pop: the body reads
	// `m_iName` (`+0x026c`) purely to label a crash-report breadcrumb (`"CBaseEntity::SetSize"`,
	// with `"NULL ENTITY"` for a null `this` and the empty string for an unnamed entity), pushes the
	// row, writes the three words, and pops. The breadcrumb stack has no observable effect on any
	// program and is not reproduced.
	//
	// The three writes ARE the body: `m_vecSize` (`+0x038c`) and the two words after it. Nothing in
	// layers 0–9 reads them but slot 214 `GetSize`. Unreal's collision component is the eventual
	// host for an actor's bounds; until then this member is what the kernel sees.
	SizeCm = InSizeCm;
}

// =================================================================================================
// Slot 337 — `GetUsedHullBits`, `0x1029a050` and its seven species replacements
// =================================================================================================

const FElysiumNpc::FUsedHullBitsSpecies* FElysiumNpc::UsedHullBitsSpeciesRows(int32& OutCount)
{
	// Every class in the `CAI_BaseNPC` census that fills slot 337 with something other than
	// `CAI_BaseNPCTroika::GetUsedHullBits`. The three Tzimisce rows are the ones the decompiled C
	// gets wrong: it shows a bare forward with no bit added, and the listing shows `OR AH,<imm>` —
	// a byte-wide OR into bits 8..15, which is `| 0x400`, `| 0x800` and `| 0x2000`.
	static const FUsedHullBitsSpecies Rows[] =
	{
		// --- The OR-onto-the-base shape ---
		{ TEXT("CNPC_VTzimisce"),         TEXT("0x103b9160"), 0x0400,   false },
		{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("0x103c1cb0"), 0x0800,   false },
		{ TEXT("CNPC_VTzimisceRunner"),   TEXT("0x103c3cb0"), 0x2000,   false },
		{ TEXT("CNPC_VMingXiao"),         TEXT("0x10392a50"), 0x38000,  false },
		{ TEXT("CNPC_VScurrying"),        TEXT("0x103ac4e0"), 0x80000,  false },
		{ TEXT("CNPC_VRat"),              TEXT("0x103ac4e0"), 0x80000,  false },
		// --- The answer-a-bare-constant shape: no call up the chain, so bit 0 is NOT set ---
		{ TEXT("CNPC_VCamera"),           TEXT("0x10368e80"), 0x80,     true },
		{ TEXT("CNPC_VCameraSecurity"),   TEXT("0x10368e80"), 0x80,     true },
		{ TEXT("CNPC_VGargoyle"),         TEXT("0x10378680"), 0x4000,   true },
		{ TEXT("CNPC_VHengeyokai"),       TEXT("0x1037fb20"), 0x40001,  true },
		{ TEXT("CNPC_VManBat"),           TEXT("0x1038b100"), 0x100000, true },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039c480"), 0x38000,  true },
		{ TEXT("CNPC_VSheriffMan"),       TEXT("0x103ae840"), 0x200000, true },
		{ TEXT("CNPC_VWerewolf"),         TEXT("0x103cab50"), 0x1000,   true },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FUsedHullBitsSpecies* FElysiumNpc::UsedHullBitsSpeciesOf(
	const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FUsedHullBitsSpecies* Rows = UsedHullBitsSpeciesRows(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FCString::Strcmp(Rows[Index].RetailClass, InRetailClass) == 0)
		{
			return &Rows[Index];
		}
	}
	return nullptr;
}

int32 FElysiumNpc::GetUsedHullBits()
{
	// The Troika line, `0x1029a050`:
	//
	//     1029a050  CALL 0x10012c1a           ; CAI_BaseNPC::GetUsedHullBits, 0x10270820
	//     1029a055  OR AL,0x1
	//     1029a057  RET
	//
	// and `0x10270820` is the same two instructions over `CBaseCombatCharacter::GetUsedHullBits`
	// (`0x10341710`), whose whole body past the breadcrumb pair is `return 1`. So the base answer is
	// 1 and BOTH ORs are no-ops — recovered, not a transcription slip, and the reason 29c's walk of
	// `0x1029a050` names `CBaseCombatCharacter` while the listing calls `CAI_BaseNPC`: the chain is
	// three deep and every rung adds the same bit.
	int32 Bits = BaseCombatCharacterHullBits;

	// The species dispatch, walking the census chain as the vtable does.
	const FElysiumNpcClass* Cls = RetailClass();
	const TCHAR* SlotBody = ElysiumNpcKernelClass::BodyOf(Cls, 337);
	if (SlotBody == nullptr)
	{
		// No census class claims this classname — `npc_VCop` is the recovered example — so the
		// dispatch lands on the Troika line, which is exactly what a null answer means.
		return Bits;
	}

	int32 Count = 0;
	const FUsedHullBitsSpecies* Rows = UsedHullBitsSpeciesRows(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FCString::Strcmp(Rows[Index].Body, SlotBody) != 0)
		{
			continue;
		}
		// `bReplaces`: seven species answer a bare `return <imm>` and never call up, so bit 0 is
		// absent from their answer. The other six call the Troika body and OR onto its 1.
		return Rows[Index].bReplaces ? Rows[Index].Bits : (Bits | Rows[Index].Bits);
	}
	return Bits;
}

// =================================================================================================
// Slot 533 — `EyeOffset(Activity, Activity)`, `0x102b4ab0` over `0x10274db0`
// =================================================================================================

const int32* FElysiumNpc::HintEyeOffsetActivities(int32& OutCount)
{
	// `0x102b4ab0`'s `switch`, in the listing's case order. Six activities and no others.
	static const int32 Activities[] = { 0x1119, 0x111a, 0x111b, 0x111c, 0x111f, 0x1120 };
	OutCount = UE_ARRAY_COUNT(Activities);
	return Activities;
}

uint32 FElysiumNpc::DebugOverlayBits() const
{
	// SEAM for slot 513 (vtable `+0x804`). No per-NPC overlay word here; answers 0.
	return 0u;
}

FVector FElysiumNpc::DefaultEyeOffsetCm() const
{
	// `m_vDefaultEyeOffset` (`+0x5d60`). The shape map binds the word to `FElysiumEntity` with "no
	// stored view offset; the eye point is the chain's virtual `EyePosition()`", so the offset is
	// recovered from the eye point rather than stored twice.
	return EyePosition() - Origin;
}

FVector FElysiumNpc::BaseEyeOffset(int32 Activity) const
{
	// `CAI_BaseNPC::FUN_10274db0`, 92 bytes:
	//
	//     if ((vfunc0x804() & 0x8000000) != 0 && (act == 0x57 || act == 8))
	//         return Vector(0, 0, 1.5);
	//     return m_vDefaultEyeOffset;
	//
	// The `Activity` argument is retail's `param_2`; `param_3` (the second `Activity`) never reaches
	// an instruction in this body. Both immediates are literals, not `.rdata` cells: the X and Y
	// stores are `MOV 0` and the Z store is `MOV 0x41c00000`.
	if ((DebugOverlayBits() & GDebugOverlayEyeOffsetBit) != 0
		&& (Activity == GDebugEyeOffsetActivityA || Activity == GDebugEyeOffsetActivityB))
	{
		return FVector(0.f, 0.f, GDebugEyeOffsetZUnits * ElysiumMove::U);
	}
	return DefaultEyeOffsetCm();
}

FVector FElysiumNpc::EyeOffset(int32 Activity, int32 /*SecondActivity*/)
{
	// `CAI_BaseNPCTroika::FUN_102b4ab0`, 241 bytes. The hint arm only:
	//
	//     if (m_pHintNode != 0) switch (act) {
	//       case 0x1119: case 0x111a: case 0x111b: case 0x111c: case 0x111f: case 0x1120:
	//         pos = GetAbsOrigin();
	//         ApplyHintLeanOffset(&pos, false);              // thunk_FUN_102b6120
	//         HintStandPosition(m_pHintNode, this, &hint);   // thunk_FUN_102d1180
	//         return (hint - pos) + m_vDefaultEyeOffset;
	//     }
	//     return CAI_BaseNPC::FUN_10274db0(this, out, act, act2);
	//
	// Note what the hint arm does NOT consult: the debug-overlay bit, the second activity, and the
	// two special-cased activities of the base body. And note that `hint` is an UNINITIALISED stack
	// vector on the way into `0x102d1180` — retail reads whatever the hint query leaves.
	const int32 HintNode = ScheduleHost.HintNode;
	if (HintNode != INDEX_NONE)
	{
		int32 Count = 0;
		const int32* Activities = HintEyeOffsetActivities(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Activities[Index] != Activity)
			{
				continue;
			}
			// SOURCE units through both seams, as family TroikaHelpers takes them.
			FVector StandUnits = Origin / ElysiumMove::U;
			ApplyHintLeanOffset(StandUnits, /*bStanding*/ false);
			FVector HintUnits = FVector::ZeroVector;
			HintStandPosition(HintNode, HintUnits);
			return (HintUnits - StandUnits) * ElysiumMove::U + DefaultEyeOffsetCm();
		}
	}
	// `thunk_FUN_10274db0(this, out, param_2, param_3)` — the base body, which reads only `param_2`.
	return BaseEyeOffset(Activity);
}

// =================================================================================================
// `CAI_BaseNPCTroika::ResolveStandingOnHead`, `0x102bf820`
// =================================================================================================

FVector FElysiumNpc::StandingOnHeadDiagonal(int32 DiagonalRoll)
{
	// `102bf966`..`102bf9bf`. The roll is `RandomInt(0, 3)` and the listing dispatches it with three
	// `DEC EAX; JZ`, so the fall-through (0, and anything outside 1..3) is the LAST arm written.
	// Each arm loads the Y component onto the FP stack from `.rdata` and stores the X component as
	// an immediate:
	//
	//     roll 1 -> Y = _DAT_1049aea8 (+0.707), X = 0xbf34fdf4 (-0.707)
	//     roll 2 -> Y = _DAT_1049aea4 (-0.707), X = 0x3f34fdf4 (+0.707)
	//     roll 3 -> Y = _DAT_1049aea4 (-0.707), X = 0xbf34fdf4 (-0.707)
	//     else   -> Y = _DAT_1049aea8 (+0.707), X = 0x3f34fdf4 (+0.707)
	//
	// The four diagonals of the XY plane. Z stays at the zero the delta was forced to.
	switch (DiagonalRoll)
	{
	case 1:  return FVector(GDiagonalMinus, GDiagonalPlus, 0.f);
	case 2:  return FVector(GDiagonalPlus, GDiagonalMinus, 0.f);
	case 3:  return FVector(GDiagonalMinus, GDiagonalMinus, 0.f);
	default: return FVector(GDiagonalPlus, GDiagonalPlus, 0.f);
	}
}

FElysiumNpc::FStandingOnHeadStep FElysiumNpc::StandingOnHeadStep(const FVector& MyOriginCm,
	const FVector& GroundOriginCm, float PreviousTimerSeconds, float IntervalSeconds,
	int32 DiagonalRoll, float JitterX, float JitterY)
{
	FStandingOnHeadStep Out;

	// `102bf8f1`..`102bf94e`: the delta from the ground entity's origin to mine, with **Z forced to
	// zero after it is computed** (`MOV dword ptr [ESP + 0x14],0x0` overwrites the Z difference the
	// two instructions before it just stored). The push is always horizontal.
	FVector Direction = MyOriginCm - GroundOriginCm;
	Direction.Z = 0.0;

	if (Direction.X == GGeometrySharedZero && Direction.Y == GGeometrySharedZero)
	{
		// Exactly co-located in XY — `FCOMP` against `_DAT_104454c4`, an exact float compare, on
		// both axes. Retail picks one of four diagonals rather than dividing by zero.
		Direction = StandingOnHeadDiagonal(DiagonalRoll);
	}
	else
	{
		// `102bf9c1`..`102bfa13`: normalise, add an independent `RandomFloat(-0.1, 0.1)` to X and to
		// Y, then normalise AGAIN. The jitter is applied to a UNIT vector, so it is a fixed angular
		// spread of about +-8 degrees and not a distance-dependent one.
		Direction.Normalize();
		Direction.X += JitterX;
		Direction.Y += JitterY;
		Direction.Normalize();
		Direction.Z = 0.0;
	}
	Out.Direction = Direction;

	// `102bfa17`..`102bfa3b`: `m_flStandingOnHeadTimer = min(timer + interval, 5.0)`. A RAMP, so a
	// body that has been stood on for a while is pushed harder than one that just was.
	Out.TimerSeconds = FMath::Min(PreviousTimerSeconds + IntervalSeconds,
		GStandingOnHeadTimerCeiling);

	// `102bfa41`..`102bfa52`: the trace starts at my origin lifted `_DAT_1049a1f4` (0.1 units) on Z,
	// and `102bfc78` subtracts that lift back off the destination before the move.
	Out.StartCm = MyOriginCm + FVector(0.f, 0.f, GStandingOnHeadLiftUnits * ElysiumMove::U);

	// `102bfa56`..`102bfaaf`: `dir * timer * 40 * interval`, the multiplications in that order.
	Out.DeltaCm = Out.Direction * Out.TimerSeconds * (GStandingOnHeadSpeedUnits * ElysiumMove::U)
		* IntervalSeconds;
	return Out;
}

void FElysiumNpc::ResolveStandingOnHead(float IntervalSeconds)
{
	// `0x102bf820`, 1,179 bytes. `IntervalSeconds` is retail's single stack argument — a float the
	// decompiler lost to `fStack_4`, read twice off the frame (`[ESP+0xac]` for the timer and
	// `[ESP+0xb0]` after a `PUSH EBP` for the distance).
	//
	// The first two arms are refusals and both land on the SAME write: `m_flStandingOnHeadTimer = 0`
	// (`LAB_102bfc9e`). The ramp only survives while an NPC is continuously standing on something.
	FElysiumEntity* Ground = GetGroundEntity();
	if (Ground == nullptr)
	{
		// `m_hGroundEntity` (`+0x384`) did not resolve. Slot 209 `GetGroundEntity` is another
		// story's row and is still a generated stub, so this is the arm this runtime always takes
		// today — stated rather than worked around.
		StandingOnHeadTimer = 0.f;
		return;
	}

	FVector GroundMinsUnits = FVector::ZeroVector;
	FVector GroundMaxsUnits = FVector::ZeroVector;
	if (!RetailCollisionExtents(*Ground, GroundMinsUnits, GroundMaxsUnits))
	{
		// Retail's second refusal is `ground->m_Collision == 0` (`+0x9c` on the resolved entity),
		// and the body reads the ground entity's origin THROUGH that collision object. Family
		// Motor's extents seam is the same absent collision property; a false answer is the refusal.
		StandingOnHeadTimer = 0.f;
		return;
	}

	// `102bf8e6`: the ground position retail differences against is read off the collision object,
	// not off the entity, so it is the collideable's own origin. This runtime has one origin.
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const int32 DiagonalRoll = Stream.RandRange(0, 3);
	const float JitterX = Stream.FRandRange(-GStandingOnHeadJitter, GStandingOnHeadJitter);
	const float JitterY = Stream.FRandRange(-GStandingOnHeadJitter, GStandingOnHeadJitter);

	const FStandingOnHeadStep Step = StandingOnHeadStep(Origin, Ground->Origin,
		StandingOnHeadTimer, IntervalSeconds, DiagonalRoll, JitterX, JitterY);
	StandingOnHeadTimer = Step.TimerSeconds;

	// `102bfaf3`..`102bfb74`: the hull trace, with this NPC's OWN collision mins and maxs, mask
	// `0x202400b`, and a filter built from `m_pMoveProbe`'s entity (`+0x5d40`) and its collision
	// group. Under a `CAI_MoveProbe_TraceHull` VProf scope, which is how the profiler names it.
	FVector MyMinsUnits = FVector::ZeroVector;
	FVector MyMaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MyMinsUnits, MyMaxsUnits);

	FVector EndCm = Step.StartCm + Step.DeltaCm;
	FKernelHullTrace Trace;
	KernelHullTrace(Step.StartCm, EndCm, MyMinsUnits, MyMaxsUnits, GStandingOnHeadTraceMask, Trace);

	// `102bfb7a`: a blocked or solid first trace and retail tries the OPPOSITE direction — the same
	// start, the delta SUBTRACTED. It does not renormalise and it does not redraw; it simply pushes
	// the other way.
	if (Trace.Fraction < GGeometryTraceClearFraction)
	{
		EndCm = Step.StartCm - Step.DeltaCm;
		KernelHullTrace(Step.StartCm, EndCm, MyMinsUnits, MyMaxsUnits, GStandingOnHeadTraceMask,
			Trace);
	}

	// `102bfc4d`: the second gate reads the SAME trace result slot, so an unblocked first trace
	// passes here without a second one having run.
	if (Trace.Fraction < GGeometryTraceClearFraction)
	{
		StandingOnHeadTimer = 0.f;
		return;
	}

	// `102bfc78`..`102bfc97`: subtract the Z lift back off, `SetAbsOrigin` (slot 216) and
	// `CBaseEntity::Relink`. The timer is NOT cleared on this arm — that is what lets the ramp
	// build while the push is succeeding.
	EndCm.Z -= GStandingOnHeadLiftUnits * ElysiumMove::U;
	SetRuntimeOrigin(EndCm);
}

// =================================================================================================
// `CNPC_VAsianVampire::StandingOnPlayer`, `0x10362730`
// =================================================================================================

bool FElysiumNpc::StandingOnPlayerOverlap(const FVector& MyOriginCm, const FVector& OtherOriginCm,
	const FVector& MyMinsCm, const FVector& MyMaxsCm, const FVector& OtherMinsCm,
	const FVector& OtherMaxsCm)
{
	// `10362730`'s tail. Retail builds three 2-D lengths with `sqrtf` and compares one against the
	// sum of the other two, each scaled by `_DAT_104454d0` (0.5):
	//
	//     sqrtf((o.x-m.x)^2 + (o.y-m.y)^2)
	//       <   sqrt((oMax.x-oMin.x)^2 + (oMax.y-oMin.y)^2) * 0.5
	//         + sqrt((mMax.x-mMin.x)^2 + (mMax.y-mMin.y)^2) * 0.5
	//
	// The two right-hand terms are HALF-DIAGONALS of the XY footprints, not radii: for a 32x32 hull
	// that is 22.6 units, not 16. So the test admits a diagonal overlap a circle of the box's
	// half-width would refuse, and the comparison is STRICTLY less — exactly touching is not
	// standing on.
	const double Separation = FVector2D(OtherOriginCm.X - MyOriginCm.X,
		OtherOriginCm.Y - MyOriginCm.Y).Size();
	const double OtherHalfDiagonal = FVector2D(OtherMaxsCm.X - OtherMinsCm.X,
		OtherMaxsCm.Y - OtherMinsCm.Y).Size() * GRetailHalf;
	const double MyHalfDiagonal = FVector2D(MyMaxsCm.X - MyMinsCm.X,
		MyMaxsCm.Y - MyMinsCm.Y).Size() * GRetailHalf;
	return Separation < OtherHalfDiagonal + MyHalfDiagonal;
}

bool FElysiumNpc::StandingOnPlayer() const
{
	// `0x10362730`, 383 bytes, retail-named. The scope-trace pair is the outer 100 of them.
	//
	// The subject is `m_hClosestPlayer` (`+0x628c`), the sense pass's cache, NOT `GetEnemy()` — an
	// asian vampire standing on a player it is not fighting still answers true.
	const FElysiumNpcMemory& Mem = Senses.Memory;
	FElysiumEntity* Player = (Mem.ClosestPlayer.IsSet() && World)
		? World->Resolve(Mem.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		// `thunk_FUN_100290c0` answered null — retail falls straight to the `return false` tail.
		return false;
	}

	FVector PlayerMinsUnits = FVector::ZeroVector;
	FVector PlayerMaxsUnits = FVector::ZeroVector;
	FVector MyMinsUnits = FVector::ZeroVector;
	FVector MyMaxsUnits = FVector::ZeroVector;
	// `piVar14[0x9c]` is the player's `m_Collision` and `this->m_Collision` is `+0x270`; slots 4 and
	// 8 on each are the OBB mins and maxs. Family Motor's extents seam is the same absent collision
	// property and answers both as zero, which collapses both half-diagonals to zero and makes the
	// test "are the two origins at exactly the same XY point" — the conservative refusal, stated.
	RetailCollisionExtents(*Player, PlayerMinsUnits, PlayerMaxsUnits);
	RetailCollisionExtents(*this, MyMinsUnits, MyMaxsUnits);

	return StandingOnPlayerOverlap(Origin, Player->Origin, MyMinsUnits * ElysiumMove::U,
		MyMaxsUnits * ElysiumMove::U, PlayerMinsUnits * ElysiumMove::U,
		PlayerMaxsUnits * ElysiumMove::U);
}

// =================================================================================================
// `CNPC_VWerewolf::UpdateFakeHull`, `0x103d93b0`
// =================================================================================================

bool FElysiumNpc::BoxesOverlap(const FVector& AMin, const FVector& AMax, const FVector& BMin,
	const FVector& BMax)
{
	// `FUN_10240250`, verbatim and in retail's order: X max, X min, Y max, Y min, Z max, Z min.
	// Every comparison is inclusive (`>=` / `<=`), so two boxes that share a face overlap.
	return BMax.X >= AMin.X && BMin.X <= AMax.X
		&& BMax.Y >= AMin.Y && BMin.Y <= AMax.Y
		&& BMax.Z >= AMin.Z && BMin.Z <= AMax.Z;
}

int32 FElysiumNpc::FakeHullKnockbackActivity(int32 DirectionClass)
{
	// `103d971e`..`103d973c`: `EBP = 0x79` before the test, `DEC EAX; JZ -> 0x7a`,
	// `SUB EAX,2; JNZ -> keep 0x79`, else `0x7b`. So class 1 answers `0x7a`, class 3 answers `0x7b`
	// and everything else — including 2 — answers `0x79`.
	switch (DirectionClass)
	{
	case 1:  return GFakeHullActivityClassOne;
	case 3:  return GFakeHullActivityClassThree;
	default: return GFakeHullActivityDefault;
	}
}

int32 FElysiumNpc::FakeHullDebugCvar() const
{
	// `DAT_1093f73c` `+0x2c`: `werewolf_show_debug`, shipped "0", which closes the debug draw.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfShowDebug);
}

FElysiumEntity* FElysiumNpc::FakeHullPushTarget() const
{
	// SEAM for `GetEnemy()->+0xa8`. **Unrecovered** which field that is. Slot 167 (`+0x29c`) is the
	// CONST `GetEnemy`, which is the one this body dispatches.
	return GetEnemy();
}

FVector FElysiumNpc::NearestPointOnEntity(const FElysiumEntity* /*Entity*/,
	const FVector& PointCm) const
{
	// SEAM for `CollisionProperty::CalcNearestPoint` (`0x100dd000`): rotate the point into the
	// collideable's space, clamp it into `[m_vecMins, m_vecMaxs]` (`0x1013c8c0`), rotate back.
	++FakeHullSeams.NearestPointCalls;
	return PointCm;
}

int32 FElysiumNpc::PushedEntityDirectionClass(FElysiumEntity* Pushed, const FVector& DeltaCm) const
{
	// Slot 323 (`0x10344dd0`), dispatched on the PUSHED entity, not on this one.
	++FakeHullSeams.DirectionClassCalls;
	if (FElysiumNpc* PushedNpc = Pushed ? Pushed->AsNpc() : nullptr)
	{
		return PushedNpc->Slot323(DeltaCm);
	}
	return 0;
}

bool FElysiumNpc::PushedEntityKnockback(FElysiumEntity* Pushed, int32 Activity)
{
	// Slot 320 `PlayerKnockbackReaction(CBaseCombatCharacter*, Activity)`, also on the pushed
	// entity, with THIS npc as the attacker argument.
	++FakeHullSeams.KnockbackCalls;
	FakeHullSeams.LastKnockbackActivity = Activity;
	if (FElysiumNpc* PushedNpc = Pushed ? Pushed->AsNpc() : nullptr)
	{
		return PushedNpc->PlayerKnockbackReaction(this, Activity);
	}
	return false;
}

void FElysiumNpc::PushFakeHullDamage(FElysiumEntity* /*Pushed*/, float /*Damage*/,
	const FVector& ForceUnits, const FVector& /*PositionUnits*/)
{
	// SEAM for `CBaseEntity::TakeDamage`. The FORCE and the POSITION are the recovered halves and
	// the ledger carries them; this runtime's damage path (`ElysiumDamage::Apply`) needs a
	// `FElysiumDmg` descriptor and a dice context a geometry body has no source for.
	++FakeHullSeams.DamagePushes;
	FakeHullSeams.LastDamageForceUnits = ForceUnits;
}

void FElysiumNpc::UpdateFakeHull(double Now)
{
	// `0x103d93b0`, 1,202 bytes, retail-named. The scope-trace pair is the outer 110.
	//
	// The word this whole body maintains is `+0x66dc` — the world position of the `Bip01` bone as of
	// the previous call — and the ONE cell it compares against is `DAT_1070d1b0`, which is
	// `vec3_origin`. So the three reads of that cell mean three different things and the body only
	// makes sense once that is settled:
	//
	//   * `103d949c` uses it as the INPUT vector of a `VectorTransform`, which makes the answer the
	//     translation column of the `Bip01` bone's local matrix — i.e. the bone's own point.
	//   * `103d9431` writes it into the cache, which RESETS the cache to zero.
	//   * `103d94c0` compares it against the cache, which asks "is the cache still zero?".
	const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();
	if (Enemy == nullptr)
	{
		// No enemy: reset the cache and stop. The werewolf's fake hull only exists while it has
		// something to shoulder out of the way.
		WerewolfFakeHullPosUnits = FVector::ZeroVector;
		return;
	}

	// `103d9459`..`103d94a6`: `LookupBone("Bip01")`, `GetModelPtr(-1)`, `GetBoneTransform(bone, m)`,
	// then `VectorTransform(vec3_origin, studiohdr->bone[bone] + 0x58, local)` followed by
	// `VectorTransform(local, m, world)`. Two transforms, and the first one's input is the zero
	// vector, so the whole chain is "where is the `Bip01` bone in the world".
	FVector BonePosCm = FVector::ZeroVector;
	const bool bHaveBone = BoneWorldPosition(GFakeHullBoneName, BonePosCm);
	const FVector BonePosUnits = BonePosCm / ElysiumMove::U;

	// `103d94c0`..`103d94fa`: three exact float compares of `vec3_origin` against the cache, and the
	// overlap test runs ONLY when at least one component differs — i.e. only when the cache is
	// non-zero, which is only on the second and later calls after an enemy appeared. The first call
	// after every reset does nothing but fill the cache.
	const bool bCacheArmed = WerewolfFakeHullPosUnits != FVector::ZeroVector;
	if (bCacheArmed)
	{
		// `103d9500`: the debug hull draw, behind `!cvar->IsCommand() && cvar->m_nValue != 0`.
		if (FakeHullDebugCvar() != 0)
		{
			++FakeHullSeams.DebugHullDraws;
		}

		// `103d953d`..`103d95fe`: the hull table's mins and maxs for `m_eHull` (`+0x1568`), each
		// scaled by `_DAT_10462914` (1.25) and added to the BONE point — not to the origin. A fake
		// hull, a quarter larger than the real one, hung off the pelvis.
		FVector HullMinsUnits = FVector::ZeroVector;
		FVector HullMaxsUnits = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMinsUnits, HullMaxsUnits);
		const FVector FakeMinUnits = HullMinsUnits * GFakeHullExtentScale + BonePosUnits;
		const FVector FakeMaxUnits = HullMaxsUnits * GFakeHullExtentScale + BonePosUnits;

		// `103d9604`..`103d963d`: the enemy's `m_Collision` slot `+0x3c` (its world-space
		// surrounding bounds) against that box, through `FUN_10240250`.
		FVector EnemyMinsUnits = FVector::ZeroVector;
		FVector EnemyMaxsUnits = FVector::ZeroVector;
		RetailCollisionExtents(*Enemy, EnemyMinsUnits, EnemyMaxsUnits);

		if (BoxesOverlap(FakeMinUnits, FakeMaxUnits, EnemyMinsUnits, EnemyMaxsUnits))
		{
			ApplyFakeHullPush(BonePosUnits, Now);
		}
	}

	// `103d982f`: the cache is written on EVERY path that got an enemy, overlap or not. A failed
	// bone lookup leaves the point at zero, which resets the cache and disarms the next call —
	// retail's own behaviour with no `Bip01`.
	WerewolfFakeHullPosUnits = bHaveBone ? BonePosUnits : FVector::ZeroVector;
}

void FElysiumNpc::ApplyFakeHullPush(const FVector& BonePosUnits, double Now)
{
	// `103d9643`..`103d9829`, the overlap arm of `0x103d93b0`.
	//
	// The delta is how far the BONE moved since the previous call, not how far the NPC moved: an
	// animation that swings the pelvis pushes, and a werewolf standing still inside its enemy does
	// not.
	const FVector DeltaUnits = BonePosUnits - WerewolfFakeHullPosUnits;

	// `103d968e`: the body asks slot 167 for the enemy a SECOND time and then reads a pointer out of
	// it at `+0xa8`. That pointer, not the enemy, is what is offset, classified, knocked back and
	// damaged.
	FElysiumEntity* Pushed = FakeHullPushTarget();
	if (Pushed == nullptr)
	{
		return;
	}

	// `103d96a2`: `CalcNearestPoint(bonePos)` on the pushed entity's collision property. The out
	// vector is seeded with `vec3_origin` first, which is dead — the callee writes all three words.
	const FVector NearestCm = NearestPointOnEntity(Pushed, BonePosUnits * ElysiumMove::U);

	// `103d96b7`..`103d9712`: the direction is from MY `WorldSpaceCenter()` (slot 192) to the pushed
	// entity's `GetAbsOrigin()` (slot 217), and it is handed to slot 323 **before** it is
	// normalised — the `VectorNormalize` at `103d9723` runs after the call and its result is
	// discarded (`FSTP ST0`), so it is dead code that the classifier never sees.
	const FVector DirectionCm = Pushed->Origin - SpeciesWorldSpaceCenter();
	const int32 DirectionClass = PushedEntityDirectionClass(Pushed, DirectionCm);
	const int32 Activity = FakeHullKnockbackActivity(DirectionClass);
	PushedEntityKnockback(Pushed, Activity);

	// `103d974d`: the damage is rate-limited to once a second by `+0x66f4` against
	// `gpGlobals->curtime`, and the gate is strict — `stamp + 1.0 < curtime`. The knockback above is
	// NOT rate-limited; only the damage is.
	if (!(WerewolfFakeHullPushTime + GFakeHullPushIntervalSeconds < Now))
	{
		return;
	}

	// `103d976c`..`103d981c`: `CTakeDamageInfo(this, this, 20.0, 1, 0, 0, -1)`, then the sub-type
	// writes `2` and `1`, the force is the bone delta scaled by `_DAT_10457f5c` (500), the position
	// is the nearest point, the scale is 1.0, and `TakeDamage` is called on the pushed entity.
	// **The attacker and the inflictor are both `this`**, so a werewolf shouldering an object
	// credits itself with the damage.
	PushFakeHullDamage(Pushed, GFakeHullDamage, DeltaUnits * GFakeHullForceScale,
		NearestCm / ElysiumMove::U);
	WerewolfFakeHullPushTime = Now;
}

// =================================================================================================
// `CNPC_VMingXiao`'s two severed-tentacle scatter notices
// =================================================================================================

void FElysiumNpc::NotifyScatterCenter(FElysiumEntity* Tentacle, const FVector& PositionCm)
{
	// `FUN_1039ef90`, 40 bytes:
	//
	//     (**(code **)(*DAT_10924a6c + 4))();      // a global object, unrecovered
	//     SetCondition(this, 0x78);                // 0x10269a20
	//     m_vecScatterCenter = *param_1;           // +0x668c on CNPC_VMingXiaoTentacle
	//
	// The condition is set on the NOTIFIED tentacle, not on the notifier, and so is the centre.
	++ScatterNoticeEvents;
	FElysiumNpc* TentacleNpc = Tentacle ? Tentacle->AsNpc() : nullptr;
	if (TentacleNpc == nullptr)
	{
		return;
	}
	TentacleNpc->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(ScatterNoticeCondition));
	TentacleNpc->TentacleScatterCenterUnits = PositionCm / ElysiumMove::U;
}

void FElysiumNpc::NotifyOwnedCopiesOfOwnerMove(FElysiumEntity* Moved)
{
	// `FUN_10397e00`, 100 bytes. `this` is the OWNER `CNPC_VMingXiao` and `Moved` is the tentacle
	// that moved — `0x1039ef60` above it resolves the pair that way round.
	if (Moved == nullptr)
	{
		// `if (param_1 != 0)` is the whole of retail's first test.
		return;
	}

	// The position handed out is `Moved`'s own (`(**(code **)(*param_1 + 0x364))()`), read ONCE per
	// surviving tentacle inside the loop rather than hoisted — 29c's walk reads it as "this
	// entity's own position", and the listing's receiver is `param_1`.
	const FVector MovedOriginCm = Moved->Origin;

	// `m_rhSeveredTentacles[6]` (`+0x66a8`, family Squad's member), walked all six unconditionally.
	for (int32 Index = 0; Index < GSeveredTentacleCount; ++Index)
	{
		if (!SeveredTentacles[Index].IsSet() || World == nullptr)
		{
			continue;
		}
		FElysiumEntity* Other = World->Resolve(SeveredTentacles[Index]);
		if (Other == nullptr || Other == Moved)
		{
			// Retail's two guards: the handle resolved to something, and it is not `param_1`
			// itself. A tentacle is never told to scatter away from where it already is.
			continue;
		}
		NotifyScatterCenter(Other, MovedOriginCm);
	}
}

void FElysiumNpc::NotifyOwnerOfMyMove()
{
	// `FUN_1039ef60`, 22 bytes: resolve `m_hMingXiao` (`+0x665c`) and, when it is live, run the walk
	// above ON THE OWNER. This is how the body is entered; nothing calls `0x10397e00` directly.
	FElysiumEntity* Owner = (TentacleMingXiao.IsSet() && World)
		? World->Resolve(TentacleMingXiao) : nullptr;
	if (FElysiumNpc* OwnerNpc = Owner ? Owner->AsNpc() : nullptr)
	{
		OwnerNpc->NotifyOwnedCopiesOfOwnerMove(this);
	}
}

bool FElysiumNpc::ScatterTentacleGate(const FVector& DeltaCm, const FVector& Forward)
{
	// `10399919`..`10399988`. The delta is normalised IN PLACE and the length `VectorNormalize`
	// answers is the range test, so the dot that follows is against a UNIT direction.
	FVector Direction = DeltaCm;
	const double LengthCm = Direction.Size();
	Direction.Normalize();

	// `FCOMP [0x1046dcd0]` with `AND EAX,0x4100; JZ skip` — the mask keeps both the "below" and the
	// "equal" flags, so the range gate is inclusive at exactly 128 Source units.
	if (!(LengthCm <= GScatterRangeUnits * ElysiumMove::U))
	{
		return false;
	}

	// `1039996b`..`10399988`: `dir.y * m_vecForward[1] + dir.x * m_vecForward[0]`, a **2-D** dot —
	// Z is not multiplied by anything — compared against the DOUBLE `_DAT_10449260` = 0.25 with
	// `TEST AH,0x5; JNP skip`, which proceeds at or above. A 75.5-degree half-angle in front.
	const double Dot = Direction.X * Forward.X + Direction.Y * Forward.Y;
	return Dot >= GScatterForwardDotFloor;
}

void FElysiumNpc::FUN_103998d0(FElysiumEntity* Tentacle)
{
	// `0x103998d0`, 212 bytes — `CNPC_VMingXiao::CoordinateTroops`'s severed-tentacle half.
	// `CoordinateTroops` (`0x10399610`) runs it on ONE tentacle per call, walking
	// `m_iCoordinateTentacleID` (`+0x6740`) 0..5 and wrapping, so the whole set is coordinated over
	// six calls rather than every call.
	FElysiumNpc* TentacleNpc = Tentacle ? Tentacle->AsNpc() : nullptr;
	if (TentacleNpc == nullptr)
	{
		return;
	}

	// `103998db`..`103998fe`: three gates on the tentacle's own words, all three before anything is
	// measured. `m_iForcedSchedule` (`+0x65c8`) is `FElysiumNpcScheduleHost::ForcedSchedule` in this
	// runtime, which carries a registered schedule id; the two refused numbers are retail's and are
	// compared as numbers.
	const int32 ForcedSchedule = static_cast<int32>(TentacleNpc->ScheduleHost.ForcedSchedule);
	if (ForcedSchedule == GScatterRefusedScheduleA || ForcedSchedule == GScatterRefusedScheduleB)
	{
		return;
	}
	if (TentacleNpc->TentaclePhase != GScatterRequiredPhase)
	{
		return;
	}

	// `10399904`..`10399931`: the delta is the TENTACLE's origin minus mine.
	const FVector DeltaCm = TentacleNpc->Origin - Origin;

	// `m_vecForward` (`+0x6290`) is `FElysiumNpc::Forward`, retail's cached facing basis. Nothing in
	// this runtime writes it yet (the shape map says so: "this runtime recomputes it per query"), so
	// the cone gate takes its refusal arm until a sense pass fills it.
	if (!ScatterTentacleGate(DeltaCm, Forward))
	{
		return;
	}

	// `1039998a`: the centre handed over is MY origin — the tentacle is told to scatter away from
	// the boss, which is the opposite receiver from `0x10397e00`'s.
	NotifyScatterCenter(TentacleNpc, Origin);
}
