#include "ElysiumCheatManager.h"

#include "ElysiumPlayerBody.h"

#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCheat, Log, All);

void UElysiumCheatManager::Noclip()
{
	APlayerController* PC = GetPlayerController();
	if (IElysiumPlayerBody* Body = PC ? Cast<IElysiumPlayerBody>(PC->GetPawn()) : nullptr)
	{
		Body->SetNoclip(!Body->IsNoclip());
		UE_LOG(LogElysiumCheat, Display, TEXT("noclip %s"), Body->IsNoclip() ? TEXT("ON") : TEXT("OFF"));
	}
}

void UElysiumCheatManager::ElysiumTeleport(float SrcX, float SrcY, float SrcZ)
{
	APlayerController* PC = GetPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}
	// Source (inches, right-handed) -> Unreal (cm, left-handed): (sx, -sy, sz) * 2.54. The inverse
	// of the load transform (matches the HUD's UnrealToSource readout).
	const FVector Dest(SrcX * 2.54, -SrcY * 2.54, SrcZ * 2.54);
	Pawn->TeleportTo(Dest, Pawn->GetActorRotation());
	UE_LOG(LogElysiumCheat, Display, TEXT("teleport -> src (%.0f, %.0f, %.0f)"), SrcX, SrcY, SrcZ);
}
