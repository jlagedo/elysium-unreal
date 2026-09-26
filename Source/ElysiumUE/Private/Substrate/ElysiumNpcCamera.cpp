#include "Substrate/ElysiumNpcCamera.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"

const FElysiumNpcClass* FElysiumNpcCamera::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCamera"));
	return Row;
}

// Slots 497 / 506: `0x103681d0` / `0x103682f0`, one-byte `ret` bodies.
void FElysiumNpcCamera::Slot497()
{
	FUN_103681d0();
}

void FElysiumNpcCamera::Slot506()
{
	FUN_103682f0();
}

// Slot 420: `0x103692c0`.
void FElysiumNpcCamera::NPCInit()
{
	CameraNPCInit();
}

// Slot 422: `0x10369930`.
void FElysiumNpcCamera::StartNPC()
{
	CameraStartNPC();
}

// Slot 104: `0x103689c0`.
void FElysiumNpcCamera::Precache()
{
	CameraPrecache();
}

// Slot 463: `0x10368ea0`, an EMPTY body that does not chain: a camera's state change writes nothing,
// not even the base state-flag byte. Inherited by `CNPC_VCameraSecurity`.
void FElysiumNpcCamera::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	(void)OldState;
	(void)NewState;
}

// Slot 461: `0x10369060`, the ideal state HARDCODED to ALERT with no test (inherited by `CNPC_VCameraSecurity`).
int32 FElysiumNpcCamera::SelectIdealStateRetail()
{
	return SpeciesIdealStateRetail(EIdealStateSpecies::AlwaysAlert);
}

// Slot 437: `0x10368f20`.
int32 FElysiumNpcCamera::PreSelectSchedule()
{
	return CameraPreSelectSchedule();
}

// Slot 438: `0x10368f40`, which replaces the whole selector (the Troika selector's species hook).
int32 FElysiumNpcCamera::SpeciesSelectSchedule()
{
	return CameraSelectSchedule();
}

// Slot 460: `0x10368f80`, a replacement that does not chain (inherited by `CNPC_VCameraSecurity`).
int32 FElysiumNpcCamera::PreSelectIdealStateRetail()
{
	return CameraPreSelectIdealState();
}

// Slot 337: `0x10368e80`.
int32 FElysiumNpcCamera::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x10368e80"));
}

// Slot 546: `0x10368550`, the class's own schedule id space.
const TCHAR* FElysiumNpcCamera::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VCamera"), SlotEn);
}

// Slot 545: `0x10369bd0`, a replacement that does not chain (inherited by `CNPC_VCameraSecurity`).
bool FElysiumNpcCamera::InitSquad()
{
	return CameraInitSquad();
}

// Slot 259: `0x10368ec0`, an empty replacement (inherited by `CNPC_VCameraSecurity`).
bool FElysiumNpcCamera::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return CameraHandleAnimEvent(Event);
}

// Slot 563: `0x10368ee0`, the `Empty` shape; a replacement that does not chain.
void FElysiumNpcCamera::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::Empty, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 434: `0x10369100`, an empty replacement (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::PrescheduleThink()
{
}

// Slot 488: `0x103680b0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::DeathSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 488);
}

// Slot 489: `0x103680d0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::AlertSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 489);
}

// Slot 490: `0x103680f0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::IdleSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 490);
}

// Slot 491: `0x10368110`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::PainSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 491);
}

// Slot 492: `0x10368130`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FearSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 492);
}

// Slot 493: `0x10368150`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::LostEnemySound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 493);
}

// Slot 494: `0x10368170`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FoundEnemySound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 494);
}

// Slot 495: `0x10368190`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::SurprisedSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 495);
}

// Slot 496: `0x103681b0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::TargetAcquiredSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 496);
}

// Slot 498: `0x103681f0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FleeSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 498);
}

// Slot 499: `0x10368210`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::IdleAgitatedSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 499);
}

// Slot 500: `0x10368230`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::ExertHvySound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 500);
}

// Slot 501: `0x10368250`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::ExertLightSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 501);
}

// Slot 502: `0x10368270`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::RiledSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 502);
}

// Slot 503: `0x10368290`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::ComfortSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 503);
}

// Slot 504: `0x103682b0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::UpsetSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 504);
}

// Slot 505: `0x103682d0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::TargetGiveUpSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 505);
}

// Slot 507: `0x10368310`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FloatSound()
{
	SpeciesVocalize(TEXT("CNPC_VCamera"), 507);
}

// Slot 508: `0x10368330`, three bytes that pop the argument — the camera never speaks a sentence.
void FElysiumNpcCamera::SpeakSentence(int32 SentenceIndex)
{
	(void)SentenceIndex;
	SpeciesVocalize(TEXT("CNPC_VCamera"), 508);
}
