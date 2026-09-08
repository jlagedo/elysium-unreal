// func_lod and func_areaportalwindow -- the two distance-culled brush classes.
//
// Neither class derives anything at runtime. A `func_lod`'s cull range is the producer's
// `cull_max_cm` (its `DisappearDist x 2.54`), which `FElysiumEntityWorld::BuildBrushBody` writes
// onto the brush visual the moment it is attached; the leaf only carries the authored keyvalue so
// the debug view can show what the row was derived from. A `func_areaportalwindow` is a point row
// on every corpus map: its `FadeStartDist`/`FadeDist` govern the black backing brush named by
// `target`, whose mesh the exporter omits by the ruling in `docs/vtmb/entity_io.md`, so it carries
// the numbers and the two names and writes nothing (the R7 owner call). Both take only the base
// inputs; a wire fired at either still reports through the base chain.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumClassFields.h"

class FElysiumFuncLod final : public FElysiumEntity
{
public:
	float DisappearDist = 0.0f;   // Source inches, the authored value; the row's `cull_max_cm` is its cm twin

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("DisappearDist"), FString::Printf(TEXT("%.0f in"), DisappearDist));
		Out.Emplace(TEXT("Cull (baked)"), FString::Printf(TEXT("%.1f cm"), Def ? Def->CullMaxCm : 0.f));
	}
};

class FElysiumFuncAreaPortalWindow final : public FElysiumEntity
{
public:
	float FadeStartDist = 0.0f;
	float FadeDist = 0.0f;
	float TranslucencyLimit = 0.0f;
	FString BackgroundBModel;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Fade"), FString::Printf(TEXT("%.0f -> %.0f in, limit %.2f"),
			FadeStartDist, FadeDist, TranslucencyLimit));
		Out.Emplace(TEXT("Backing / window"), FString::Printf(TEXT("%s / %s"),
			*Target, BackgroundBModel.IsEmpty() ? TEXT("(none)") : *BackgroundBModel));
	}
};

static TUniquePtr<FElysiumEntity> MakeFuncLod() { return MakeUnique<FElysiumFuncLod>(); }
static TUniquePtr<FElysiumEntity> MakeFuncAreaPortalWindow()
{
	return MakeUnique<FElysiumFuncAreaPortalWindow>();
}

static FElysiumClassRegistrar GRegFuncLod(
	TEXT("func_lod"), ElysiumBaseClassName(), &MakeFuncLod,
	[](FElysiumClassDesc& D)
	{
		ElysiumAddClassField(D, TEXT("DisappearDist"), &FElysiumFuncLod::DisappearDist, EElysiumField::None);
	});

static FElysiumClassRegistrar GRegFuncAreaPortalWindow(
	TEXT("func_areaportalwindow"), ElysiumBaseClassName(), &MakeFuncAreaPortalWindow,
	[](FElysiumClassDesc& D)
	{
		ElysiumAddClassField(D, TEXT("FadeStartDist"), &FElysiumFuncAreaPortalWindow::FadeStartDist, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("FadeDist"), &FElysiumFuncAreaPortalWindow::FadeDist, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("TranslucencyLimit"), &FElysiumFuncAreaPortalWindow::TranslucencyLimit, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("BackgroundBModel"), &FElysiumFuncAreaPortalWindow::BackgroundBModel, EElysiumField::None);
	});
