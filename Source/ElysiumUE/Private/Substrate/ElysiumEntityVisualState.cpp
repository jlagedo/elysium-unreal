// `CBaseEntity`'s presentation state and angle motion (`walks/L0-r021.md`, stories
// L0.entity_core.entity-visual-state and L0.entity_core.angle-motion): the model words (slots 8 / 10
// / 212) and `SetModel` (slot 105) over `UTIL_SetModel`, `IsViewable` (slot 163), `Hide` / `Unhide`
// (slots 66 / 67) with `ForceTransmit`, `SetMovedir` (slot 171) with `AngleVectors`, and
// `SimulateAngles`. `SetAngles` (slot 64) and `IsBSPModel` (`IsStandableSolid`) are in
// `ElysiumEntitySlotBodies.cpp`; `UTIL_SetModel` / `UTIL_SetSize` in `ElysiumEntityCollision.cpp`.

#include "ElysiumEntity.h"

#include "ElysiumEntityWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEntityVisual, Log, All);

namespace
{
	// `m_fEffects` bit `EF_NODRAW` in this build (`TEST AL,0x40` at `IsViewable` 0x100a9800, `OR AL,0x40`
	// at `Hide` 0x1009d2a0, `AND 0xffffffbf` at `Unhide` 0x1009d380).
	constexpr uint32 GVisualStateNoDraw = 0x40u;
	// `_DAT_104454c0` = 1.0f, `ForceTransmit` 0x1009d1e0's addend.
	constexpr float GVisualStateTransmitWindow = 1.0f;
	// `_DAT_104454c4` = 0.0f (x and z sentinels), the y sentinels' bit patterns (`SetMovedir`
	// 0x100ad550 compares the y word as a dword), and the two straight directions' z words.
	constexpr float GVisualStateZero = 0.0f;
	constexpr uint32 GVisualStateUpYawBits = 0xbf800000u;     // -1.0f
	constexpr uint32 GVisualStateDownYawBits = 0xc0000000u;   // -2.0f
	constexpr float GVisualStateUpZ = 1.0f;                   // 0x3f800000
	constexpr float GVisualStateDownZ = -1.0f;                // 0xbf800000
	// `_DAT_1044eb08` = 0x3c8efa35, `AngleVectors` 0x10139610's degrees-to-radians factor (pi/180 as f32).
	const float GVisualStateDegToRad = []() { const uint32 Bits = 0x3c8efa35u; float F; FMemory::Memcpy(&F, &Bits, 4); return F; }();
	// `m_Collision.m_nSurroundType` arms of `FUN_100b51b0`'s jump table at 0x100b5254 (read from the PE:
	// entries 0, 1, 6 -> 0x100b51cd the test; 2, 4 -> 0x100b51e0 the call; 3, 5 -> 0x100b51e5 skip).
	constexpr int32 GVisualStateSolidBbox = 2;
	constexpr int32 GVisualStateSolidNone = 0;

	uint32 GVisualStateBits(float F)
	{
		uint32 Bits;
		FMemory::Memcpy(&Bits, &F, 4);
		return Bits;
	}

	FString GVisualStateVec(const FVector& V)
	{
		return FString::Printf(TEXT("%.9g,%.9g,%.9g"), static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}

	// `AngleVectors` 0x10139610, the forward row only (`SetMovedir` passes right = up = NULL). Per axis
	// `FLD angle; FMUL [0x1044eb08]; FSTP float` -- the radian value rounded to f32 -- then `FSINCOS`,
	// sin and cos each rounded to f32 on its store (yaw first, then pitch, then roll, whose results the
	// forward never reads). Forward = (cp * cy, cp * sy, -sp): one f32 multiply each, z by `FCHS`.
	FVector GVisualStateAngleVectorsForward(const FVector& Angles)
	{
		const float Ax = static_cast<float>(Angles.X);
		const float Ay = static_cast<float>(Angles.Y);
		const float RadYaw = static_cast<float>(static_cast<double>(Ay) * static_cast<double>(GVisualStateDegToRad));
		const float Sy = static_cast<float>(FMath::Sin(static_cast<double>(RadYaw)));
		const float Cy = static_cast<float>(FMath::Cos(static_cast<double>(RadYaw)));
		const float RadPitch = static_cast<float>(static_cast<double>(Ax) * static_cast<double>(GVisualStateDegToRad));
		const float Sp = static_cast<float>(FMath::Sin(static_cast<double>(RadPitch)));
		const float Cp = static_cast<float>(FMath::Cos(static_cast<double>(RadPitch)));
		const float Fx = static_cast<float>(static_cast<double>(Cp) * static_cast<double>(Cy));
		const float Fy = static_cast<float>(static_cast<double>(Cp) * static_cast<double>(Sy));
		const float Fz = -Sp;
		return FVector(Fx, Fy, Fz);
	}
}

// --- The model words --------------------------------------------------------------------------

int32 FElysiumEntity::GetModelIndex() const
{
	// `CBaseEntity::GetModelIndex` 0x100b17f0, slot 8: `return m_nModelIndex` (+0x1a4).
	return ModelIndex;
}

void FElysiumEntity::SetModelIndex(int32 Index)
{
	// `CBaseEntity::SetModelIndex` 0x100b1750, slot 10: `m_nModelIndex` (+0x1a4) = arg.
	ModelIndex = Index;
}

void FElysiumEntity::SetModelName(FName Name)
{
	// `CBaseEntity::SetModelName` 0x100b15f0, slot 212 (`MOV [ECX+0x388],EAX`): `m_ModelName` = arg, the
	// `string_t` pointer itself. A name equal to the stored one leaves the word as authored (the same
	// string retail re-stores); a different one replaces it and the Unreal body follows the model
	// (visual only, `OnRuntimeModelChanged`).
	const FString NewName = Name.IsNone() ? FString() : Name.ToString();
	if (Model.Equals(NewName, ESearchCase::IgnoreCase))
	{
		return;
	}
	Model = NewName;
	OnRuntimeModelChanged();
}

void FElysiumEntity::SetModel(TCHAR* ModelName)
{
	// `CBaseEntity::SetModel` 0x100ad460, slot 105 (`walks/L0-r021.md`), arms in retail order. Its scope-
	// trace frame writes no game state.
	const FString Name = ModelName != nullptr ? FString(ModelName) : FString();
	// 1. `t = VEngineServer014 slot 20 (+0x50)(name)` (`100ad4d8`): the model-table index, -1 when absent.
	const int32 T = World != nullptr ? World->ModelTableIndex(Name) : -1;
	// 2. `u = slot 21 (+0x54)(t)` (`100ad4e4`): the model type, 0 for t < 0, t == 0 or a failed load.
	const int32 U = (World != nullptr && World->ModelTableHasModel(T)) ? World->ModelTableType(T) : 0;
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("set_model"), TEXT("CBaseEntity::SetModel"), 0x100ad460u, TEXT("entry"),
			FString::Printf(TEXT("name=\"%s\" index=%d type=%d"), *Name, T, U));
	}
	//    `u != 1` (`100ad4e7`) -> `Msg("Setting CBaseEntity to non-brush model %s\n", name)` (0x105573c8, TIER0 Msg); no
	//    early return. An empty name prints the line too (type 0).
	if (U != FElysiumEntityWorld::ModelTypeBrush)
	{
		UE_LOG(LogElysiumEntityVisual, Log, TEXT("Setting CBaseEntity to non-brush model %s"), *Name);
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("set_model"), TEXT("CBaseEntity::SetModel"), 0x100ad4f2u, TEXT("branch"),
				FString::Printf(TEXT("arm=non_brush msg=\"Setting CBaseEntity to non-brush model %s\""), *Name));
		}
	}
	// 3. `UTIL_SetModel(this, name)` (`100ad4fd`, thunk 0x10006e83 -> 0x101cf4a0); `t` is not forwarded.
	UtilSetModel(Name);
	// 4. `+0x1b1 = 1` (`m_NetworkChangeState.m_bChanged`), always.
	bNetworkChanged = true;
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("set_model"), TEXT("CBaseEntity::SetModel"), 0x100ad502u, TEXT("write"),
			FString::Printf(TEXT("m_bChanged=1 m_nModelIndex=%d m_ModelName=\"%s\""), ModelIndex, *Model));
	}
}

// --- Visibility -------------------------------------------------------------------------------

bool FElysiumEntity::IsViewable()
{
	// `CBaseEntity::IsViewable` 0x100a9800, slot 163, arms in retail order.
	bool bResult = false;
	bool bBsp = false;
	// 1. `m_fEffects & 0x40` (byte test on +0x19c) -> false.
	if ((EffectsWord & GVisualStateNoDraw) == 0)
	{
		// 2. `IsBSPModel` 0x100b5110 (thunk 0x100117d9).
		bBsp = IsStandableSolid();
		// 3. not BSP: `GetModelIndex() != 0` (slot 8, `[vtbl+0x20]`). 4. BSP: `GetMoveType() != 0` (slot 94,
		//    `[vtbl+0x178]`) -- a SOLID_BSP entity with MOVETYPE_NONE is not viewable.
		bResult = bBsp ? GetMoveType() != 0 : GetModelIndex() != 0;
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("is_viewable"), TEXT("CBaseEntity::IsViewable"), 0x100a9800u, TEXT("return"),
			FString::Printf(TEXT("m_fEffects=0x%x m_Solid=%d bsp=%d m_MoveType=%d m_nModelIndex=%d result=%d"),
				EffectsWord, RetailSolidType, bBsp ? 1 : 0, GetMoveType(), ModelIndex, bResult ? 1 : 0));
	}
	return bResult;
}

void FElysiumEntity::ForceTransmit()
{
	// `CBaseEntity::ForceTransmit` 0x1009d1e0: `FLD [gpGlobals+0xc]; FADD [0x104454c0]; FSTP [ECX+0x90]`.
	const float CurTime = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.0f;
	ForceTransmitUntil = static_cast<float>(static_cast<double>(CurTime) + static_cast<double>(GVisualStateTransmitWindow));
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("force_transmit"), TEXT("CBaseEntity::ForceTransmit"), 0x1009d1e0u, TEXT("write"),
			FString::Printf(TEXT("curtime=%.6f m_flForceTransmitUntil=%.6f"), CurTime, ForceTransmitUntil));
	}
}

void FElysiumEntity::Hide()
{
	// `CBaseEntity::Hide` 0x1009d2a0, slot 66. `m_bScriptHidden` (+0xf4) is this port's `bHidden` latch.
	// 1. Script-hidden: `m_fScriptSavedEffects |= 0x40` (+0xf8) -- the word `ScriptUnhide` 0x100a8990
	//    restores. 2. Else `m_fEffects |= 0x40` (+0x19c).
	const bool bScript = bHidden;
	if (bScript)
	{
		ScriptSavedEffects = static_cast<int32>(static_cast<uint32>(ScriptSavedEffects) | GVisualStateNoDraw);
	}
	else
	{
		EffectsWord |= GVisualStateNoDraw;
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("hide"), TEXT("CBaseEntity::Hide"), 0x1009d2a0u, TEXT("write"),
			bScript ? FString::Printf(TEXT("path=script m_fScriptSavedEffects=0x%x"), static_cast<uint32>(ScriptSavedEffects))
				: FString::Printf(TEXT("path=live m_fEffects=0x%x"), EffectsWord));
	}
	// 3. Both arms: `ForceTransmit` (`CALL 0x1000e859` -> 0x1009d1e0).
	ForceTransmit();
}

void FElysiumEntity::Unhide()
{
	// `CBaseEntity::Unhide` 0x1009d380, slot 67: the bit cleared on the same path `Hide` set it, and no
	// `ForceTransmit` (the asymmetry is retail's).
	const bool bScript = bHidden;
	if (bScript)
	{
		ScriptSavedEffects = static_cast<int32>(static_cast<uint32>(ScriptSavedEffects) & ~GVisualStateNoDraw);
	}
	else
	{
		EffectsWord &= ~GVisualStateNoDraw;
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("unhide"), TEXT("CBaseEntity::Unhide"), 0x1009d380u, TEXT("write"),
			bScript ? FString::Printf(TEXT("path=script m_fScriptSavedEffects=0x%x"), static_cast<uint32>(ScriptSavedEffects))
				: FString::Printf(TEXT("path=live m_fEffects=0x%x"), EffectsWord));
	}
}

// --- Angle motion -----------------------------------------------------------------------------

void FElysiumEntity::SetMovedir()
{
	// `CBaseEntity::SetMovedir` 0x100ad550, slot 171, arms in retail order. The x and z tests are
	// `FLD [0x104454c4]; FCOMP [angle]` against 0.0f (so -0.0 passes and a NaN does not); the y test is a
	// dword compare against the bit pattern.
	const TCHAR* Arm = TEXT("angle_vectors");
	FVector A = GetAngles();                                                           // call 1, slot 221
	const FVector Seen = A;
	if (static_cast<float>(A.X) == GVisualStateZero && GVisualStateBits(static_cast<float>(A.Y)) == GVisualStateUpYawBits
		&& static_cast<float>(A.Z) == GVisualStateZero)
	{
		// 1. Up: `m_vecMoveDir = (0, 0, 1.0)`.
		Arm = TEXT("up");
		MoveDir = FVector(0.0, 0.0, GVisualStateUpZ);
	}
	else
	{
		A = GetAngles();                                                               // call 2
		if (static_cast<float>(A.X) == GVisualStateZero && GVisualStateBits(static_cast<float>(A.Y)) == GVisualStateDownYawBits
			&& static_cast<float>(A.Z) == GVisualStateZero)
		{
			// 2. Down: `m_vecMoveDir = (0, 0, -1.0)`.
			Arm = TEXT("down");
			MoveDir = FVector(0.0, 0.0, GVisualStateDownZ);
		}
		else
		{
			// 3. `AngleVectors(GetAngles() [call 3], &m_vecMoveDir, NULL, NULL)` (0x10139610).
			MoveDir = GVisualStateAngleVectorsForward(GetAngles());
		}
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("set_movedir"), TEXT("CBaseEntity::SetMovedir"), 0x100ad550u, TEXT("branch"),
			FString::Printf(TEXT("arm=%s angles=%s m_vecMoveDir=%s bits=0x%08x,0x%08x,0x%08x"), Arm, *GVisualStateVec(Seen),
				*GVisualStateVec(MoveDir), GVisualStateBits(static_cast<float>(MoveDir.X)),
				GVisualStateBits(static_cast<float>(MoveDir.Y)), GVisualStateBits(static_cast<float>(MoveDir.Z))));
	}
	// 4. Every path: slot 64 `SetAngles(&vec3_angle)` (0x1070d9d0, three zero dwords from
	//    `staticinit_10137100`), through the dispatch; its own gate writes only when an angle differs.
	SetAngles(FRotator(0.0, 0.0, 0.0));                                                // 100ad67a
}

void FElysiumEntity::SimulateAngles(float Interval)
{
	// `CBaseEntity::SimulateAngles` 0x1003f810. `A = GetAngles()` (slot 221, `[vtbl+0x374]`); per component
	// `FLD interval; FMUL vel; FADD [A]; FSTP float` -- the product of two f32 is exact in the x87
	// register, the sum rounds there (the CRT's 53-bit precision control, `__setdefaultprecision`
	// 0x10433913), and the store rounds to f32. Then slot 64 `SetAngles(T)` (`[vtbl+0x100]`).
	const FVector A = GetAngles();
	const FVector Vel = AngularVelocity;                                              // m_vecAngVelocity +0x3c8
	const double I = static_cast<double>(Interval);
	const float Tx = static_cast<float>(I * static_cast<double>(static_cast<float>(Vel.X)) + static_cast<double>(static_cast<float>(A.X)));
	const float Ty = static_cast<float>(I * static_cast<double>(static_cast<float>(Vel.Y)) + static_cast<double>(static_cast<float>(A.Y)));
	const float Tz = static_cast<float>(I * static_cast<double>(static_cast<float>(Vel.Z)) + static_cast<double>(static_cast<float>(A.Z)));
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("simulate_angles"), TEXT("CBaseEntity::SimulateAngles"), 0x1003f810u, TEXT("integrate"),
			FString::Printf(TEXT("interval=%.9g m_vecAngVelocity=%s before=%s after=%s"), Interval, *GVisualStateVec(Vel),
				*GVisualStateVec(A), *GVisualStateVec(FVector(Tx, Ty, Tz))));
	}
	SetAngles(FRotator(Tx, Ty, Tz));                                                   // 1003f8c2 slot 64
}

void FElysiumEntity::MarkSurroundingBoundsTreeDirty()
{
	// `FUN_100b51b0` 0x100b51b0 (`walks/L0-r021.md`): `switch (m_Collision.m_nSurroundType)` (+0x28c,
	// `CMP EAX,6; JA` -> call) through the jump table at 0x100b5254.
	const int32 Surround = static_cast<int32>(SurroundType);
	bool bCall = false;
	const TCHAR* Arm = TEXT("call");
	if (Surround == 0 || Surround == 1 || Surround == 6)
	{
		// 0x100b51cd: skip when `(signed char)m_usSolidFlags < 0` (bit 0x80, `JS`), or `m_Solid == 2`
		// (SOLID_BBOX) or `m_Solid == 0` (SOLID_NONE); else call.
		Arm = TEXT("test");
		bCall = (RetailSolidFlags & 0x80u) == 0 && RetailSolidType != GVisualStateSolidBbox && RetailSolidType != GVisualStateSolidNone;
	}
	else if (Surround == 3 || Surround == 5)
	{
		Arm = TEXT("skip");                                                            // 0x100b51e5
	}
	else
	{
		bCall = true;                                                                  // 2, 4, > 6: 0x100b51e0
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("surround_dirty"), TEXT("FUN_100b51b0"), 0x100b51b0u, TEXT("branch"),
			FString::Printf(TEXT("m_nSurroundType=%d arm=%s m_Solid=%d m_usSolidFlags=0x%x called=%d"), Surround, Arm,
				RetailSolidType, RetailSolidFlags & 0xffffu, bCall ? 1 : 0));
	}
	if (bCall)
	{
		MarkCollisionBoundsDirty();                                                    // CALL 0x100121f7 -> 0x100dda20
	}
	if (World == nullptr)
	{
		return;
	}
	// 0x100b51e5..: the first move child (+0x260), then each peer (+0x264), the same body (thunk 0x10004ade).
	FElysiumEntity* Child = World->Resolve(MoveChild);
	int32 Guard = 0;
	while (Child != nullptr && Guard++ < 8192)
	{
		if (Child != this)
		{
			Child->MarkSurroundingBoundsTreeDirty();
		}
		Child = World->Resolve(Child->MovePeer);
	}
}
