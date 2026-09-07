#include "UI/ElysiumTerminalProjection.h"

#include "ElysiumViewState.h"

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
	const FLinearColor Phosphor(0.63f, 0.88f, 0.70f, 1.0f);
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
	ProjectionTarget.Reset();
	ProjectionMaterialIndex = INDEX_NONE;
	ProjectionMaterial = nullptr;
	// The renderer holds the target; drop it first.
	WidgetRenderer.Reset();
	RenderTarget = nullptr;
	DrawnRevision = 0;
}

bool UElysiumTerminalProjection::NeedsRedraw(const FElysiumTerminalView& View) const
{
	return IsBound() && View.Revision != DrawnRevision;
}

void UElysiumTerminalProjection::Draw(const FElysiumTerminalView& View)
{
	DrawnRevision = View.Revision;
	++DrawCount;
	if (!WidgetRenderer.IsValid() || !RenderTarget)
	{
		return;   // bound with no renderer: the revision is consumed, nothing is rasterized
	}
	WidgetRenderer->DrawWidget(RenderTarget, BuildSurface(View), SurfaceDrawSize, 0.0f, false);
}

TSharedRef<SWidget> UElysiumTerminalProjection::BuildSurface(const FElysiumTerminalView& View) const
{
	// The authority's grid and nothing else. Retail has no client-side draft at all — the client
	// sends each keystroke and the SERVER echoes it into the cell buffer (type 9,
	// `docs/vtmb/computer-terminals.md` §8.1/§8.6) — so the glass showing only what the entity has
	// written is the retail semantic, not a gap. Slice D replaces this text block with the cell
	// painter at fixed metrics, the style bit and the block cursor.
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
						.Text(FText::FromString(FString::Join(View.ScreenRows, TEXT("\n"))))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 18))
						.ColorAndOpacity(FSlateColor(Phosphor))
						.Clipping(EWidgetClipping::ClipToBoundsAlways)
					]
				]
			]
		];
}
