#include "UI/ElysiumTerminalProjection.h"

#include "ElysiumViewState.h"
#include "UI/ElysiumTerminalScreenTuning.h"
#include "UI/SElysiumTerminalCells.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTerminalProjection, Log, All);

namespace
{
	const FName ScreenMaterialSlot(TEXT("screen"));
	// The authored material's own parameters.
	const FName ScreenParameter(TEXT("Screen"));
	const FName FlipUParameter(TEXT("FlipU"));
	const FName FlipVParameter(TEXT("FlipV"));
	const FName Rotate90Parameter(TEXT("Rotate90"));
	// The engine fallback's parameters, which are a different set entirely.
	const FName SlateUIParameter(TEXT("SlateUI"));
	const FName TintParameter(TEXT("TintColorAndOpacity"));
	const FName OpacityParameter(TEXT("OpacityFromTexture"));
	// The glass is SQUARE, at 2x retail. `FUN_100c77f0` (client.dll `0x100c77f0`) returns without
	// drawing unless its texture is 512x512 (`0x100c7806`/`0x100c7818` compare `0x200`), so every
	// VtMB monitor model's `screen` UVs were cut against a 512x512 sheet with a centred
	// `columns*14 x rows*16` text block in it. A 4:3 target made those UVs sample a sub-window of
	// the grid — the live `sp_tutorial_1` glass lost three rows off each end (owner QA,
	// 2026-09-07). `ElysiumTerminalPaint` lays the block out at retail's origin on this surface.
	constexpr float SurfaceWidth =
		static_cast<float>(ElysiumTerminalPaint::SurfaceExtentPx);
	constexpr float SurfaceHeight = SurfaceWidth;
	const FVector2D SurfaceDrawSize(SurfaceWidth, SurfaceHeight);
	const FLinearColor ScreenBlack(0.004f, 0.009f, 0.007f, 1.0f);
	// The project's own CRT material, authored in the editor: unlit, emissive from one texture
	// parameter, faint scanlines, a vignette and a slight UV curvature, plus the three orientation
	// switches the per-model tuning table feeds.
	const TCHAR* AuthoredMaterialPath =
		TEXT("/Game/ElysiumAuthored/UI/M_ElysiumTerminalScreen.M_ElysiumTerminalScreen");
	// The engine's flat blit. It shows the grid and nothing else — no emissive lift, no flip — so a
	// glass running on it is legible but wrong, which is exactly what the named warning says.
	const TCHAR* FallbackMaterialPath =
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque");
}

int32 UElysiumTerminalProjection::FindScreenMaterialSlot(const TArray<FName>& SlotNames)
{
	return SlotNames.IndexOfByKey(ScreenMaterialSlot);
}

bool UElysiumTerminalProjection::Bind(UPrimitiveComponent* InTarget)
{
	if (ProjectionTarget.Get() == InTarget && IsBound())
	{
		return true;
	}

	// Replacing a prior target intentionally leaves its last pixels/material in place: a monitor
	// whose body was rebuilt keeps showing whatever it last showed until the new one draws.
	Release();
	if (!InTarget)
	{
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection failed for %s: no physical use visual"), *Owner.ToString());
		return false;
	}

	const TArray<FName> SlotNames = InTarget->GetMaterialSlotNames();
	const int32 ListedIndex = FindScreenMaterialSlot(SlotNames);
	const int32 MaterialIndex = InTarget->GetMaterialIndex(ScreenMaterialSlot);
	if (ListedIndex == INDEX_NONE || MaterialIndex == INDEX_NONE)
	{
		const FString Available = FString::JoinBy(SlotNames, TEXT(", "),
			[](const FName& MaterialSlot) { return MaterialSlot.ToString(); });
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection failed for %s: component '%s' has no exact 'screen' material "
				"slot (available: %s)"),
			*Owner.ToString(), *InTarget->GetName(),
			Available.IsEmpty() ? TEXT("none") : *Available);
		return false;
	}

	ProjectionTarget = InTarget;
	ProjectionMaterialIndex = MaterialIndex;

	// The "no renderer" state, named and taken **before** anything is allocated. A commandlet or a
	// `-nullrhi` test still owns the slot and still consumes revisions, so the lifecycle above this
	// is the same one the game runs; only the rasterization is missing.
	if (!FApp::CanEverRender())
	{
		UE_LOG(LogElysiumTerminalProjection, Verbose,
			TEXT("terminal projection for %s bound '%s' slot %d with no renderer in this process"),
			*Owner.ToString(), *InTarget->GetName(), MaterialIndex);
		return true;
	}

	// The authored CRT material first. It is tracked repository content, so a miss is a missing
	// authored asset rather than a missing export — but it must not take the monitor down with it:
	// the engine pass-through still shows the grid, and the named warning is what says the picture
	// on the glass is the fallback's flat blit.
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, AuthoredMaterialPath);
	bAuthoredMaterial = BaseMaterial != nullptr;
	if (!BaseMaterial)
	{
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection for %s: authored material '%s' did not load; falling back to "
				"the engine pass-through — the glass will draw flat and unflipped"),
			*Owner.ToString(), AuthoredMaterialPath);
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
	}
	if (!BaseMaterial)
	{
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection failed for %s: material '%s' did not load"),
			*Owner.ToString(), FallbackMaterialPath);
		Release();
		return false;
	}

	WidgetRenderer = MakeUnique<FWidgetRenderer>(false, true);
	RenderTarget = FWidgetRenderer::CreateTargetFor(SurfaceDrawSize, TF_Bilinear, false);
	if (!RenderTarget)
	{
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection failed for %s: could not allocate %dx%d target"),
			*Owner.ToString(), static_cast<int32>(SurfaceWidth), static_cast<int32>(SurfaceHeight));
		Release();
		return false;
	}
	// Before anything can initialize the resource: an unset clear colour shows one white frame on
	// the glass the moment the material is installed.
	RenderTarget->ClearColor = ScreenBlack;

	// The physical component, not a transient input screen, owns the persistent MID. That is what
	// lets the screensaver stay on the monitor with no CommonUI screen anywhere in the world.
	ProjectionMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, InTarget);
	if (!ProjectionMaterial)
	{
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection failed for %s: could not create screen material"),
			*Owner.ToString());
		Release();
		return false;
	}
	// Remembered before the override so `Release` can put it back. Null is a legitimate answer (the
	// mesh's own default material), and `SetMaterial(index, nullptr)` restores exactly that.
	OriginalMaterial = InTarget->GetMaterial(MaterialIndex);
	if (bAuthoredMaterial)
	{
		ProjectionMaterial->SetTextureParameterValue(ScreenParameter, RenderTarget);
		// The authored UVs of a 2004 monitor's screen face run whichever way its author left them,
		// and only the model knows. The table answers "none" for every model nobody has calibrated.
		const FElysiumTerminalScreenTuningEntry Tuning =
			UElysiumTerminalScreenTuning::FindForBody(InTarget);
		ProjectionMaterial->SetScalarParameterValue(FlipUParameter, Tuning.bFlipU ? 1.0f : 0.0f);
		ProjectionMaterial->SetScalarParameterValue(FlipVParameter, Tuning.bFlipV ? 1.0f : 0.0f);
		ProjectionMaterial->SetScalarParameterValue(Rotate90Parameter,
			Tuning.bRotate90 ? 1.0f : 0.0f);
	}
	else
	{
		ProjectionMaterial->SetTextureParameterValue(SlateUIParameter, RenderTarget);
		ProjectionMaterial->SetVectorParameterValue(TintParameter, FLinearColor::White);
		ProjectionMaterial->SetScalarParameterValue(OpacityParameter, 1.0f);
	}
	InTarget->SetMaterial(MaterialIndex, ProjectionMaterial);
	return true;
}

bool UElysiumTerminalProjection::IsBound() const
{
	return ProjectionTarget.IsValid() && ProjectionMaterialIndex != INDEX_NONE;
}

void UElysiumTerminalProjection::Release()
{
	// Hand the slot back what it was authored with. Leaving the MID installed keeps the render
	// target referenced — the component holds the material, the material holds the texture — so a
	// monitor destroyed mid-map would pin a 1024x1024 surface until the map epoch retired, and the
	// dead body would still be showing the last frame of a terminal that no longer exists.
	if (UPrimitiveComponent* Target = ProjectionTarget.Get();
		Target && ProjectionMaterialIndex != INDEX_NONE && ProjectionMaterial)
	{
		Target->SetMaterial(ProjectionMaterialIndex, OriginalMaterial.Get());
	}
	OriginalMaterial.Reset();
	ProjectionTarget.Reset();
	ProjectionMaterialIndex = INDEX_NONE;
	ProjectionMaterial = nullptr;
	bAuthoredMaterial = false;
	// The renderer holds the target; drop it first.
	WidgetRenderer.Reset();
	RenderTarget = nullptr;
	DrawnRevision = 0;
	DrawnDraft.Reset();
	bDrawnBlinkLit = false;
}

void UElysiumTerminalProjection::SetCalibration(bool bInCalibration)
{
	if (bCalibration == bInCalibration)
	{
		return;
	}
	bCalibration = bInCalibration;
	// The pattern is not the authority's grid, so neither the revision nor the draft moved: force
	// the next pass to redraw by dropping what the last one consumed.
	DrawnRevision = 0;
	DrawnDraft.Reset();
}

bool UElysiumTerminalProjection::ShowsCursor(const FElysiumTerminalView& View)
{
	// The same three conditions `ElysiumTerminalPaint::BuildDrawPlan` applies, minus the blink
	// itself: an idle monitor, a closed line editor and acknowledge mode all draw no caret, and a
	// glass with no caret must not redraw twice a second forever.
	constexpr uint8 AcknowledgeMode = 2;
	return View.IsOpen() && View.bLineEditActive && View.InputMode != AcknowledgeMode;
}

bool UElysiumTerminalProjection::NeedsRedraw(const FElysiumTerminalView& View,
	const FString& Draft) const
{
	if (!IsBound())
	{
		return false;
	}
	if (View.Revision != DrawnRevision || Draft != DrawnDraft)
	{
		return true;
	}
	// The blink is the third half of the gate, and only while there is a caret to blink.
	return ShowsCursor(View)
		&& ElysiumTerminalPaint::BlinkLit(BlinkPhase) != bDrawnBlinkLit;
}

bool UElysiumTerminalProjection::IsBodyResident() const
{
	if (ForcedResidency.IsSet())
	{
		return ForcedResidency.GetValue();
	}
	// A monitor in another room, behind the player or culled has nothing to show, and rasterizing a
	// 1024x1024 Slate surface per idle terminal per frame would be the whole cost of the feature.
	const UPrimitiveComponent* Body = BoundBody();
	return Body != nullptr && Body->WasRecentlyRendered();
}

void UElysiumTerminalProjection::Draw(const FElysiumTerminalView& View, const FString& Draft)
{
	DrawnRevision = View.Revision;
	DrawnDraft = Draft;
	bDrawnBlinkLit = ElysiumTerminalPaint::BlinkLit(BlinkPhase);
	++DrawCount;
	if (!WidgetRenderer.IsValid() || !RenderTarget)
	{
		return;   // bound with no renderer: the revision is consumed, nothing is rasterized
	}
	WidgetRenderer->DrawWidget(RenderTarget, BuildSurface(View, Draft), SurfaceDrawSize, 0.0f, false);
}

TSharedRef<SWidget> UElysiumTerminalProjection::BuildSurface(const FElysiumTerminalView& View,
	const FString& Draft) const
{
	// The cell painter, at the render target's own metrics. It composes the draft onto the
	// authority's grid itself (`ElysiumTerminalPaint::BuildDrawPlan` -> `ComposeDraft`), which is
	// what retail's client does — `FUN_100c6d50` collects the keystroke into its own line and
	// re-runs it through `FUN_100c8060` before the rasterizer sees the buffer
	// (`docs/vtmb/computer-terminals.md` §8.1, TERM13) — so the typed characters are genuinely on
	// the glass and not in a viewport overlay.
	//
	// Entity message type 9 (the CRACKING echo, `FUN_10217d60`) is a different thing and is already
	// in the authority's cells.
	//
	// A fresh widget per draw rather than a retained one: the rasterization is on demand into a
	// render target, the tree is one leaf, and a retained widget would have to be kept in step with
	// a view that can be republished by any pass.
	const TSharedRef<SElysiumTerminalCells> Cells = SNew(SElysiumTerminalCells);
	Cells->SetView(View, Draft);
	Cells->SetBlinkPhase(BlinkPhase);
	Cells->SetCalibration(bCalibration);
	return Cells;
}
