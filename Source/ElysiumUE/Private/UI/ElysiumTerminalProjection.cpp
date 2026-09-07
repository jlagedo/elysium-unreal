#include "UI/ElysiumTerminalProjection.h"

#include "ElysiumViewState.h"
#include "UI/ElysiumTerminalCells.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTerminalProjection, Log, All);

namespace
{
	const FName ScreenMaterialSlot(TEXT("screen"));
	const FName SlateUIParameter(TEXT("SlateUI"));
	const FName TintParameter(TEXT("TintColorAndOpacity"));
	const FName OpacityParameter(TEXT("OpacityFromTexture"));
	constexpr float SurfaceWidth = 1024.0f;
	constexpr float SurfaceHeight = 768.0f;
	const FVector2D SurfaceDrawSize(SurfaceWidth, SurfaceHeight);
	const FLinearColor ScreenBlack(0.004f, 0.009f, 0.007f, 1.0f);
	const FLinearColor ProjectionPhosphor(0.63f, 0.88f, 0.70f, 1.0f);
	const TCHAR* ProjectionMaterialPath =
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

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
		ProjectionMaterialPath);
	if (!BaseMaterial)
	{
		UE_LOG(LogElysiumTerminalProjection, Warning,
			TEXT("terminal projection failed for %s: material '%s' did not load"),
			*Owner.ToString(), ProjectionMaterialPath);
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
	ProjectionMaterial->SetTextureParameterValue(SlateUIParameter, RenderTarget);
	ProjectionMaterial->SetVectorParameterValue(TintParameter, FLinearColor::White);
	ProjectionMaterial->SetScalarParameterValue(OpacityParameter, 1.0f);
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
	// monitor destroyed mid-map would pin a 1024x768 surface until the map epoch retired, and the
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
	// The renderer holds the target; drop it first.
	WidgetRenderer.Reset();
	RenderTarget = nullptr;
	DrawnRevision = 0;
	DrawnDraft.Reset();
}

bool UElysiumTerminalProjection::NeedsRedraw(const FElysiumTerminalView& View,
	const FString& Draft) const
{
	return IsBound() && (View.Revision != DrawnRevision || Draft != DrawnDraft);
}

bool UElysiumTerminalProjection::IsBodyResident() const
{
	if (ForcedResidency.IsSet())
	{
		return ForcedResidency.GetValue();
	}
	// A monitor in another room, behind the player or culled has nothing to show, and rasterizing a
	// 1024x768 Slate surface per idle terminal per frame would be the whole cost of the feature.
	const UPrimitiveComponent* Body = BoundBody();
	return Body != nullptr && Body->WasRecentlyRendered();
}

void UElysiumTerminalProjection::Draw(const FElysiumTerminalView& View, const FString& Draft)
{
	DrawnRevision = View.Revision;
	DrawnDraft = Draft;
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
	// The COMPOSED grid: the authority's cells with the local draft put-charred over them from the
	// authority's cursor. That composition is what retail's client does — `FUN_100c6d50` collects
	// the keystroke into its own line and re-runs it through `FUN_100c8060` before the rasterizer
	// sees the buffer (`docs/vtmb/computer-terminals.md` §8.1, TERM13) — so the typed characters are
	// genuinely on the glass and not in a viewport overlay.
	//
	// Entity message type 9 (the CRACKING echo, `FUN_10217d60`) is a different thing and is already
	// in the authority's cells.
	//
	// Slice D replaces this text block with the cell painter at fixed metrics, the style bit, the
	// four palettes and the blinking block cursor. Until then the composed grid is drawn as rows of
	// monospace text, which is the same characters in the same places.
	const ElysiumTerminalCells::FElysiumTerminalComposed Composed =
		ElysiumTerminalCells::ComposeDraft(View, Draft);
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FSlateColor(ScreenBlack))
		.Padding(FMargin(28.0f, 24.0f))
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::Both)
			[
				SNew(SBox)
				.WidthOverride(SurfaceWidth - 56.0f)
				.HeightOverride(SurfaceHeight - 48.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Join(Composed.RowTexts(), TEXT("\n"))))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 18))
						.ColorAndOpacity(FSlateColor(ProjectionPhosphor))
						.Clipping(EWidgetClipping::ClipToBoundsAlways)
					]
				]
			]
		];
}
