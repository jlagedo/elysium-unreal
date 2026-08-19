// The prop family's registration site: the `prop_dynamic` (+ `prop_dynamic_ornament`) base, the
// model-bearing button, switch and sign leaves, and the physical prop/constraint leaves.
//
// The classes themselves live one to a file beside this one — `ElysiumProp` (the prop_dynamic
// base), `ElysiumPropLeaves` (button/switch/sign) and `ElysiumPhysProp` (prop_physics +
// phys_hinge). What remains here is the registry wiring: the factories, the input/field tables
// the class chain exposes, and the one static registrar that installs them. The terminal leaf
// lives with the shared skill-attempt substrate instead.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumProp.h"
#include "Substrate/ElysiumPropLeaves.h"

namespace
{
	// The `skin` field, whose setter repaints the body rather than only storing the number. VtMB
	// makes no distinction: `skin` is one datamap record flagged both KEY and INPUT with a null
	// inputFunc, so the keyvalue, the `Skin` wire and a script's `.skin =` all land in the same
	// direct write. Templated over the leaf because both prop leaves own their own Skin member.
	template <typename TClass>
	void AddPropSkinField(FElysiumClassDesc& D)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).Skin); };
		Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).SetSkin(V.ToInt()); };
		D.Fields.Add(FName(TEXT("skin")), MoveTemp(Acc));
	}
}

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeProp() { return MakeUnique<FElysiumProp>(); }
static TUniquePtr<FElysiumEntity> MakePropButton() { return MakeUnique<FElysiumPropButton>(); }
static TUniquePtr<FElysiumEntity> MakePropSwitch() { return MakeUnique<FElysiumPropSwitch>(); }
static TUniquePtr<FElysiumEntity> MakePropSign() { return MakeUnique<FElysiumPropSign>(); }
static TUniquePtr<FElysiumEntity> MakePhysProp() { return MakeUnique<FElysiumPhysProp>(); }
static TUniquePtr<FElysiumEntity> MakePhysHinge() { return MakeUnique<FElysiumPhysHinge>(); }

static void BuildPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumProp&>(E).InputSetAnimation(Args); });

	// `skin` keyfield / script `.skin`: a write repaints the body, mirroring VtMB, where the input
	// and the keyfield are the *same* datamap record and both just write m_nSkin.
	AddPropSkinField<FElysiumProp>(D);

	// CDynamicProp's own animation keyfields. `demo_sequence` is deliberately absent: it is a
	// Hammer/FGD field the engine never reads.
	ElysiumAddClassField(D, TEXT("LoopSequence"), &FElysiumProp::LoopSequence);
	ElysiumAddClassField(D, TEXT("RandomAnimation"), &FElysiumProp::bRandomAnimator);
	ElysiumAddClassField(D, TEXT("MinAnimTime"), &FElysiumProp::MinAnimTime);
	ElysiumAddClassField(D, TEXT("MaxAnimTime"), &FElysiumProp::MaxAnimTime);
}

// Model-bearing leaves that only need the common body/skin surface use this descriptor helper.
// Specialized leaves add their own I/O without inheriting prop_dynamic inputs they do not own.
static void BuildPropBodyClass(FElysiumClassDesc& D)
{
	AddPropSkinField<FElysiumProp>(D);
}

static void BuildPropButtonClass(FElysiumClassDesc& D)
{
	BuildPropBodyClass(D);
	D.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropButton&>(E).Use(A.Activator); });
	D.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropButton&>(E).InputLock(); });
	D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropButton&>(E).InputUnlock(); });
	D.Input(TEXT("ToggleLock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropButton&>(E).InputToggleLock(); });
	D.Input(TEXT("SetState"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropButton&>(E).InputSetState(A.Param.ToInt(), A.Activator); });
	ElysiumAddClassField(D, TEXT("locked"), &FElysiumPropButton::bLocked);
	ElysiumAddClassField(D, TEXT("current_state"), &FElysiumPropButton::CurrentState);
	ElysiumAddClassField(D, TEXT("max_states"), &FElysiumPropButton::MaxStates);
}

static void BuildPropSwitchClass(FElysiumClassDesc& D)
{
	BuildPropBodyClass(D);
	D.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropSwitch&>(E).Use(A.Activator); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropSwitch&>(E).InputToggle(A.Activator); });
	D.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropSwitch&>(E).InputLock(); });
	D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
		{ static_cast<FElysiumPropSwitch&>(E).InputUnlock(); });
	D.Input(TEXT("Activate"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropSwitch&>(E).InputActivate(A.Activator); });
	D.Input(TEXT("Deactivate"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ static_cast<FElysiumPropSwitch&>(E).InputDeactivate(A.Activator); });
	ElysiumAddClassField(D, TEXT("linkedswitch"), &FElysiumPropSwitch::LinkedSwitchName);
	ElysiumAddClassField(D, TEXT("reset_state"), &FElysiumPropSwitch::ResetState);
}

static void BuildPropSignClass(FElysiumClassDesc& D)
{
	BuildPropBodyClass(D);
	ElysiumAddClassField(D, TEXT("definition_file"), &FElysiumPropSign::DefinitionFile);
}

// prop_physics: the RE'd CPhysicsProp/CBreakableProp input surface. Wake + Break are real;
// the skin inputs are stubs (skin 0 only exported); no EnableMotion/DisableMotion/Sleep exist in VtMB.
static void BuildPhysPropClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Wake"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputWake(Args); });
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputBreak(Args); });
	D.Input(TEXT("Skin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkin(Args); });
	D.Input(TEXT("SetSkin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSkin(Args); });
	D.Input(TEXT("FadeToSkin"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputFadeToSkin(Args); });
	D.Input(TEXT("SetSkinFadeTime"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysProp&>(E).InputSetSkinFadeTime(Args); });

	AddPropSkinField<FElysiumPhysProp>(D);
}

// phys_hinge: the RE'd CPhysHinge/CPhysConstraint input surface.
static void BuildPhysHingeClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputTurnOn(Args); });
	D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputTurnOff(Args); });
	D.Input(TEXT("Break"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumPhysHinge&>(E).InputBreak(Args); });
}

// One shared leaf per classname (class-for-class registration, so the registry's exact case-folded
// Find resolves each).
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
		BuildPropButtonClass(Reg.Register(
			FName(TEXT("prop_button")), ElysiumBaseClassName(), &MakePropButton));
		BuildPropSwitchClass(Reg.Register(
			FName(TEXT("prop_switch")), ElysiumBaseClassName(), &MakePropSwitch));
		BuildPropSignClass(Reg.Register(
			FName(TEXT("prop_sign")), ElysiumBaseClassName(), &MakePropSign));
		BuildPhysPropClass(Reg.Register(FName(TEXT("prop_physics")), ElysiumBaseClassName(), &MakePhysProp));
		BuildPhysHingeClass(Reg.Register(FName(TEXT("phys_hinge")), ElysiumBaseClassName(), &MakePhysHinge));
	}
};

static FElysiumPropRegistrar GElysiumPropRegistrar;
