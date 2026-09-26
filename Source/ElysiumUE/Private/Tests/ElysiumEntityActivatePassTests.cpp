#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Tests/ElysiumTestServices.h"

// The map's activation pass against retail's `ServerActivate` (`0x1011aaf0`):
//
//   for (e = NextEnt(NULL); e; e = NextEnt(e)) if (!(e->m_iEFlags & EFL_DORMANT)) e->Activate();
//
// `NextEnt` (`0x100f7060`) re-reads each node's next link and `CBaseEntityList::AddEntityAtSlot`
// (`0x100f9fc0`) appends a new entity at the tail, so what an earlier `Activate` creates is activated
// later in the same pass. The only skip is `EFL_DORMANT` (`0x100a8220`); `EFL_KILLME` is never
// tested, so an entity removed during the pass is still activated. Removals made before the pass are
// purged from retail's list; the port keeps their slots and skips them.
// (`docs/vtmb/npc-ai/lifecycle.md`, 0019 story 5 step 2 review.)

static constexpr EAutomationTestFlags GElysiumActivatePassFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The order the probes' `Activate` ran in, by targetname.
	TArray<FString> GActivatePassOrder;

	// Suite-local probe class. Its `Activate` records itself, then performs what its keys ask: kill
	// the entity named by `kill`, and create one more probe named by `spawn`.
	class FElysiumActivatePassProbe : public FElysiumEntity
	{
	public:
		virtual void Activate() override
		{
			GActivatePassOrder.Add(Def != nullptr ? Def->TargetName : FString());
			if (World == nullptr || Def == nullptr)
			{
				return;
			}
			if (const FString* Victim = Def->Keys.Find(TEXT("kill")))
			{
				if (FElysiumEntity* VictimEntity = World->FindByName(*Victim))
				{
					VictimEntity->Kill();
				}
			}
			if (const FString* Spawned = Def->Keys.Find(TEXT("spawn")))
			{
				FElysiumEntityDef Child;
				Child.Classname = TEXT("elysium_test_activate_probe");
				Child.TargetName = *Spawned;
				World->SpawnRuntimeEntity(MoveTemp(Child));
			}
		}
	};

	TUniquePtr<FElysiumEntity> MakeActivatePassProbe()
	{
		return MakeUnique<FElysiumActivatePassProbe>();
	}

	FElysiumClassRegistrar GRegActivatePassProbe(TEXT("elysium_test_activate_probe"),
		ElysiumBaseClassName(), &MakeActivatePassProbe, [](FElysiumClassDesc&) {});

	FElysiumEntityDef& AddActivatePassProbe(FElysiumEntityDefs& Defs, const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("elysium_test_activate_probe");
		Def.TargetName = Name;
		Defs.Defs.Add(MoveTemp(Def));
		return Defs.Defs.Last();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEntityActivatePassTest,
	"Elysium.Substrate.EntityWorld.ActivatePass", GElysiumActivatePassFlags)
bool FElysiumEntityActivatePassTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("activate_pass");
	AddActivatePassProbe(Defs, TEXT("first")).Keys.Add(TEXT("kill"), TEXT("victim"));
	Defs.Defs.Last().Keys.Add(TEXT("spawn"), TEXT("created"));
	AddActivatePassProbe(Defs, TEXT("dormant"));
	AddActivatePassProbe(Defs, TEXT("victim"));
	AddActivatePassProbe(Defs, TEXT("predead"));
	AddActivatePassProbe(Defs, TEXT("hidden"));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	FElysiumEntity* Dormant = World.FindByName(TEXT("dormant"));
	FElysiumEntity* Predead = World.FindByName(TEXT("predead"));
	FElysiumEntity* Hidden = World.FindByName(TEXT("hidden"));
	if (!TestNotNull(TEXT("dormant"), Dormant) || !TestNotNull(TEXT("predead"), Predead)
		|| !TestNotNull(TEXT("hidden"), Hidden))
	{
		return false;
	}
	Dormant->bEflDormant = true;   // what `MakeDormant` (`0x100a8060`) leaves
	Predead->Kill();                // removed before the pass: retail purges it from the list
	Hidden->ScriptHide();           // ScriptHide is not dormancy: retail still activates it

	GActivatePassOrder.Reset();
	World.Activate(0.0);

	const TArray<FString> Expected = {
		TEXT("first"),     // kills `victim` and creates `created` from inside its Activate
		TEXT("victim"),    // killed during the pass but still listed: activated
		TEXT("hidden"),    // hidden, not dormant: activated
		TEXT("created"),   // appended at the tail: activated after every listed entity
	};
	TestEqual(TEXT("the pass order"), FString::Join(GActivatePassOrder, TEXT(" ")),
		FString::Join(Expected, TEXT(" ")));
	TestFalse(TEXT("an EFL_DORMANT entity is not activated"), Dormant->bActivateCalled);
	TestFalse(TEXT("an entity removed before the pass is not activated"), Predead->bActivateCalled);
	return true;
}

#endif
