#include "Substrate/ElysiumNpcAnimal.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

const FElysiumNpcClass* FElysiumNpcAnimal::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 482: `0x1035fd40`, a standalone copy that keeps a SCRIPT-state body's answer.
/** `0x1035fd40` / `0x103bd270` — `CNPC_VAnimal`'s and `CNPC_VTzimisce`'s slot 482: standalone
 *  copies that never call the base and keep a SCRIPT-state body's answer. */
int32 FElysiumNpcAnimal::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	// `0x1035fd40`, `CNPC_VAnimal`'s slot 482 (inherited by `CNPC_VDog`, `CNPC_VScurrying`,
	// `CNPC_VRat` and `CNPC_VZombie`). A standalone copy: it calls `0x101a8ac0` directly and slot 158
	// `IsAlive` virtually, and **never** the base `CAI_BaseNPC::CanPlaySequence` `0x10278090`. Its head
	// is the base's, but its state gate ends `SETNZ CL / DEC ECX / AND ECX,EDI` (`0x1035fdf9`), so a
	// body in retail state 4 (SCRIPT) keeps its 1-or-2 where the base answers 0. That is family
	// Bosses' `CanPlaySequenceSpecies`, the tail the four species copies share.
	//
	// Story 5 step 3 corrected this body: it used to call the base and so refused in SCRIPT state
	// (`decisions-step3.json` `retail_corrections`).
	return CanPlaySequenceSpecies(bDisregardState, InterruptLevel);
}

// Slot 461: `0x1035fe80`, chaining the Troika body directly.
int32 FElysiumNpcAnimal::SelectIdealStateRetail()
{
	return AnimalSelectIdealState();
}

// Slot 546: `0x1035edb0`, the class's own schedule id space.
const TCHAR* FElysiumNpcAnimal::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a4f4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VAnimal"), TEXT("0x1035edb0"), TEXT("0x1093a4f4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 563: `0x1035f5c0`, the `OffsetOnly` shape; a replacement that does not chain.
void FElysiumNpcAnimal::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::OffsetOnly, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}
