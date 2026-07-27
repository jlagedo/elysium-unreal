#include "Debug/ElysiumScreenshot.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"

namespace
{
	// One in-flight capture. The engine broadcasts OnScreenshotCaptured once, at the end of the
	// frame it services the request on; the ticker is only the timeout escape hatch. Whichever
	// fires first calls the user callback and tears the other one down, so the callback contract
	// (exactly once) holds even if the viewport never presents.
	struct FElysiumPendingCapture : TSharedFromThis<FElysiumPendingCapture>
	{
		ElysiumScreenshot::FOnCaptured Callback;
		FDelegateHandle CapturedHandle;
		FTSTicker::FDelegateHandle TickerHandle;
		int32 FramesLeft = 0;
		bool bFired = false;

		// Keeps itself alive between the request and the callback; released in Finish.
		TSharedPtr<FElysiumPendingCapture> SelfRef;

		void Finish(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
		{
			if (bFired)
			{
				return;
			}
			bFired = true;

			if (CapturedHandle.IsValid() && UGameViewportClient::OnScreenshotCaptured().Remove(CapturedHandle))
			{
				CapturedHandle.Reset();
			}
			if (TickerHandle.IsValid())
			{
				FTSTicker::RemoveTicker(TickerHandle);
				TickerHandle.Reset();
			}

			const ElysiumScreenshot::FOnCaptured Local = Callback;
			// Drop the self-reference last: Local holds everything the callback needs, so this
			// object may die inside it.
			TSharedPtr<FElysiumPendingCapture> KeepAlive = MoveTemp(SelfRef);
			if (Local)
			{
				Local(Width, Height, Bitmap);
			}
		}
	};
}

bool ElysiumScreenshot::Request(const FOnCaptured& OnCaptured, int32 TimeoutFrames, bool bShowUI)
{
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		return false;
	}

	TSharedPtr<FElysiumPendingCapture> Pending = MakeShared<FElysiumPendingCapture>();
	Pending->Callback = OnCaptured;
	Pending->FramesLeft = FMath::Max(1, TimeoutFrames);
	Pending->SelfRef = Pending;

	TWeakPtr<FElysiumPendingCapture> Weak = Pending;

	Pending->CapturedHandle = UGameViewportClient::OnScreenshotCaptured().AddLambda(
		[Weak](int32 Width, int32 Height, const TArray<FColor>& Bitmap)
		{
			if (TSharedPtr<FElysiumPendingCapture> Strong = Weak.Pin())
			{
				// The back buffer's alpha channel is not meaningful; force it opaque so the PNG
				// does not read as fully transparent.
				TArray<FColor> Opaque = Bitmap;
				for (FColor& C : Opaque)
				{
					C.A = 255;
				}
				Strong->Finish(Width, Height, Opaque);
			}
		});

	Pending->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("ElysiumScreenshot"), 0.0f,
		[Weak](float)
		{
			TSharedPtr<FElysiumPendingCapture> Strong = Weak.Pin();
			if (!Strong.IsValid())
			{
				return false;
			}
			if (--Strong->FramesLeft > 0)
			{
				return true;
			}
			Strong->Finish(0, 0, TArray<FColor>());
			return false;
		});

	// bShowUI false keeps the debug/ImGui overlay out, so a shot taken with a Cog window open still
	// matches one taken without it — but it drops **all** Slate, game UI included. The regression
	// harness wants that; a caller looking at what the player sees does not.
	FScreenshotRequest::RequestScreenshot(bShowUI);
	return true;
}

bool ElysiumScreenshot::EncodePng(int32 Width, int32 Height, const TArray<FColor>& Bitmap, TArray64<uint8>& OutPng)
{
	OutPng.Reset();
	if (Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height)
	{
		return false;
	}

	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid())
	{
		return false;
	}
	if (!Wrapper->SetRaw(Bitmap.GetData(), static_cast<int64>(Width) * Height * sizeof(FColor),
		Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	OutPng = Wrapper->GetCompressed(100);
	return OutPng.Num() > 0;
}

bool ElysiumScreenshot::SavePng(int32 Width, int32 Height, const TArray<FColor>& Bitmap, const FString& AbsolutePath)
{
	TArray64<uint8> Png;
	if (!EncodePng(Width, Height, Bitmap, Png))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), /*Tree*/ true);
	return FFileHelper::SaveArrayToFile(Png, *AbsolutePath);
}
