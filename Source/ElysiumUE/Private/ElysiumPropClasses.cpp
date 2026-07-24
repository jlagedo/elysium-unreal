// 8.3 — Dynamic props: the `prop_dynamic` (+ `prop_dynamic_ornament`) static-mesh leaf.
//
// Closes the "bodiless prop" gap (roadmap 9.3): a prop_dynamic record that parsed as an inert entity
// now stands its decoded static `.mdl` (out/<map>/props/<stem>.obj via AElysiumMapActor::BuildPropVisual,
// the shared prop decode 8.1 exports) at its placement, and gains per-instance addressability — the base
// ScriptHide/ScriptUnhide dormancy, the 9.3 SetOrigin/SetAngles/SetModel writers (body-follow), and the
// prop_dynamic inputs. `Break` hides the body and fires OnBreak. `Skin`/`SetAnimation` are logged stubs:
// the prop decode is LOD0 static geometry, skin 0 only — no alternate skin families or skeleton are
// exported — so faithfully they can only record the request (roadmap 8.3 / decisions.md).
//
// Deliberately out of scope: prop_physics Chaos bodies + constraints (8.4), the interactive
// prop_button/prop_sign/prop_switch/… `+use` family (4.10/8.8), and collision — a prop stands non-solid
// here (visual parity first; entity_visuals R2 "collision optional for visuals").

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"

#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumProp, Log, All);

// A/B toggle for the dynamic-prop bodies (mirrors elysium.NpcBodies). Read in the leaf's Spawn, so it
// takes effect on the next map load: 1 stands the meshes, 0 leaves the props bodiless records (their
// I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarPropBodies(
	TEXT("elysium.PropBodies"),
	1,
	TEXT("Stand dynamic-prop static-mesh bodies at their placements at map load (1, default) or skip them (0)."),
	ECVF_Default);

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddNpcField — file-unique name so all of them can land in one
	// unity blob. Only the int case this leaf needs.
	template <typename TClass, typename TMember>
	void AddPropField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, bool bKeyable = true)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		static_assert(std::is_same_v<TMember, int32>, "AddPropField: only int32 members are used here");
		FElysiumFieldAccessor Acc;
		Acc.bKeyable = bKeyable;
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// FElysiumProp — the AI-free static-mesh leaf shared by prop_dynamic / prop_dynamic_ornament. It
// stands a decoded prop mesh at its placement and exposes the dynamic-prop inputs over it.
// ============================================================================================

class FElysiumProp final : public FElysiumEntity
{
public:
	// The standing static-mesh body, or null (elysium.PropBodies 0, a decode that failed, or a record
	// with no model_mesh annotation). Owned by the map actor; the world tears it down. This leaf only
	// gates its visibility / moves it.
	UStaticMeshComponent* Visual = nullptr;
	bool  bBroken = false;   // Break fired — the body is hidden and stays down
	int32 Skin = 0;          // `skin` keyfield / Skin input (recorded; no alternate skins exported)

	virtual void Spawn() override
	{
		// Keyfields (model/angles/skin) are already applied. Stand the body from the export annotation.
		BuildBody(/*bFromSetModel=*/false);
	}

	// Mirror the whole-entity dormancy switch onto the body (R6): a ScriptHidden/dead prop is undrawn.
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		GateVisual();
	}

	// SetOrigin/SetAngles: the body is Movable, so follow it. Authored placement is a full 3-axis quat
	// (the exporter's model_quat); a runtime re-face carries no pre-converted form, so it collapses to
	// yaw-only, negated by the Source->Unreal Y reflection — matching the NPC leaf (cosmetic in practice).
	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Visual)
		{
			Visual->SetRelativeLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)));
		}
	}

	// SetModel: tear the old body down and rebuild from the new model at the same placement.
	virtual void OnRuntimeModelChanged() override
	{
		if (Visual)
		{
			Visual->DestroyComponent();
			Visual = nullptr;
		}
		BuildBody(/*bFromSetModel=*/true);
	}

	// Break: hide the prop and fire OnBreak (Source spawns gibs here; 8.3 has no gib system, so the
	// body simply goes down). Idempotent.
	void InputBreak(const FElysiumInputArgs& Args)
	{
		if (bBroken)
		{
			return;
		}
		bBroken = true;
		GateVisual();
		static const FName OnBreak(TEXT("OnBreak"));
		FireOutput(OnBreak, Args.Activator);
		UE_LOG(LogElysiumProp, Verbose, TEXT("%s Break"), *DebugString());
	}

	// Skin: faithfully selects an alternate skin family. The prop decode is skin 0 only (no alternate
	// texture sets exported), so this records the request and logs (roadmap 8.3 deferral).
	void InputSkin(const FElysiumInputArgs& Args)
	{
		Skin = Args.Param.ToInt();
		UE_LOG(LogElysiumProp, Log, TEXT("%s Skin %d — no alternate skin families exported (8.3 stub)"),
			*DebugString(), Skin);
	}

	// SetAnimation: prop_dynamic can be skeletal in Source; the decode here is LOD0 static geometry (no
	// skeleton), so animation is a no-op stub (roadmap 8.3 deferral — skeletal props are a follow-up).
	void InputSetAnimation(const FElysiumInputArgs& Args)
	{
		UE_LOG(LogElysiumProp, Log, TEXT("%s SetAnimation '%s' — prop is static geometry (8.3 stub)"),
			*DebugString(), *Args.Param.ToString());
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("static mesh") : TEXT("(none)"));
		Out.Emplace(TEXT("Broken"), bBroken ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Skin"), FString::FromInt(Skin));
	}

private:
	// Stand (or restand) the body. At spawn the exporter's model_mesh annotation names the decoded OBJ
	// stem exactly (a plain basename may not reproduce the sanitised stem) and model_quat the placement
	// rotation; a runtime SetModel has neither, so it derives the stem from the model path's basename
	// (matching the NPC SetModel path) and keeps the current yaw.
	void BuildBody(bool bFromSetModel)
	{
		if (CVarPropBodies.GetValueOnGameThread() == 0 || !World || !Def)
		{
			return;   // gated off, no world, or bare test world (no map actor to build on)
		}
		AElysiumMapActor* Map = Cast<AElysiumMapActor>(World->GetOwnerActor());
		if (!Map)
		{
			return;
		}

		FString Stem;
		FVector Loc;
		FQuat   Rot;
		if (bFromSetModel)
		{
			if (Model.IsEmpty())
			{
				return;   // now modelless
			}
			Stem = FPaths::GetBaseFilename(Model).ToLower();
			Loc  = Origin;
			Rot  = FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
		}
		else
		{
			if (Def->ModelMesh.IsEmpty())
			{
				return;   // no decoded prop mesh (bodiless record / decode failed) — stay a data-only entity
			}
			Stem = Def->ModelMesh;
			Loc  = Def->Origin;
			Rot  = Def->ModelQuat;
		}

		Visual = Map->BuildPropVisual(Stem, Loc, Rot);
		if (Visual)
		{
			World->RegisterPropBody(Visual);
			if (IsInert() || bBroken)
			{
				GateVisual();   // born hidden (start_hidden / a Spawn()-time Kill) or already broken
			}
		}
	}

	void GateVisual()
	{
		if (Visual)
		{
			Visual->SetVisibility(!IsInert() && !bBroken);
		}
	}
};

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeProp() { return MakeUnique<FElysiumProp>(); }

static void BuildPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSetAnimation(Args); });

	// `skin` keyfield / script `.skin`: registered read/write so the initial skin applies and a script
	// read/write resolves to a real field instead of the instance __dict__ (closes the 9.3 `.skin` note).
	AddPropField(D, TEXT("skin"), &FElysiumProp::Skin);
}

// One shared leaf per dynamic-prop classname (class-for-class registration, so the registry's exact
// case-folded Find resolves each). prop_physics (8.4) and the +use prop_* family (4.10/8.8) are not here.
struct FElysiumPropRegistrar
{
	FElysiumPropRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		static const TCHAR* const PropClasses[] = {
			TEXT("prop_dynamic"), TEXT("prop_dynamic_ornament"),
		};
		for (const TCHAR* Name : PropClasses)
		{
			BuildPropClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeProp));
		}
	}
};

static FElysiumPropRegistrar GElysiumPropRegistrar;
