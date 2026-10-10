// func_illusionary -- `CFuncIllusionary` (`walks/L0-r013.md`, story L0.effects_world.illusionary-brush-
// spawn): a purely visual brush. `CFuncIllusionary::Spawn` 0x100bf760 is the class's one body: zero
// angles, MOVETYPE_NONE, SOLID_NONE, the authored model re-set through `CBaseEntity::SetModel`
// 0x100ad460 (which recomputes the collision bounds from the brush model and marks `+0x1b1`), and the
// `m_NetworkChangeState` byte 0 (+0x1b0) set. The brush body and its visual are the world's
// (`FElysiumEntityWorld::BuildBrushBody`: `EElysiumBrushSolidity::None`, the mesh from the def), the
// bounds a brush model's own; this leaf writes the retail words those stand for.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumIllusionary, Log, All);

namespace
{
	FString IllusionaryAng(const FVector& A)
	{
		return FString::Printf(TEXT("%g,%g,%g"), A.X == 0.0 ? 0.0 : A.X, A.Y == 0.0 ? 0.0 : A.Y, A.Z == 0.0 ? 0.0 : A.Z);
	}
}

class FElysiumFuncIllusionary final : public FElysiumEntity
{
public:
	// `m_NetworkChangeState` byte 0 (+0x1b0; the ledger's `layout.md:86`): the bool `FUN_101466e0`
	// writes 1 at the end of Spawn. Replication state (the send-due query is engine-side), which
	// Unreal's replication replaces; carried because retail writes it and a record reads it.
	bool bNetworkChangeState = false;

	virtual void Spawn() override;

private:
	void SetBrushModel(const FString& Name);   // CBaseEntity::SetModel 0x100ad460
	void Site(const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload) const
	{
		if (World)
		{
			World->EmitRetailSite(*this, Tag, Fn, Va, Phase, Payload);
		}
	}
};

static TUniquePtr<FElysiumEntity> MakeFuncIllusionary()
{
	return MakeUnique<FElysiumFuncIllusionary>();
}

static FElysiumClassRegistrar GRegFuncIllusionary(
	TEXT("func_illusionary"), ElysiumBaseClassName(), &MakeFuncIllusionary,
	[](FElysiumClassDesc& D)
	{
		ElysiumAddClassField(D, TEXT("m_NetworkChangeState"), &FElysiumFuncIllusionary::bNetworkChangeState, EElysiumField::None);
		// `m_Collision`'s solid type (+0x2b0, the int `SetSolid` 0x100dc480 writes), by Source's member name.
		ElysiumAddClassFieldVia<FElysiumFuncIllusionary>(D, TEXT("m_nSolidType"), [](auto& E) -> auto& { return E.RetailSolidType; }, EElysiumField::None);
	});

void FElysiumFuncIllusionary::Spawn()
{
	// `CFuncIllusionary::Spawn` 0x100bf760 (`walks/L0-r013.md`), arms in retail order. It calls neither
	// `CBaseEntity::Spawn` nor `Precache` (slot 104 is the inherited `ret`).
	const FVector Before = LocalAnglesWord();
	Site(TEXT("brush.spawn"), TEXT("CFuncIllusionary::Spawn"), 0x100bf760u, TEXT("entry"),
		FString::Printf(TEXT("model=%s angles=%s m_MoveType=%d m_MoveCollide=%d m_nSolidType=%d m_bChanged=%d"),
			Model.IsEmpty() ? TEXT("(empty)") : *Model, *IllusionaryAng(Before), GetMoveType(), GetMoveCollide(), RetailSolidType, bNetworkChanged ? 1 : 0));
	// 1. `SetAngles(&DAT_1070d9d0)` (slot 64, 0x100b2d00): the static (0,0,0) (zeroed by
	//    `staticinit_10137100`). Written -- with the EFL invalidation and `+0x1b1` -- only when the
	//    stored angles differ.
	SetAngles(FRotator(0.0, 0.0, 0.0));
	// 2. `SetMoveType(0, 0)` (slot 93, 0x100aad70): MOVETYPE_NONE, movecollide 0, on change only.
	SetMoveType(0, 0);
	// 3. `SetSolid(&m_Collision, 0)` (thunk 0x1000428c -> 0x100dc480, under a "CBaseEntity::SetSolid"
	//    scope frame naming `m_iName`): SOLID_NONE, the helpers only on change. The port's collision
	//    words; the body the world builds for this classname is `EElysiumBrushSolidity::None`.
	RetailSolidType = 0;
	++RetailSolidSets;
	// 4. `SetModel(GetModelName() or "")` (slot 105, `CBaseEntity::SetModel` 0x100ad460).
	SetBrushModel(Model);
	// 5. `FUN_101466e0(this + 0x1b0, 1)` (thunk 0x10012e04): `*(bool*)(this + 0x1b0) = true`.
	bNetworkChangeState = true;
	Site(TEXT("brush.spawn"), TEXT("CFuncIllusionary::Spawn"), 0x100bf760u, TEXT("return"),
		FString::Printf(TEXT("angles=%s m_MoveType=%d m_MoveCollide=%d m_nSolidType=%d m_NetworkChangeState=%d m_bChanged=%d model=%s"),
			*IllusionaryAng(LocalAnglesWord()), GetMoveType(), GetMoveCollide(), RetailSolidType, bNetworkChangeState ? 1 : 0,
			bNetworkChanged ? 1 : 0, Model.IsEmpty() ? TEXT("(empty)") : *Model));
}

void FElysiumFuncIllusionary::SetBrushModel(const FString& Name)
{
	// Slot 105, `CBaseEntity::SetModel` 0x100ad460 (`FElysiumEntity::SetModel`, L0-r021): the model-table
	// lookup and type (`VEngineServer014` slots 20 / 21), the non-brush `Msg`, `UTIL_SetModel`
	// `FUN_101cf4a0` (the index, the name and the collision bounds from a non-empty name; nothing for an
	// empty one), `+0x1b1 = 1`. Dispatched through the vtable, as `100bf7f0` does. The `brush.setmodel`
	// site states the body's outcome for this leaf's record.
	FString Arg = Name;
	SetModel(Arg.GetCharArray().GetData() != nullptr ? Arg.GetCharArray().GetData() : const_cast<TCHAR*>(TEXT("")));
	const int32 Index = World != nullptr ? World->ModelTableIndex(Name) : -1;
	const int32 Type = (World != nullptr && World->ModelTableHasModel(Index)) ? World->ModelTableType(Index) : 0;
	const bool bMsg = Type != FElysiumEntityWorld::ModelTypeBrush;
	Site(TEXT("brush.setmodel"), TEXT("CBaseEntity::SetModel"), 0x100ad460u, TEXT("write"),
		FString::Printf(TEXT("model=%s type=%d msg=%d set=%d m_bChanged=%d"), Name.IsEmpty() ? TEXT("(empty)") : *Name, Type, bMsg ? 1 : 0,
			Name.IsEmpty() ? 0 : 1, bNetworkChanged ? 1 : 0));
}
