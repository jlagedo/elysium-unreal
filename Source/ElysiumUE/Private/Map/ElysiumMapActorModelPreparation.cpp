#include "ElysiumMapActor.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Map/ElysiumMapLog.h"
#include "Visual/ElysiumCharacterModel.h"
#include "Visual/ElysiumNativeAnimationData.h"
#include "Engine/GameInstance.h"

EElysiumCharacterModelAdmission AElysiumMapActor::RequestCharacterModel(
	const FElysiumEntityHandle& Entity, const FString& ModelId, uint64 Generation, FString& OutError)
{
	OutError.Reset();
	CancelCharacterModel(Entity);
	FElysiumEntity* Live = EntityWorld ? EntityWorld->Resolve(Entity) : nullptr;
	if (!Live || Live->IsDead() || !Live->AsCombatCharacter() || Entity.Epoch != EntityWorld->GetEpoch()
		|| !ElysiumCharacterModel::IsCanonicalId(ModelId) || ElysiumCharacterModel::IdFromSource(Live->Model) != ModelId
		|| bMotorsRetired || RuntimePhase == EElysiumMapRuntimePhase::Failed)
	{ OutError = TEXT("character admission has no current live entity/model in this map epoch"); return EElysiumCharacterModelAdmission::Rejected; }
	auto* Native = GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>() : nullptr;
	if (!Native) { OutError = TEXT("native animation service is unavailable"); return EElysiumCharacterModelAdmission::Rejected; }
	if (!Native->OwnsPreparationEpoch(MapEpoch))
	{ OutError = TEXT("native model preparation belongs to a different map epoch"); return EElysiumCharacterModelAdmission::Rejected; }
	if (Native->IsModelReady(ModelId)) return EElysiumCharacterModelAdmission::Ready;
	const auto Ticket = CharacterModelRequests.Begin(Entity, ModelId, Generation);
	const TWeakObjectPtr<AElysiumMapActor> WeakThis(this);
	const uint64 RequestId = Native->AdmitModelAsync(ModelId, MapEpoch,
		[WeakThis, Ticket](bool bSuccess, const FString& Error)
		{
			if (auto* Map = WeakThis.Get()) Map->CompleteCharacterModel(Ticket, bSuccess, Error);
		}, OutError);
	if (!RequestId)
	{ CharacterModelRequests.Remove(Ticket); return EElysiumCharacterModelAdmission::Rejected; }
	CharacterNativeAdmissionIds.Add(Entity, RequestId);
	return EElysiumCharacterModelAdmission::Pending;
}

void AElysiumMapActor::CancelCharacterModel(const FElysiumEntityHandle& Entity)
{
	CharacterModelRequests.Cancel(Entity);
	uint64 RequestId = 0;
	if (CharacterNativeAdmissionIds.RemoveAndCopyValue(Entity, RequestId))
		if (auto* Game = GetGameInstance())
			if (auto* Native = Game->GetSubsystem<UElysiumNativeAnimationData>()) Native->CancelModelAdmission(RequestId);
}

void AElysiumMapActor::CancelCharacterModelAdmissions()
{
	TArray<FElysiumEntityHandle> Entities; CharacterNativeAdmissionIds.GetKeys(Entities);
	for (const auto& Entity : Entities) CancelCharacterModel(Entity);
	CharacterModelRequests.Reset();
}

void AElysiumMapActor::CompleteCharacterModel(const FElysiumCharacterModelTicket& Ticket, bool bSuccess, const FString& Error)
{
	FElysiumEntity* Live = EntityWorld ? EntityWorld->Resolve(Ticket.Entity) : nullptr;
	const bool bCurrent = EntityWorld && Live && !bMotorsRetired && RuntimePhase != EElysiumMapRuntimePhase::Failed
		&& CharacterModelRequests.IsCurrent(Ticket, EntityWorld->GetEpoch(), Live->Handle,
			ElysiumCharacterModel::IdFromSource(Live->Model), !Live->IsDead());
	// Never let an old completion erase the native token of a replacement request.
	if (CharacterModelRequests.Remove(Ticket)) CharacterNativeAdmissionIds.Remove(Ticket.Entity);
	if (!bCurrent) return;
	if (!bSuccess)
	{
		if (RuntimePhase != EElysiumMapRuntimePhase::Active) bNativeAnimationPreloadFailed = true;
		UE_LOG(LogElysium, Warning, TEXT("character model admission %s (generation %llu) failed: %s"),
			*Ticket.ModelId, Ticket.Generation, *Error);
		return;
	}
	if (auto* Character = Live->AsCombatCharacter())
	{
		Character->CompletePreparedCharacterVisual(Ticket.Generation, Ticket.ModelId);
		if (!Character->GetSkeletalBody() && RuntimePhase != EElysiumMapRuntimePhase::Active)
			bNativeAnimationPreloadFailed = true;
	}
}
