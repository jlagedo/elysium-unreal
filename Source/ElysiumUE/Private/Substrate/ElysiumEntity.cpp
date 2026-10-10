#include "ElysiumEntity.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumLineService.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Visual/ElysiumNpcVisual.h"   // GateLeaderCloth -- a native placed model's garments

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEntityBase, Log, All);

void FElysiumEntity::Construct(const FElysiumEntityDef& InDef, FElysiumEntityHandle InHandle, const FElysiumClassDesc& InClass)
{
	Def = &InDef;
	Handle = InHandle;
	Class = &InClass;
	TargetName = InDef.TargetName;
	Origin = InDef.Origin;   // the live copy; the def's is immutable (SetOrigin moves this one)
	// The producer hoists `StartHidden` out of the keys, so seed the member from the def before
	// the keyvalue walk below: an authored key still wins, and a def that carries only the
	// hoisted bool still lands on the word the datamap row binds.
	bStartHidden = InDef.bStartHidden;

	// Apply the raw keyvalues through the class chain field table. Only mapped base/leaf
	// fields are copied onto members; unmapped keys stay on the def (property-bag reads land
	// with the script host later). Spawn-time application ignores bKeyable — the write-gate is
	// for runtime Python/I/O, not the map's own keyvalues.
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	for (const TPair<FString, FString>& KV : InDef.Keys)
	{
		if (const FElysiumFieldAccessor* Acc = Reg.FindField(InClass, FName(*KV.Key)))
		{
			if (Acc->Set)
			{
				Acc->Set(*this, FElysiumVariant::String(KV.Value));
			}
		}
	}

	// Seed the per-output `times` counters from the def (the world counts them down as it fires).
	OutputTimesRemaining.Reserve(InDef.Outputs.Num());
	for (const FElysiumOutputDef& O : InDef.Outputs)
	{
		OutputTimesRemaining.Add(O.Times);
	}

	// start_hidden — born fully OFF. No prior think to save; the body build skips
	// collision + draw while bHidden.
	if (bStartHidden)
	{
		bHidden = true; // 0x100a8710, hidden is not EFL_DORMANT
		SavedThinkCallback = ThinkCallback; // FUNCTION SAVE +0xe4
		ThinkCallback = NAME_None; // NULL think +0x118
		NextThink = ELYSIUM_NEVER_THINK;
	}
}

void FElysiumEntity::ScriptHide()
{
	// CBaseEntity::ScriptHide (entity_io.md): early-out if already hidden; save the prior
	// think; next-think = never; go non-solid + undrawn (the body, via OnDormancyChanged).
	if (bHidden)
	{
		return;
	}
	SavedThinkCallback = ThinkCallback; // 0x100a8710 +0xe4
	ThinkCallback = NAME_None; // 0x100a8710 NULL think
	bSavedPhysicalWordsAvailable = ReadScriptPhysicalWords(ScriptSavedSolid, ScriptSavedMoveType,
		ScriptSavedMoveCollide, ScriptSavedSolidFlags, ScriptSavedEffects); // 0x100a8710; false until reader exists
	if (bSavedPhysicalWordsAvailable) WriteScriptPhysicalWords(0, 0, 0, 4, 0xe0); // 0x100a8710
	bHidden = true;
	SavedNextThink = NextThink;
	NextThink = ELYSIUM_NEVER_THINK;
	OnDormancyChanged();
}

void FElysiumEntity::ScriptUnhide()
{
	// The exact inverse: restore the saved think and clear the hidden flag; the body restores
	// its prior solidity + draw.
	if (!bHidden)
	{
		return;
	}
	bHidden = false;
	ThinkCallback = SavedThinkCallback; // 0x100a8990: reinstate saved callback
	NextThink = World ? static_cast<float>(World->NowSeconds()) : 0.0f; // 0x100a8990 due NOW
	if (bSavedPhysicalWordsAvailable) WriteScriptPhysicalWords(ScriptSavedSolid, ScriptSavedMoveType,
		ScriptSavedMoveCollide, ScriptSavedSolidFlags, ScriptSavedEffects); // 0x100a8990
	OnDormancyChanged();
}

void FElysiumEntity::Kill()
{
	// Terminal: mark dead and go inert immediately (a killed-but-not-yet-reaped entity must
	// not touch, trace, or think). The slot removal + handle invalidation is the world's job;
	// this only flips the entity's own state.
	if (bDead)
	{
		return;
	}
	NotifyOwnerOfTermination(EElysiumOwnedEntityTermination::RemovedAlive);
	if (World)
	{
		if (IElysiumAudio* Audio = World->Audio())
		{
			FElysiumAudioOwner AudioOwner;
			AudioOwner.Kind = EElysiumAudioOwnerKind::MapEntity;
			AudioOwner.StableId =
				FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
			Audio->CancelAudioOwner(MoveTemp(AudioOwner));
		}
		if (World->Lines())
		{
			World->Lines()->CancelSession(
				FString::Printf(TEXT("direct:%u:%d"), Handle.Epoch, Handle.Index));
			World->Lines()->CancelDialogue(Handle);
		}
	}
	// CBaseCombatCharacter::UpdateOnRemove 0x10327790 removes one comfort
	// entry before its handle is invalidated. Other entity classes have no entry.
	if (FElysiumCombatCharacter* Character = AsCombatCharacter()) { Character->RemoveFromComfortList(); }
	bDead = true;
	NextThink = ELYSIUM_NEVER_THINK;
	if (!bHidden)
	{
		OnDormancyChanged();   // drop the body's collision + draw (no-op with no body); notifies below
	}
	else if (World)
	{
		// Already hidden (OnDormancyChanged is skipped), but the visual state still changed
		// hidden -> dead, so a retained visualizer must still be told.
		World->NotifyVisualChanged(*this);
	}
	// 0x101cd940 actual removal, not Event_Killed: even an already hidden corpse owns physics.
	if (FElysiumNpcBase* const RemovedNpc = AsNpcBase())
	{
		if (IElysiumEmbodiment* const RemovalBody = World != nullptr ? World->Embodiment() : nullptr)
		{
			RemovalBody->ReleaseNpcVisual(RemovedNpc->Visual, RemovedNpc->GetNpcMotor());
		}
	}
}

void FElysiumEntity::NotifyOwnerOfTermination(EElysiumOwnedEntityTermination Reason)
{
	if (bOwnerTerminationNotified)
	{
		return;
	}
	bOwnerTerminationNotified = true; // latch before the callback: owner outputs may re-enter us
	if (!World || !OwnerEntity.IsSet())
	{
		return;
	}
	if (FElysiumEntity* Owner = World->Resolve(OwnerEntity))
	{
		Owner->OnOwnedEntityTerminated(*this, Reason);
	}
}

void FElysiumEntity::PlayDialogFile(const FString& AuthoredPath)
{
	if (!World || bFakeSilence || AuthoredPath.IsEmpty() || !World->Lines())
	{
		return;
	}
	FElysiumEntity* SoundOwner = this;
	if (!SoundOverrideEntityName.IsEmpty())
	{
		if (FElysiumEntity* Override = World->FindByName(SoundOverrideEntityName))
		{
			SoundOwner = Override;
		}
	}
	const FString Session =
		FString::Printf(TEXT("direct:%u:%d"), Handle.Epoch, Handle.Index);
	World->Lines()->PlayDirect(Session, AuthoredPath, SoundOwner->Origin,
		SoundOwner->GetSkeletalBody(), EElysiumAudioCategory::Auto);
	// `InputPlayDialogFile` (`0x102c2890`) hands the line player a zero duration; only a scene
	// (`CSceneEntity` `0x10081700`) passes the line's own length.
	OnDialogFilePlayed(0.0);
}

void FElysiumEntity::SetSoundOverrideEnt(const FString& EntityName)
{
	SoundOverrideEntityName = EntityName.TrimStartAndEnd();
}

void FElysiumEntity::SetFakeSilence(bool bEnabled)
{
	bFakeSilence = bEnabled;
	if (bEnabled && World && World->Lines())
	{
		World->Lines()->CancelSession(
			FString::Printf(TEXT("direct:%u:%d"), Handle.Epoch, Handle.Index));
		World->Lines()->CancelDialogue(Handle);
	}
}

UPrimitiveComponent* FElysiumEntity::GetAttachBody() const
{
	return Body ? static_cast<UPrimitiveComponent*>(Body)
		: GenericModelBody ? static_cast<UPrimitiveComponent*>(GenericModelBody) : GenericStaticModelBody;
}

USceneComponent* FElysiumEntity::GetAttachChild() const
{
	return GetAttachBody();
}

void FElysiumEntity::EnsurePlacedModelBody()
{
	if (GetAttachBody() || !World || !Def || Model.IsEmpty() || Def->ModelMesh.IsEmpty())
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment || !Embodiment->HasPlacedModelCatalogue())
	{
		return;
	}
	FElysiumPlacedModelRequest Request;
	Request.ModelPath = Model;
	Request.StaticStem = Def->ModelMesh;
	Request.Location = Origin;
	Request.Rotation = Def->ModelQuat;
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	const auto Placed = Embodiment->BuildPlacedModelBody(Request);
	GenericModelBody = Cast<USkeletalMeshComponent>(Placed.Visual);
	GenericStaticModelBody = GenericModelBody ? nullptr : Placed.Visual;
	if (GenericStaticModelBody)
	{
		World->RegisterPropBody(GenericStaticModelBody);
		GenericStaticModelBody->SetVisibility(!IsInert(), true);
	}
	if (GenericModelBody)
	{
		World->RegisterNpcBody(GenericModelBody);
		GenericModelBody->SetVisibility(!IsInert(), true);
		ElysiumNpcVisual::GateLeaderCloth(GenericModelBody, !IsInert());
	}
}

void FElysiumEntity::PostSpawn()
{
	ResolveParentAttachment(false);
}

// --- The retail matrix path of `CBaseEntity::SetParent` (`walks/L0-r010.md`) -------------------
//
// Source's row-major 4x4 (sixteen float32 words; the translation in words 3, 7, 11) and 3x4 (twelve
// words), computed in Source space: inches, right-handed, Source QAngle degrees (pitch, yaw, roll).
// The port's `Origin` is Unreal centimetres with Y reflected (`bsp.source_to_unreal`), so a vector
// crosses into this space through `ToSourceInches` and back through `FromSourceInches`; the angle
// words are Source's already. Constants read from the PE (`pe.py const`): `_DAT_1044eb08` =
// 0x3C8EFA35 = 0.017453292f (pi/180), `_DAT_10446758` = 0x42652EE1 = 57.29578f (180/pi),
// `_DAT_10454b8c` = 0x3A83126F = 0.001f. Products and sums are float32 as retail's x87 stores are;
// the per-element summation ORDER of `0x1024cf80` is not asm-verified (k = 0..3 here).
namespace ElysiumRetailMatrix
{
	constexpr float DegToRad = 0.017453292f;   // _DAT_1044eb08
	constexpr float RadToDeg = 57.29578f;      // _DAT_10446758
	constexpr float AnglesEpsilon = 0.001f;    // _DAT_10454b8c, `MatrixAngles`' degenerate test
	constexpr double CmPerInch = 2.54;

	void ToSourceInches(const FVector& Cm, float* Out)
	{
		Out[0] = static_cast<float>(Cm.X / CmPerInch);
		Out[1] = static_cast<float>(-Cm.Y / CmPerInch);
		Out[2] = static_cast<float>(Cm.Z / CmPerInch);
	}

	FVector FromSourceInches(float X, float Y, float Z)
	{
		return FVector(X * CmPerInch, -Y * CmPerInch, Z * CmPerInch);
	}

	// `FUN_1024c630(out)` 0x1024c630: the sixteen words of a 4x4 identity (1.0f at 0, 5, 10, 15).
	void Identity4(float* Out)
	{
		for (int32 I = 0; I < 16; ++I) Out[I] = 0.0f;
		Out[0] = Out[5] = Out[10] = Out[15] = 1.0f;
	}

	// `FUN_1024cef0(dst, src)` 0x1024cef0: the sixteen-word copy.
	void Copy4(float* Dst, const float* Src)
	{
		for (int32 I = 0; I < 16; ++I) Dst[I] = Src[I];
	}

	// `FUN_1024ddf0(out, origin, angles)` 0x1024ddf0: Source's `AngleMatrix` with the origin as
	// column 3 and the row (0, 0, 0, 1) appended. Each angle is multiplied by pi/180 in float32
	// before `FSINCOS`, and the sin/cos results are stored as float32; the products below are in the
	// body's own operand order.
	void AngleMatrix4(float* Out, const float* Origin, const float* Angles)
	{
		const float YawRad = Angles[1] * DegToRad;
		const float Cy = static_cast<float>(FMath::Cos(static_cast<double>(YawRad)));   // fVar1
		const float Sy = static_cast<float>(FMath::Sin(static_cast<double>(YawRad)));   // fVar2
		const float PitchRad = Angles[0] * DegToRad;
		const float Cp = static_cast<float>(FMath::Cos(static_cast<double>(PitchRad))); // fVar3
		const float Sp = static_cast<float>(FMath::Sin(static_cast<double>(PitchRad))); // fVar4
		const float RollRad = Angles[2] * DegToRad;
		const float Cr = static_cast<float>(FMath::Cos(static_cast<double>(RollRad)));  // fVar5
		const float Sr = static_cast<float>(FMath::Sin(static_cast<double>(RollRad)));  // fVar6
		Out[0] = Cp * Cy;
		Out[4] = Cp * Sy;
		Out[8] = -Sp;
		Out[1] = Cy * Sr * Sp - Cr * Sy;
		Out[5] = Cr * Cy + Sr * Sp * Sy;
		Out[9] = Sr * Cp;
		Out[2] = Sr * Sy + Cy * Cr * Sp;
		Out[6] = Cr * Sp * Sy - Sr * Cy;
		Out[10] = Cr * Cp;
		Out[3] = Origin[0];
		Out[7] = Origin[1];
		Out[11] = Origin[2];
		Out[12] = 0.0f;
		Out[13] = 0.0f;
		Out[14] = 0.0f;
		Out[15] = 1.0f;
	}

	// `FUN_1024da80(this, out)` 0x1024da80: the full 4x4 transpose.
	void Transpose4(const float* In, float* Out)
	{
		for (int32 R = 0; R < 4; ++R)
			for (int32 C = 0; C < 4; ++C)
				Out[R * 4 + C] = In[C * 4 + R];
	}

	// `FUN_1024cf80(this, p1, out)` 0x1024cf80: `out[r][c] = sum_k this[r][k] * p1[k][c]`. Every
	// operand is loaded before the first store, so `out` may alias `this` (SetParent's call does).
	void Concat4(const float* A, const float* B, float* Out)
	{
		float Tmp[16];
		for (int32 R = 0; R < 4; ++R)
		{
			for (int32 C = 0; C < 4; ++C)
			{
				float Sum = A[R * 4] * B[C];
				Sum += A[R * 4 + 1] * B[4 + C];
				Sum += A[R * 4 + 2] * B[8 + C];
				Sum += A[R * 4 + 3] * B[12 + C];
				Tmp[R * 4 + C] = Sum;
			}
		}
		Copy4(Out, Tmp);
	}

	// `FUN_100a0a10(M, out, c)` 0x100a0a10: `out = R^T * (c - t)`, R the upper 3x3 of M and t its
	// translation column: component i is `sum_k (c - t)_k * M[k*4 + i]`.
	void InverseTransformPoint(const float* M, const float* C, float* Out)
	{
		const float D0 = C[0] - M[3];
		const float D1 = C[1] - M[7];
		const float D2 = C[2] - M[11];
		Out[0] = D0 * M[0] + D1 * M[4] + D2 * M[8];
		Out[1] = D0 * M[1] + D1 * M[5] + D2 * M[9];
		Out[2] = D0 * M[2] + D1 * M[6] + D2 * M[10];
	}

	// `FUN_100a0990(this, v0..v11)` 0x100a0990: twelve words into a contiguous 3x4 -- SetParent
	// hands it the first twelve words of the 4x4 product.
	void Store3x4(float* Out, const float* In)
	{
		for (int32 I = 0; I < 12; ++I) Out[I] = In[I];
	}

	// `FUN_10137ed0(Q, A)` 0x10137ed0: Source's `MatrixAngles` over a row-major 3x4. `xy =
	// sqrt(Q0^2 + Q4^2)`; `0.001f < xy`: yaw `A[1] = atan2(Q4, Q0)`, pitch `A[0] = atan2(-Q8, xy)`,
	// roll `A[2] = atan2(Q9, Q10)`; otherwise yaw `atan2(-Q1, Q5)`, roll 0, pitch as before. Each in
	// degrees through 57.29578f, stored as float32. The translation column is not read.
	void MatrixAngles(const float* Q, float* A)
	{
		const double Xy = FMath::Sqrt(static_cast<double>(Q[0]) * Q[0] + static_cast<double>(Q[4]) * Q[4]);
		if (static_cast<double>(AnglesEpsilon) < Xy)
		{
			A[1] = static_cast<float>(FMath::Atan2(static_cast<double>(Q[4]), static_cast<double>(Q[0])) * RadToDeg);
			A[0] = static_cast<float>(FMath::Atan2(-static_cast<double>(Q[8]), Xy) * RadToDeg);
			A[2] = static_cast<float>(FMath::Atan2(static_cast<double>(Q[9]), static_cast<double>(Q[10])) * RadToDeg);
			return;
		}
		A[2] = 0.0f;
		A[1] = static_cast<float>(FMath::Atan2(-static_cast<double>(Q[1]), static_cast<double>(Q[5])) * RadToDeg);
		A[0] = static_cast<float>(FMath::Atan2(-static_cast<double>(Q[8]), Xy) * RadToDeg);
	}

	// `FUN_101d15f0(out, ent)` 0x101d15f0: `AngleMatrix(ent->GetOrigin(), ent->GetAngles())` -- the
	// LOCAL words, slots 220 and 221 (221 is called first) -- when the entity exists and has an edict
	// (`+0x2e0`); otherwise the identity (`0x1024c630` then `0x1024cef0`). Every live port entity
	// stands for an edict-bearing one (as `ElysiumNpcMaker.cpp` reads the same gate), so the identity
	// arm is reached only by a NULL entity, which SetParent never passes.
	void EntityMatrix(float* Out, FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			float Identity[16];
			Identity4(Identity);
			Copy4(Out, Identity);
			return;
		}
		const FVector Angles = Entity->GetAngles();   // slot 221, vtable +0x374, first
		const FVector Origin = Entity->GetOrigin();   // slot 220, vtable +0x370
		float SrcOrigin[3], SrcAngles[3] = { static_cast<float>(Angles.X), static_cast<float>(Angles.Y),
			static_cast<float>(Angles.Z) };
		ToSourceInches(Origin, SrcOrigin);
		AngleMatrix4(Out, SrcOrigin, SrcAngles);
	}
}

void FElysiumEntity::SetParent(FElysiumEntity* Parent, uint8 Attachment)
{
	using namespace ElysiumRetailMatrix;
	// `CBaseEntity::SetParent` `0x100a0670` (`walks/L0-r010.md`). The scope-trace frame
	// ("CBaseEntity::SetParent", `0x105558d0`) pushed on entry and popped on every exit is not modelled.
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("set_parent"), TEXT("CBaseEntity::SetParent"), 0x100a0670u, TEXT("entry"),
			FString::Printf(TEXT("this=%s parent=%s attach=%d dirty=%d"), *Handle.ToString(),
				Parent ? *Parent->Handle.ToString() : TEXT("null"), Attachment, (EFlags & 0x800u) != 0 ? 1 : 0));
	}
	// 1. `0x1012c840`: unlink from the current move parent, preserving the pose.
	UnlinkFromMoveParent();
	// 2. `+0x1b1 = 1`, before the parent test (the bad-parent path writes it too).
	bNetworkChanged = true;
	// 3. `m_pParent` <- `0x100a0ae0(parent)`: the parent's own handle, or -1 for NULL.
	ParentHandle = Parent != nullptr ? Parent->Handle : FElysiumEntityHandle::Invalid();
	// 4. `0x100290c0`: the handle must resolve with its serial, else the bad-parent arm.
	FElysiumEntity* P = World != nullptr ? World->Resolve(ParentHandle) : nullptr;
	if (P == nullptr)
	{
		// `Msg("Entity %s(%s) set bad parent\n", m_iClassname or "", GetDebugName())` (`0x105558ec`),
		// `m_iParent = 0`, exit. No link, no pose, no notify; `m_pParent` keeps what step 3 wrote.
		UE_LOG(LogElysiumEntityBase, Log, TEXT("Entity %s(%s) set bad parent"),
			Def ? *Def->Classname : TEXT(""), *DebugString());
		ParentName.Empty();
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("set_parent"), TEXT("CBaseEntity::SetParent"), 0x100a0670u, TEXT("bad_parent"),
				FString::Printf(TEXT("this=%s m_pParent=%s m_iParent=0"), *Handle.ToString(),
					ParentHandle.IsSet() ? *ParentHandle.ToString() : TEXT("-1")));
		}
		return;
	}
	// 5. `m_iParent` (+0x124) <- the parent's `m_iName` (+0x26c).
	ParentName = P->TargetName;
	// 6. Gate `this[+0x2e0] != 0` (`m_pEdict`): every live port entity passes, as every edict-bearing
	// retail entity does; which retail classes hold zero here is UNRECOVERED (`walks/L0-r010.md` Open 3).
	{
		// 6a. `0x1012c7a0(P, this)`: prepend this to the parent's child chain (P re-resolved from +0x254).
		LinkMoveChild(*P, *this);
		// 6b. `+0x258` <- the attach byte.
		ParentAttachment = Attachment;
		// 6c. `0x101d1530(M, E, attach)`, E = entity(+0x25c) -- the parent-pose producer, the L1 hook of
		// `hooks.tsv:8`. E == NULL -> identity; otherwise, when `attach != 0` and E's slot 137
		// (`GetBaseAnimating`) answers, `GetAttachment02` (`0x10092ef0`, L1) is sampled into M and then
		// unconditionally OVERWRITTEN by `AngleMatrix(E->GetAbsOrigin(), E->GetAbsAngles())` (slots 217,
		// 219): the attachment result is discarded, and no corpus caller passes a non-zero attach anyway.
		float M[16];
		FElysiumEntity* E = World->Resolve(MoveParent);
		if (E == nullptr)
		{
			float Identity[16];
			Identity4(Identity);
			Copy4(M, Identity);
		}
		else
		{
			if (Attachment != 0 && E->GetBaseAnimating() != nullptr)
			{
				// HOOK (L1 animation, `hooks.tsv:8`): `CBaseAnimating::GetAttachment02` `0x10092ef0` would
				// run here and its result be thrown away by the fall-through below. Not run.
				World->EmitRetailSite(*this, TEXT("parent_pose"), TEXT("FUN_101d1530"), 0x101d1530u, TEXT("hook"),
					FString::Printf(TEXT("fn=CBaseAnimating::GetAttachment02 va=0x10092ef0 attach=%d discarded=1"), Attachment));
			}
			const FVector ParentAbsAngles = E->GetAbsAngles();   // slot 219, vtable +0x36c, first
			const FVector ParentAbsOrigin = E->GetAbsOrigin();   // slot 217, vtable +0x364
			float SrcOrigin[3], SrcAngles[3] = { static_cast<float>(ParentAbsAngles.X),
				static_cast<float>(ParentAbsAngles.Y), static_cast<float>(ParentAbsAngles.Z) };
			ToSourceInches(ParentAbsOrigin, SrcOrigin);
			AngleMatrix4(M, SrcOrigin, SrcAngles);
		}
		World->EmitRetailSite(*this, TEXT("parent_pose"), TEXT("FUN_101d1530"), 0x101d1530u, TEXT("return"),
			FString::Printf(TEXT("parent=%s M=[%g %g %g %g %g %g %g %g %g %g %g %g]"),
				E ? *E->Handle.ToString() : TEXT("null"), M[0], M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11]));
		// The local words come into use here (see `bParentLocalPose`): an entity that arrives
		// unparented has local == abs, which is what retail's two words hold at this point; one
		// whose stale link `0x1012c840` could not unlink keeps its old local words, as retail does.
		if (!bParentLocalPose)
		{
			LocalOrigin = Origin;
			LocalAngles = Angles;
			bParentLocalPose = true;
		}
		// 6d. `0x101d15f0(C, this)`: this entity's LOCAL pose as a matrix.
		float C[16];
		EntityMatrix(C, this);
		// 6e. `0x100a0a10(&M, o, GetOrigin())`: the local origin `o = R^T (c - t)`.
		float SrcLocal[3], O[3];
		ToSourceInches(GetOrigin(), SrcLocal);   // slot 220, vtable +0x370
		InverseTransformPoint(M, SrcLocal, O);
		// 6f. `0x100b5340(this, 0x800, 0)`: this entity and every descendant get the dirty bit.
		InvalidateTransform(0x800u, 0u);
		// 6g. `T = transpose(M)` (`0x1024da80`), then `M := T * C` (`0x1024cf80`, output aliasing M).
		float T[16];
		Transpose4(M, T);
		Concat4(T, C, M);
		// 6h. `0x100a0990`: the first twelve words as a 3x4; `0x10137ed0`: its Euler angles.
		float Q[12], A[3];
		Store3x4(Q, M);
		MatrixAngles(Q, A);
		World->EmitRetailSite(*this, TEXT("set_parent"), TEXT("CBaseEntity::SetParent"), 0x100a0670u, TEXT("pose"),
			FString::Printf(TEXT("local_origin=[%g %g %g] local_angles=[%g %g %g]"), O[0], O[1], O[2], A[0], A[1], A[2]));
		// 6i. slot 64 `SetAngles(A)` (`0x100b2d00`): changed-only.
		SetAngles(FRotator(A[0], A[1], A[2]));
		// 6j. `UTIL_SetOrigin(this, &o, bTouch = 0)` (`0x101cf5c0`, the `PUSH 0` at `0x100a089f`): slot 62
		// `SetOrigin(o)` (`0x100b2be0`) and NO `PhysicsTouchTriggers`.
		SetOrigin(FromSourceInches(O[0], O[1], O[2]));
		World->EmitRetailSite(*this, TEXT("set_parent"), TEXT("CBaseEntity::SetParent"), 0x100a0670u, TEXT("link"),
			FString::Printf(TEXT("m_pMoveParent=%s m_pMovePeer=%s parent.m_pMoveChild=%s m_iParentAttachment=%d m_iEFlags=0x%x"),
				*MoveParent.ToString(), MovePeer.IsSet() ? *MovePeer.ToString() : TEXT("-1"), *P->MoveChild.ToString(),
				ParentAttachment, EFlags));
	}
	// 7. Notify: `entity(+0x254)->vfn[115](this)`. Every one of the 497 classes that fill slot 115 holds
	// the empty `0x10026bf0` (`vtmb_slot 115`), so nothing happens; the dispatch is kept.
	if (FElysiumEntity* P2 = World->Resolve(ParentHandle))
	{
		P2->Slot115(this);
		World->EmitRetailSite(*this, TEXT("set_parent"), TEXT("CBaseEntity::SetParent"), 0x100a0670u, TEXT("notify"),
			FString::Printf(TEXT("parent=%s fn=CAISound::FUN_10026bf0 va=0x10026bf0"), *P2->Handle.ToString()));
	}
}

void FElysiumEntity::SetParentByName(const TCHAR* Name, const FElysiumEntityHandle& Activator)
{
	// `CBaseEntity::SetParent(const char*, CBaseEntity*)` `0x100a04e0` (same scope-frame name as the
	// pointer overload). A NULL name is handed to the finder as "" (which matches nothing, `0x100f7770`:
	// `*name != 0` fails) and skips the bad-parent Msg, so it reaches `SetParent(this, NULL, 0)` below.
	if (World == nullptr)
	{
		return;
	}
	const FString Pattern = Name != nullptr ? FString(Name) : FString();
	// `FindEntityByName(list, start = NULL, name, activator, 0)` `0x100f7770`: a leading `!` takes the
	// single-result path (`0x100f7460`, not walked here: `!activator` is the inputdata's activator);
	// otherwise the first live case-insensitive match (a trailing `*` a prefix). The second call
	// starts after the first match: a second match is the "ambigious parent" Msg and nothing more.
	TArray<FElysiumEntity*> Matches;
	if (Pattern.StartsWith(TEXT("!")))
	{
		FElysiumEntity* Special = Pattern.Equals(TEXT("!activator"), ESearchCase::IgnoreCase)
			? World->Resolve(Activator) : World->FindByName(Pattern);
		if (Special != nullptr) Matches.Add(Special);
	}
	else
	{
		World->ForEachNamed(Pattern, [&Matches](FElysiumEntity& E) { if (!E.IsDead()) Matches.Add(&E); });
	}
	FElysiumEntity* Found = Matches.Num() >= 1 ? Matches[0] : nullptr;
	if (Name != nullptr && Found == nullptr)
	{
		// `Msg("Entity %s(%s) has bad parent %s\n", m_iClassname or "", GetDebugName(), name)` (`0x105558a8`);
		// returns WITHOUT calling SetParent: nothing changes.
		UE_LOG(LogElysiumEntityBase, Log, TEXT("Entity %s(%s) has bad parent %s"),
			Def ? *Def->Classname : TEXT(""), *DebugString(), Name);
		World->EmitRetailSite(*this, TEXT("set_parent_name"), TEXT("CBaseEntity::SetParent"), 0x100a04e0u, TEXT("bad_parent"),
			FString::Printf(TEXT("name=%s"), Name));
		return;
	}
	if (Matches.Num() >= 2)
	{
		// `Msg("Entity %s(%s) has ambigious parent %s\n", ...)` (`0x10555878`) and continue with the first.
		UE_LOG(LogElysiumEntityBase, Log, TEXT("Entity %s(%s) has ambigious parent %s"),
			Def ? *Def->Classname : TEXT(""), *DebugString(), *Pattern);
		World->EmitRetailSite(*this, TEXT("set_parent_name"), TEXT("CBaseEntity::SetParent"), 0x100a04e0u, TEXT("ambiguous"),
			FString::Printf(TEXT("name=%s matches=%d"), *Pattern, Matches.Num()));
	}
	SetParent(Found, 0);   // `thunk_FUN_100a0670(this, found, 0)`
}

void FElysiumEntity::InputSetParent(const FElysiumInputArgs& Args)
{
	// `CBaseEntity::InputSetParent` `0x100ad030`: the name is the input's string value when its variant
	// type is 2 (string), else NULL; the activator is `inputdata[0]`.
	const FString Name = Args.Param.Type == EElysiumVariantType::String ? Args.Param.ToString() : FString();
	SetParentByName(Args.Param.Type == EElysiumVariantType::String ? *Name : nullptr, Args.Activator);
}

void FElysiumEntity::ClearParent()
{
	// `CBaseEntity::ClearParent` `0x100a0b20` and `InputClearParent` `0x100ad100`: each is only
	// `0x1012c840(this)`; `m_pParent`, `m_iParent` and `m_iParentAttachment` are left as they were.
	UnlinkFromMoveParent();
}

void FElysiumEntity::UnlinkFromMoveParent()
{
	// `FUN_1012c840` `0x1012c840`: `m_pMoveParent` must be a non -1 handle whose entity-list entry has
	// the matching serial and a non-NULL pointer; the second resolve inside repeats the same test, so
	// the `0x1012c6c0(0, this)` arm is unreachable. A stale or invalid link does nothing and LEAVES
	// `m_pMoveParent` unchanged.
	if (!MoveParent.IsSet() || World == nullptr)
	{
		return;
	}
	FElysiumEntity* A = World->Resolve(MoveParent);
	if (A == nullptr)
	{
		return;
	}
	World->EmitRetailSite(*this, TEXT("unlink"), TEXT("FUN_1012c840"), 0x1012c840u, TEXT("unlink"),
		FString::Printf(TEXT("old_parent=%s m_pMovePeer=%s dirty=%d"), *A->Handle.ToString(),
			MovePeer.IsSet() ? *MovePeer.ToString() : TEXT("-1"), (EFlags & 0x800u) != 0 ? 1 : 0));
	UnlinkMoveChild(*A, *this);
	ClearMoveLinks();
	DetachBodyFromParent(*A);
}

void FElysiumEntity::UnlinkMoveChild(FElysiumEntity& Parent, FElysiumEntity& Child)
{
	// `FUN_1012c6c0(parent, child)` `0x1012c6c0`: walk the parent's chain from `m_pMoveChild` through
	// each `m_pMovePeer`; at the node that is the child, write into the predecessor's link (the
	// parent's `m_pMoveChild` when the child is first) the successor's re-read handle, or -1 when the
	// successor does not resolve. A child not in the chain changes nothing. The child's own
	// `m_pMovePeer` is NOT cleared here.
	FElysiumEntityWorld* World = Parent.World;
	if (World == nullptr)
	{
		return;
	}
	FElysiumEntityHandle* Link = &Parent.MoveChild;
	FElysiumEntity* Node = World->Resolve(*Link);
	int32 Guard = 0;
	while (Node != nullptr && Guard++ < 8192)
	{
		FElysiumEntityHandle* NodePeerLink = &Node->MovePeer;
		FElysiumEntity* Next = World->Resolve(Node->MovePeer);
		if (Node == &Child)
		{
			*Link = Next != nullptr ? Next->Handle : FElysiumEntityHandle::Invalid();
			return;
		}
		Node = Next;
		Link = NodePeerLink;
	}
}

void FElysiumEntity::ClearMoveLinks()
{
	// `FUN_1012c7f0(this)` `0x1012c7f0`: `m_pMoveParent = -1; m_pMovePeer = -1;` then
	// `SetAngles(GetAbsAngles())` (slot 219 -> 64) and `SetOrigin(GetAbsOrigin())` (slot 217 -> 62).
	// With the link already cleared, a getter that finds the 0x800 bit set recomputes with no parent
	// (abs := local) -- and `SetAngles` SETS that bit when the angles differ, so a child whose local
	// angles differ from its absolute ones (a rotated parent) hands `SetOrigin` its LOCAL origin as
	// the world one. Reproduced verbatim (`walks/L0-r010.md` Open 5).
	MoveParent = FElysiumEntityHandle::Invalid();
	MovePeer = FElysiumEntityHandle::Invalid();
	const FVector AbsAngles = GetAbsAngles();
	SetAngles(FRotator(AbsAngles.X, AbsAngles.Y, AbsAngles.Z));
	const FVector AbsOrigin = GetAbsOrigin();
	SetOrigin(AbsOrigin);
	// The two words fold back into one (`bParentLocalPose`): what any getter answers from here on is
	// the local word (the dirty bit is set when a write ran, and `CalcAbsolutePosition`'s no-parent
	// arm copies local to abs; when none ran the two already agree). The bit itself stays as the
	// writes left it, as retail's does.
	if (bParentLocalPose)
	{
		const FVector FoldOrigin = LocalOrigin;
		const FVector FoldAngles = LocalAngles;
		bParentLocalPose = false;
		SetRuntimeTransform(FoldOrigin, FoldAngles);
	}
}

void FElysiumEntity::LinkMoveChild(FElysiumEntity& Parent, FElysiumEntity& Child)
{
	// `FUN_1012c7a0(parent, child)` `0x1012c7a0`, the prepend, in write order: the child's peer takes
	// the parent's previous first child (a raw handle copy), the parent's first child becomes the
	// child's own handle (slot 1 `GetRefEHandle`), the child's move parent the parent's.
	Child.MovePeer = Parent.MoveChild;
	Parent.MoveChild = Child.Handle;
	Child.MoveParent = Parent.Handle;
	if (Child.World)
	{
		Child.World->EmitRetailSite(Child, TEXT("prepend"), TEXT("FUN_1012c7a0"), 0x1012c7a0u, TEXT("link"),
			FString::Printf(TEXT("parent=%s child.m_pMovePeer=%s parent.m_pMoveChild=%s"), *Parent.Handle.ToString(),
				Child.MovePeer.IsSet() ? *Child.MovePeer.ToString() : TEXT("-1"), *Parent.MoveChild.ToString()));
	}
}

void FElysiumEntity::InvalidateTransform(uint32 SelfBits, uint32 ChildBits)
{
	// `FUN_100b5340(this, selfBits, childBits)` `0x100b5340`: `m_iEFlags |= selfBits`; then for the
	// first child (`+0x260`) and each peer after it (`+0x264`), recurse with `selfBits | childBits`
	// and 0. The guard bounds a malformed cycle (a self-parented row) retail would spin on.
	EFlags |= SelfBits;
	if (World == nullptr)
	{
		return;
	}
	FElysiumEntity* Child = World->Resolve(MoveChild);
	int32 Guard = 0;
	while (Child != nullptr && Guard++ < 8192)
	{
		if (Child != this)
		{
			Child->InvalidateTransform(SelfBits | ChildBits, 0u);
		}
		Child = World->Resolve(Child->MovePeer);
	}
}

void FElysiumEntity::SetAimEnt(FElysiumEntity* Aim)
{
	// `CBaseEntity::SetAimEnt` `0x1009ee80` (scope-traced): `m_hAimEnt` <- the entity's own handle
	// (slot 1), or -1 for NULL.
	AimEnt = Aim != nullptr ? Aim->Handle : FElysiumEntityHandle::Invalid();
}

void FElysiumEntity::ComputeAbsolutePose(FVector& OutOrigin, FVector& OutAngles) const
{
	using namespace ElysiumRetailMatrix;
	// `CBaseEntity::CalcAbsolutePosition` `0x100b1ac0`, the arithmetic: `m_rgflCoordinateFrame` is
	// rebuilt from the local words (`0x10139b90` AngleMatrix, `0x10138760` the translation); with no
	// move parent abs := local; otherwise `GetParentToWorldTransform` (the parent's own frame --
	// `m_iParentAttachment` is 0 at every corpus call) is concatenated with it (`0x10138df0`), the
	// translation column is the absolute origin, and the absolute angles are the parent's own
	// (`GetAbsAngles`, slot 219) when the local angles are exactly (0, 0, 0) (`DAT_1070d9d0/d4/d8`,
	// 0.0f each) and `MatrixAngles` (`0x10137ed0`) of the product otherwise.
	FElysiumEntity* P = World != nullptr ? World->Resolve(MoveParent) : nullptr;
	if (P == nullptr)
	{
		OutOrigin = LocalOriginWord();
		OutAngles = LocalAnglesWord();
		return;
	}
	const FVector ParentAbsOrigin = P->GetAbsOrigin();
	const FVector ParentAbsAngles = P->GetAbsAngles();
	const FVector& Local = LocalOriginWord();
	const FVector& LocalAng = LocalAnglesWord();
	float ParentSrc[3], ParentAng[3] = { static_cast<float>(ParentAbsAngles.X), static_cast<float>(ParentAbsAngles.Y),
		static_cast<float>(ParentAbsAngles.Z) };
	float LocalSrc[3], LocalAngF[3] = { static_cast<float>(LocalAng.X), static_cast<float>(LocalAng.Y),
		static_cast<float>(LocalAng.Z) };
	ToSourceInches(ParentAbsOrigin, ParentSrc);
	ToSourceInches(Local, LocalSrc);
	float ParentToWorld[16], LocalFrame[16], World4[16];
	AngleMatrix4(ParentToWorld, ParentSrc, ParentAng);
	AngleMatrix4(LocalFrame, LocalSrc, LocalAngF);
	Concat4(ParentToWorld, LocalFrame, World4);
	OutOrigin = FromSourceInches(World4[3], World4[7], World4[11]);
	if (LocalAngF[0] == 0.0f && LocalAngF[1] == 0.0f && LocalAngF[2] == 0.0f)
	{
		OutAngles = ParentAbsAngles;
	}
	else
	{
		float Q[12], A[3];
		Store3x4(Q, World4);
		MatrixAngles(Q, A);
		OutAngles = FVector(A[0], A[1], A[2]);
	}
}

void FElysiumEntity::DetachBodyFromParent(const FElysiumEntity& OldParent)
{
	USceneComponent* ChildBody = GetAttachChild();
	UPrimitiveComponent* ParentBody = OldParent.GetAttachBody();
	if (ChildBody != nullptr && ParentBody != nullptr && ChildBody->GetAttachParent() == ParentBody)
	{
		ChildBody->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}

bool FElysiumEntity::ResolveParentAttachment(bool bWarnIfPending)
{
	// The Unreal half only. The logical link is retail's: the map parse (`0x10136650`) ran
	// `SetParent` (`0x100a0670`) for every row whose `parentname` resolved, before any `Spawn`, and the
	// `SetParent` / `ClearParent` inputs run it afterwards. Here the body hangs under the move
	// parent's body, keeping its world transform: the local words already describe the same pose.
	FElysiumEntity* Parent = World != nullptr ? World->Resolve(MoveParent) : nullptr;
	if (Parent == nullptr || Parent == this)
	{
		return true;
	}

	USceneComponent* ChildBody = GetAttachChild();
	if (!ChildBody)
	{
		return true; // valid logical parenting between entities that need no scene component
	}
	UPrimitiveComponent* ParentBody = Parent->GetAttachBody();
	if (!ParentBody)
	{
		if (bWarnIfPending)
		{
			UE_LOG(LogElysiumEntityBase, Warning,
				TEXT("%s resolved parent '%s', but its attachment body is unavailable"),
				*DebugString(), *ParentName);
		}
		return false;
	}
	if (ChildBody->GetAttachParent() == ParentBody)
	{
		return true;
	}
	const FTransform ParentWorld = ParentBody->GetComponentTransform();
	if (!ChildBody->AttachToComponent(ParentBody, FAttachmentTransformRules::KeepWorldTransform))
	{
		if (bWarnIfPending)
		{
			UE_LOG(LogElysiumEntityBase, Warning, TEXT("%s failed to attach to parent '%s'"),
				*DebugString(), *ParentName);
		}
		return false;
	}
	OnParentAttached(ParentWorld);
	return true;
}

void FElysiumEntity::OnDormancyChanged()
{
	if (GenericStaticModelBody) GenericStaticModelBody->SetVisibility(!IsInert(), true);
	// One reversible switch. Inert (hidden or dead) drops the body's collision so it cannot
	// be touched or traced; active restores its built solidity. Idempotent (SetDormant re-applies).
	RefreshBrushBodyState();
	if (GenericModelBody)
	{
		GenericModelBody->SetVisibility(!IsInert(), true);
		// Propagation sets bVisible on the cloth children, but the cloth component may restore
		// its own bVisible on an asset update; HiddenInGame is the gate that holds.
		ElysiumNpcVisual::GateLeaderCloth(GenericModelBody, !IsInert());
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
	// The visual (colour/visibility) changed; let a retained gizmo layer dirty this one
	// instance on the event rather than polling every entity every frame. No-op in normal play.
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

void FElysiumEntity::RefreshBrushBodyState()
{
	const bool bEnabled = IsBrushBodyEnabled();
	if (!bEnabled && World)
	{
		World->EndBrushTouches(Handle);
	}
	if (Body)
	{
		Body->SetDormant(!bEnabled);
	}
}

void FElysiumEntity::SetRuntimeOrigin(const FVector& NewOrigin)
{
	Origin = NewOrigin;
	OnRuntimeTransformChanged();
}

void FElysiumEntity::SetRuntimeAngles(const FVector& NewAngles)
{
	Angles = NewAngles;
	OnRuntimeTransformChanged();
}

void FElysiumEntity::SetRuntimeTransform(const FVector& NewOrigin, const FVector& NewAngles)
{
	Origin = NewOrigin;
	Angles = NewAngles;
	OnRuntimeTransformChanged();
}

void FElysiumEntity::SetRuntimeModel(const FString& NewModel)
{
	Model = NewModel;
	OnRuntimeModelChanged();
}

void FElysiumEntity::OnRuntimeTransformChanged()
{
	if (GenericStaticModelBody) GenericStaticModelBody->SetWorldLocationAndRotation(
		Origin, FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)));
	// A brush body is cooked static at build (BuildBrushBody never sets it Movable), and scripts only
	// SetOrigin/SetAngles point entities (props/items/NPCs) in practice — so the base does not move the
	// body. The authoritative Origin/Angles fields are already updated; a leaf with a movable body
	// overrides this to follow. Tell a retained visualizer the transform changed either way.
	if (GenericModelBody)
	{
		GenericModelBody->SetWorldLocationAndRotation(
			Origin, FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)));
	}
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

void FElysiumEntity::OnRuntimeModelChanged()
{
	if (GenericStaticModelBody)
	{
		GenericStaticModelBody->DestroyComponent();
		GenericStaticModelBody = nullptr;
	}
	if (GenericModelBody)
	{
		GenericModelBody->DestroyComponent();
		GenericModelBody = nullptr;
	}
	EnsurePlacedModelBody();
}

void FElysiumEntity::FireOutput(FName Output, const FElysiumEntityHandle& Activator)
{
	if (World)
	{
		World->FireOutput(*this, Output, Activator, FElysiumVariant::Void());
	}
}

void FElysiumEntity::FireOutput(FName Output, const FElysiumEntityHandle& Activator, const FElysiumVariant& Value)
{
	if (World)
	{
		World->FireOutput(*this, Output, Activator, Value);
	}
}

FString FElysiumEntity::DebugString() const
{
	const FString Name = TargetName.IsEmpty() ? TEXT("<noname>") : TargetName;
	const FString Cls = Def ? Def->Classname : (Class ? Class->ClassName.ToString() : TEXT("<?>"));
	return FString::Printf(TEXT("#%d %s(%s)"), Handle.Index, *Name, *Cls);
}
