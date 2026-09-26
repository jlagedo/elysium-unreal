#include "Substrate/ElysiumNpcPayphone.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcPayphone::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CPayphone"));
	return Row;
}

// Slot 420: `0x101aab90`.
void FElysiumNpcPayphone::NPCInit()
{
	PayphoneNPCInit();
}

// Slot 370: `0x101aa7f0`, a tail call through slot 368 — the head aim IS the body direction. Slot 368
// (`FElysiumCombatCharacter::BodyDirection2D`) has no override on any port class, so the direct call
// resolves exactly as retail's virtual one does.
FVector FElysiumNpcPayphone::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x101aa820`, a tail call through slot 369.
FVector FElysiumNpcPayphone::HeadDirection3D()
{
	return BodyDirection3D();
}

// Slot 193: `0x101aae60`, whose miss calls the Troika-line body `0x100b4b40` directly.
FVector FElysiumNpcPayphone::EyePosition() const
{
	return PayphoneEyePosition();
}

// Slot 295: `0x101aaee0`, a replacement that does not chain.
bool FElysiumNpcPayphone::CanTalk(FElysiumEntity* Activator)
{
	return PayphoneCanTalk(Activator);
}

// Slot 431: `0x101aabf0` replaces the Troika line's `NPCThink` (`0x10292de0`), which the port's
// `Think` carries; it never calls it.
void FElysiumNpcPayphone::Think()
{
	PayphoneThinkPass();
}
