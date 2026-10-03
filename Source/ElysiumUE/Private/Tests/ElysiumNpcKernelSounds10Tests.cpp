#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "ElysiumNpcFlags.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29d, family **Sounds10** — the seventeen sound hooks of slots 488–507, the three `KeyValue`
// overloads and the two formatters behind them, slot 185 `FireBullets`, and the two species arms
// over them (`CNPC_VWerewolf#500`, `CNPC_VWerewolf#491`).
//
// Every assertion is read off the decompiled C or the listing, and the address it came from is named
// beside it. The concept-name constants were read out of the pinned `vampire.dll` at `0x105d8c30`…
// and `0x1057a1a0`, which is why they carry underscores.

static constexpr EAutomationTestFlags GElysiumNpcKernelSounds10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC, quiet, so nothing competes with the pass a case drives. Prefixed because the module
	// builds adaptive-unity and this anonymous namespace is merged with the other suites'. The NPC
	// is spawned as `RetailClass` through its factory classname (story 5 step 2); null or
	// `CAI_BaseNPCTroika` stands the bare Troika line.
	struct FSounds10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		explicit FSounds10Fixture(const TCHAR* DialogName = nullptr,
			const TCHAR* RetailClass = TEXT("CNPC_VHumanCombatant"))
			: World([DialogName, RetailClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("sounds10_kernel"), 4242);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					FElysiumEntityDef& Guard = Builder.AddNpcOfClass(TEXT("guard"),
						FVector(200.f, 0.f, 0.f), RetailClass);
					if (DialogName != nullptr)
					{
						Guard.Keys.Add(TEXT("dialogname"), DialogName);
					}
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("guard"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}
	};

	// The one speak the hook under test produced, or a default-constructed row when it made none.
	FElysiumNpc::FVSoundSpeak OnlySpeak(const FElysiumNpc& Npc)
	{
		return Npc.VSoundSpeakCalls.Num() == 1 ? Npc.VSoundSpeakCalls[0]
			: FElysiumNpc::FVSoundSpeak();
	}
}

// =================================================================================================
// Slot 110 `KeyValue` — `0x101c1480`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10KeyValueStringTest,
	"Elysium.Arm.NpcKernelSounds10.KeyValueString", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10KeyValueStringTest::RunTest(const FString&)
{
	FSounds10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `0x101c1480` arm 1: `__strcmpi(key, "lip")` → `FSTP [ESI + 0x504]` (`101c14a4`), true.
	TestTrue(TEXT("slot 110 claims \"lip\""), F.Npc->KeyValue(TEXT("lip"), TEXT("12.5")));
	TestEqual(TEXT("...into m_flLip (+0x504)"), F.Npc->Lip, 12.5f);
	TestEqual(TEXT("...and leaves m_flMoveDistance alone"), F.Npc->MoveDistance, 0.f);

	// Arm 2: `__strcmpi(key, "distance")` → `FSTP [ESI + 0x4fc]` (`101c14d0`), true.
	TestTrue(TEXT("slot 110 claims \"distance\""), F.Npc->KeyValue(TEXT("distance"), TEXT("64")));
	TestEqual(TEXT("...into m_flMoveDistance (+0x4fc)"), F.Npc->MoveDistance, 64.f);
	TestEqual(TEXT("...and leaves m_flLip alone"), F.Npc->Lip, 12.5f);

	// `__strcmpi` is case-INSENSITIVE.
	TestTrue(TEXT("the compare is case-insensitive"), F.Npc->KeyValue(TEXT("LiP"), TEXT("3")));
	TestEqual(TEXT("...so \"LiP\" writes m_flLip too"), F.Npc->Lip, 3.f);

	// `atof` of a non-numeric value is 0.0, and the arm STILL answers true — retail's own answer.
	TestTrue(TEXT("a non-numeric value is still claimed"), F.Npc->KeyValue(TEXT("lip"), TEXT("x")));
	TestEqual(TEXT("...and atof wrote 0.0"), F.Npc->Lip, 0.f);

	// Arm 3: anything else tails to `CBaseEntity::KeyValue` (`0x1009e430`) and returns its answer
	// verbatim. A key no level of the datamap chain claims is false.
	TestFalse(TEXT("an unknown key falls through to CBaseEntity::KeyValue and is refused"),
		F.Npc->KeyValue(TEXT("no_such_keyfield_anywhere"), TEXT("1")));
	// One of the nine literal arms `0x1009e430` matches itself. Not run here — it is `CBaseEntity`'s
	// row — but a matched key must not be reported to the caller as unhandled.
	TestTrue(TEXT("a CBaseEntity literal arm (\"origin\") answers true"),
		F.Npc->KeyValue(TEXT("origin"), TEXT("0 0 0")));
	// Retail truncates the key at a `#` FIRST (family Lifecycle's recovery), so `origin#2` is
	// `origin`.
	TestTrue(TEXT("...and the `#` truncation runs first, so \"origin#2\" is \"origin\""),
		F.Npc->KeyValue(TEXT("origin#2"), TEXT("0 0 0")));
	return true;
}

// =================================================================================================
// The VSound seams — what every hook below ends in
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10VSoundSeamTest,
	"Elysium.Arm.NpcKernelSounds10.VSoundSeam", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10VSoundSeamTest::RunTest(const FString&)
{
	// The concept lookup walks a list this runtime does not load, so the count is zero and the
	// fall-through is retail's own `0xffffffff`. A miss does NOT suppress the play call.
	TestEqual(TEXT("a concept lookup with no list answers retail's miss, -1"),
		FElysiumNpc::VSoundConceptId(TEXT("Death")), INDEX_NONE);
	TestEqual(TEXT("...for every concept, including one retail has no entry for either"),
		FElysiumNpc::VSoundConceptId(TEXT("not_a_concept")), INDEX_NONE);
	return true;
}

// =================================================================================================
// The seventeen sound hooks, slots 488–507
// =================================================================================================

namespace
{
	// One hook, driven and read back: the concept, the channel, the volume and the fifth argument.
	// Every plain hook is the same two statements, so the case per row states its OWN concept and
	// asserts the same four numbers against it.
	struct FHookExpectation
	{
		const TCHAR* Concept = nullptr;
		float Attenuation = 1.25f;
	};

	void CheckPlainHook(FAutomationTestBase& Test, FElysiumNpc& Npc, const TCHAR* Where,
		const FHookExpectation& Expected)
	{
		const FString Prefix(Where);
		Test.TestEqual(*(Prefix + TEXT(" speaks exactly once")), Npc.VSoundSpeakCalls.Num(), 1);
		const FElysiumNpc::FVSoundSpeak Speak = OnlySpeak(Npc);
		Test.TestEqual(*(Prefix + TEXT(" names the recovered concept")),
			FString(Speak.Concept != nullptr ? Speak.Concept : TEXT("")), FString(Expected.Concept));
		// The miss the empty concept list answers, handed to the play entry unchanged.
		Test.TestEqual(*(Prefix + TEXT(" carries retail's miss id")), Speak.ConceptId, INDEX_NONE);
		// `0x101f5950`'s third, fourth and fifth arguments, which it hands straight to `EmitSound`.
		Test.TestEqual(*(Prefix + TEXT(" on CHAN_VOICE")), Speak.Channel, 2);
		Test.TestEqual(*(Prefix + TEXT(" at volume 1.0")), Speak.Volume, 1.0f);
		Test.TestEqual(*(Prefix + TEXT(" with retail's fifth argument")), Speak.Attenuation,
			Expected.Attenuation);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10DeathSoundTest,
	"Elysium.Arm.NpcKernelSounds10.DeathSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10DeathSoundTest::RunTest(const FString&)
{
	// `0x10293ec0`, slot 488. The concept is `"Death"` (`0x105d8c30`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->DeathSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 488 DeathSound"), { TEXT("Death"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10AlertSoundTest,
	"Elysium.Arm.NpcKernelSounds10.AlertSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10AlertSoundTest::RunTest(const FString&)
{
	// `0x10293f80`, slot 489. `"Target_Suspect"` (`0x105d8c38`) — UNDERSCORED, read out of the
	// pinned image; the checklist's walk spelled it with a space.
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->AlertSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 489 AlertSound"), { TEXT("Target_Suspect"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10IdleSoundTest,
	"Elysium.Arm.NpcKernelSounds10.IdleSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10IdleSoundTest::RunTest(const FString&)
{
	// `0x10294280`, slot 490. `"Idle_Calm"` (`0x105d8c60`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	// The Troika body has NO `FOkToMakeSound` gate: a body that just made a sound still speaks.
	F.Npc->JustMadeSound();
	F.Npc->IdleSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 490 IdleSound"), { TEXT("Idle_Calm"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10PainSoundTest,
	"Elysium.Arm.NpcKernelSounds10.PainSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10PainSoundTest::RunTest(const FString&)
{
	// `0x10294340`, slot 491. The concept at `0x105d8c6c`, which the corpus left unnamed and the
	// pinned image reads as `"Pain"`.
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->PainSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 491 PainSound"), { TEXT("Pain"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10PainSoundWerewolfTest,
	"Elysium.Arm.NpcKernelSounds10.PainSoundWerewolf", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10PainSoundWerewolfTest::RunTest(const FString&)
{
	// `CNPC_VWerewolf::vfunc491` (`0x103d87a0`), 245 bytes: the SAME concept through its own guard
	// and cache, and `0` as the play entry's fifth argument where the base passes `1.25`.
	{
		FSounds10Fixture F(nullptr, TEXT("CNPC_VWerewolf"));
		if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
		TestNotNull(TEXT("the census carries the class"),
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VWerewolf")));
		F.Npc->PainSound();
		CheckPlainHook(*this, *F.Npc, TEXT("CNPC_VWerewolf#491"), { TEXT("Pain"), 0.0f });
	}
	// A bare Troika NPC has no species class at all, so every species lookup falls through to the
	// Troika line.
	{
		FSounds10Fixture F(nullptr, TEXT("CAI_BaseNPCTroika"));
		if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
		F.Npc->PainSound();
		CheckPlainHook(*this, *F.Npc, TEXT("a bare Troika NPC at slot 491"),
			{ TEXT("Pain"), 1.25f });
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10FearSoundTest,
	"Elysium.Arm.NpcKernelSounds10.FearSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10FearSoundTest::RunTest(const FString&)
{
	// `0x10294400`, slot 492. `"Fear_Start"` (`0x105d8c74`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->FearSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 492 FearSound"), { TEXT("Fear_Start"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10LostEnemySoundTest,
	"Elysium.Arm.NpcKernelSounds10.LostEnemySound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10LostEnemySoundTest::RunTest(const FString&)
{
	// `0x102944c0`, slot 493 — the one hook with a roll in front: `RandomInt(0, 99) < 0x19`
	// (`1029450a`), 25 in 100. The draw happens on EVERY call, so the stream advances whether or
	// not the sound is spoken; that position is what the rest of the idle branch inherits.
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }

	// Drive 400 calls off the fixture's own seed and count the admissions. The gate is the
	// behaviour; the exact seeded sequence is not, so the assertion is the ratio's band.
	int32 Spoken = 0;
	for (int32 Pass = 0; Pass < 400; ++Pass)
	{
		F.Npc->VSoundSpeakCalls.Reset();
		F.Npc->LostEnemySound();
		Spoken += F.Npc->VSoundSpeakCalls.Num();
	}
	TestTrue(TEXT("roughly a quarter of 400 calls speak (25 in 100)"),
		Spoken > 60 && Spoken < 140);

	// And when it does speak it is the plain shape over `"Target_Lost"` (`0x105d8c84`).
	ElysiumRng::SeedAll(4242);
	FElysiumNpc::FVSoundSpeak Speak;
	for (int32 Pass = 0; Pass < 200 && Speak.Concept == nullptr; ++Pass)
	{
		F.Npc->VSoundSpeakCalls.Reset();
		F.Npc->LostEnemySound();
		if (F.Npc->VSoundSpeakCalls.Num() == 1)
		{
			Speak = F.Npc->VSoundSpeakCalls[0];
		}
	}
	TestEqual(TEXT("slot 493 names Target_Lost"),
		FString(Speak.Concept != nullptr ? Speak.Concept : TEXT("")), FString(TEXT("Target_Lost")));
	TestEqual(TEXT("...on CHAN_VOICE"), Speak.Channel, 2);
	TestEqual(TEXT("...at volume 1.0"), Speak.Volume, 1.0f);
	TestEqual(TEXT("...with the fifth argument 1.25"), Speak.Attenuation, 1.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10FoundEnemySoundTest,
	"Elysium.Arm.NpcKernelSounds10.FoundEnemySound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10FoundEnemySoundTest::RunTest(const FString&)
{
	// `0x10294590`, slot 494: `IsBusyWithDiscipline()` must be false, then `"Target_Reacquired"`
	// (`0x105d8c94`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }

	F.Npc->NpcFlags.Set(EElysiumNpcFlag::D_IS_BUSY);
	F.Npc->FoundEnemySound();
	TestEqual(TEXT("a body busy with a discipline says nothing"), F.Npc->VSoundSpeakCalls.Num(), 0);

	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::D_IS_BUSY);
	F.Npc->FoundEnemySound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 494 FoundEnemySound"),
		{ TEXT("Target_Reacquired"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10SurprisedSoundTest,
	"Elysium.Arm.NpcKernelSounds10.SurprisedSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10SurprisedSoundTest::RunTest(const FString&)
{
	// `0x10294660`, slot 495. `"Surprised"` (`0x105d8cac`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->SurprisedSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 495 SurprisedSound"), { TEXT("Surprised"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10FleeSoundTest,
	"Elysium.Arm.NpcKernelSounds10.FleeSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10FleeSoundTest::RunTest(const FString&)
{
	// `0x10294870`, slot 498. The second concept the corpus left unnamed (`0x105d8cd0`), read out
	// of the pinned image as `"Flee"` — one cell past the placeholder `"???"` at `0x105d8ccc`.
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->FleeSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 498 FleeSound"), { TEXT("Flee"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10IdleAgitatedSoundTest,
	"Elysium.Arm.NpcKernelSounds10.IdleAgitatedSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10IdleAgitatedSoundTest::RunTest(const FString&)
{
	// `0x10294930`, slot 499. `"Idle_Agitated"` (`0x105d8cd8`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->IdleAgitatedSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 499 IdleAgitatedSound"),
		{ TEXT("Idle_Agitated"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10ExertHvySoundWerewolfTest,
	"Elysium.Arm.NpcKernelSounds10.ExertHvySoundWerewolf", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10ExertHvySoundWerewolfTest::RunTest(const FString&)
{
	// `CNPC_VWerewolf::vfunc500` (`0x103d8660`): the same concept through its own guard
	// `DAT_1093f99c` and cache `DAT_1093fa30`, and `0` as the fifth argument.
	{
		FSounds10Fixture F(nullptr, TEXT("CNPC_VWerewolf"));
		if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
		F.Npc->ExertHvySound();
		CheckPlainHook(*this, *F.Npc, TEXT("CNPC_VWerewolf#500"), { TEXT("Exert_Heavy"), 0.0f });
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10ComfortSoundTest,
	"Elysium.Arm.NpcKernelSounds10.ComfortSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10ComfortSoundTest::RunTest(const FString&)
{
	// `0x10294c30`, slot 503. `"Comfort"` (`0x105d8cf0`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->ComfortSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 503 ComfortSound"), { TEXT("Comfort"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10UpsetSoundTest,
	"Elysium.Arm.NpcKernelSounds10.UpsetSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10UpsetSoundTest::RunTest(const FString&)
{
	// `0x10294cf0`, slot 504. `"Upset"` (`0x105d8cfc`).
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
	F.Npc->UpsetSound();
	CheckPlainHook(*this, *F.Npc, TEXT("slot 504 UpsetSound"), { TEXT("Upset"), 1.25f });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10FloatSoundTest,
	"Elysium.Arm.NpcKernelSounds10.FloatSound", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10FloatSoundTest::RunTest(const FString&)
{
	// `0x10294f40`, slot 507 — the computed fifth argument and the state write.
	//
	// `10294f9f`: `t = m_iDialog ? 0xe : 0`; `t + 0x42` is `0x42` or `0x50`, both above `0x32`, so
	// the `4.0` constant arm at `0x10449148` is UNREACHABLE and the answer is the INTEGER quotient
	// `0x14 / (t + 0x10)` — `20/16 = 1` idle and `20/30 = 0` in dialogue.
	TestEqual(TEXT("idle: 20/16 truncates to 1"), FElysiumNpc::FloatSoundAttenuation(false), 1.0f);
	TestEqual(TEXT("in dialogue: 20/30 truncates to 0"),
		FElysiumNpc::FloatSoundAttenuation(true), 0.0f);

	{
		FSounds10Fixture F;
		if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
		const double Before = F.Npc->World != nullptr ? F.Npc->World->NowSeconds() : 0.0;
		F.Npc->NextFloatSoundTime = 0.0;
		F.Npc->FloatSound();

		CheckPlainHook(*this, *F.Npc, TEXT("slot 507 FloatSound"), { TEXT("Float"), 1.0f });

		// `1029502c` pushes row **1** — `FloatSoundMinDelay`, 5.0 — which `__ftol` truncates to an
		// int and `FIADD` adds to the engine clock. This is the FIRST writer of
		// `m_flNextFloatSoundTime` (`+0x10ec`) in the port; `FElysiumNpcBase::ShouldPlayFloatSound` reads it.
		TestEqual(TEXT("Float_Sound_Info row 1 is the delay, truncated to an int"),
			F.Npc->FloatSoundMinDelaySeconds(), 5);
		TestEqual(TEXT("m_flNextFloatSoundTime = curtime + 5"), F.Npc->NextFloatSoundTime,
			Before + 5.0);
	}
	{
		// The same hook on a body with an authored `dialogname` — the one input that changes the
		// fifth argument.
		FSounds10Fixture F(TEXT("chatty"));
		if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }
		F.Npc->FloatSound();
		CheckPlainHook(*this, *F.Npc, TEXT("slot 507 in dialogue"), { TEXT("Float"), 0.0f });
	}
	return true;
}

// =================================================================================================
// Slot 185 `FireBullets` — `0x10268900`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10VectorVectorsTest,
	"Elysium.Arm.NpcKernelSounds10.VectorVectors", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10VectorVectorsTest::RunTest(const FString&)
{
	// `0x10138a90` is `VectorVectors`, NOT `AngleVectors` — the checklist's walk named the wrong
	// one. `right = normalize(f.y, -f.x, 0)`, `up = normalize(cross(right, f))`.
	FVector RightAxis = FVector::ZeroVector;
	FVector UpAxis = FVector::ZeroVector;
	FElysiumEntity::VectorVectors(FVector(1.0, 0.0, 0.0), RightAxis, UpAxis);
	TestTrue(TEXT("forward +X gives right (0,-1,0)"),
		RightAxis.Equals(FVector(0.0, -1.0, 0.0), 1e-4));
	TestTrue(TEXT("...and up (0,0,1)"), UpAxis.Equals(FVector(0.0, 0.0, 1.0), 1e-4));
	TestTrue(TEXT("the basis is orthonormal"),
		FMath::IsNearlyZero(FVector::DotProduct(RightAxis, UpAxis), 1e-4));

	// Retail's degenerate arm, verbatim: a vertical forward answers `right = (1,0,0)` and
	// `up = (0, -f.z, 0)` — unnormalized, and not perpendicular to the forward.
	FElysiumEntity::VectorVectors(FVector(0.0, 0.0, -3.0), RightAxis, UpAxis);
	TestTrue(TEXT("a vertical forward takes retail's degenerate arm: right (1,0,0)"),
		RightAxis.Equals(FVector(1.0, 0.0, 0.0), 1e-4));
	TestTrue(TEXT("...and up (0, -f.z, 0), unnormalized"),
		UpAxis.Equals(FVector(0.0, 3.0, 0.0), 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10FireBulletsTest,
	"Elysium.Arm.NpcKernelSounds10.FireBullets", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10FireBulletsTest::RunTest(const FString&)
{
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }

	// A crash guard where retail would fault: no caller passes null yet.
	F.Npc->FireBullets(nullptr);
	TestEqual(TEXT("a null packet traces nothing"), F.Npc->FireBulletsTraces.Num(), 0);

	FElysiumEntity::FElysiumFireBulletsInfo Info;
	Info.Repeats = 2;
	Info.Bullets = 3;
	Info.SrcUnits = FVector(10.0, 20.0, 30.0);
	Info.DirShooting = FVector(1.0, 0.0, 0.0);
	Info.DistanceUnits = 100.f;
	Info.AmmoType = 4;
	Info.TracerScale = 12.f;
	// The `0x2000000` ammo flag is what suppresses spread — and this runtime's ammo def answers 0
	// for every index, so the flag is clear and the spread offset is added. Start from a ZERO
	// spread so the endpoint is exact whatever the rejection sampler drew.
	Info.Spread = FVector::ZeroVector;

	const int32 ReloadBefore = F.Npc->FakeReloadCount;
	F.Npc->FireBullets(&Info);

	// 1. `m_pBaseNPCTroika` (`+0x98`) is always set on this leaf, so the counter always drops — once
	//    per CALL, not once per bullet.
	TestEqual(TEXT("m_iFakeReloadCount drops by one per call"), F.Npc->FakeReloadCount,
		ReloadBefore - 1);

	// 2. The ammo flags are written back into the packet. `0x104276a0` answers 0 for an index no
	//    `CAmmoDef` carries, which is every index here.
	TestEqual(TEXT("info+0x58 takes the ammo def's flags"), Info.AmmoFlags, 0);

	// 3. The attacker defaults to the shooter.
	TestTrue(TEXT("info+0x94 defaults to the shooter"), Info.Attacker.IsSet());
	TestEqual(TEXT("...which is this NPC"), Info.Attacker.Index, F.Npc->Handle.Index);

	// 4. `DAT_1072cb48 = flags | 0x1000`.
	TestEqual(TEXT("the filter word is flags | 0x1000"), F.Npc->FireBulletsFilterWord, 0x1000);

	// 7/8. Two repeats of three bullets, and slot 186 answers false (`0x100270c0`'s whole body is
	//      `return false;`), so every bullet runs the trace-and-damage pass. The tracer arm needs
	//      BOTH the latch and a non-empty name, and the skill seam answers 0, so no tracer here.
	TestEqual(TEXT("2 repeats x 3 bullets = 6 trace passes"), F.Npc->FireBulletsTraces.Num(), 6);
	TestEqual(TEXT("and no tracer, because info+0xa8 is empty"),
		F.Npc->BulletTracerCalls.Num(), 0);
	if (F.Npc->FireBulletsTraces.Num() == 6)
	{
		// 7c. `end = src + dir * distance`, with a zero spread leaving the forward untouched.
		TestTrue(TEXT("the endpoint is src + dir * distance"),
			F.Npc->FireBulletsTraces[0].EndUnits.Equals(FVector(110.0, 20.0, 30.0), 1e-3));
		TestEqual(TEXT("every trace carries the filter word"),
			F.Npc->FireBulletsTraces[0].FilterWord, 0x1000);
	}

	// 7d. The tracer latch did not fire, so `info+0xa4` was never divided.
	TestEqual(TEXT("the skill divisor is not applied without the latch"), Info.TracerScale, 12.f);

	// 9. Nothing was hit — the trace seam reports no victim — so the tally is empty and
	//    `RangedDamagePerVictim` (`0x10268330`) is never reached.
	TestEqual(TEXT("an empty tally pays no victim"), F.Npc->RangedDamagePerVictimCalls.Num(), 0);

	// The tracer arm, driven by `info+0xa0` bit 0 and a name. It REPLACES the trace pass.
	F.Npc->FireBulletsTraces.Reset();
	FElysiumEntity::FElysiumFireBulletsInfo Tracer;
	Tracer.Repeats = 1;
	Tracer.Bullets = 4;
	Tracer.DirShooting = FVector(1.0, 0.0, 0.0);
	Tracer.TracerFlags = 0x1;
	Tracer.TracerScale = 12.f;
	Tracer.TracerName = TEXT("Tracer");
	F.Npc->FireBullets(&Tracer);
	TestEqual(TEXT("the tracer arm replaces the trace pass"), F.Npc->FireBulletsTraces.Num(), 0);
	TestEqual(TEXT("...one tracer per bullet"), F.Npc->BulletTracerCalls.Num(), 4);
	if (F.Npc->BulletTracerCalls.Num() == 4)
	{
		// `_DAT_104454c0 / info[1]` — 1.0 over the bullet count.
		TestEqual(TEXT("...with 1.0/bullets as the per-shot fraction"),
			F.Npc->BulletTracerCalls[0].Fraction, 0.25f);
	}
	// The skill seam answers 0, so the divisor is `DAT_104994c8[0]` = 1.0 and the scale is
	// unchanged — but the latch DID fire, which is what the flag bit alone does.
	TestEqual(TEXT("a skill of 0 divides info+0xa4 by 1.0"), Tracer.TracerScale, 12.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSounds10BulletSpreadTest,
	"Elysium.Arm.NpcKernelSounds10.BulletSpread", GElysiumNpcKernelSounds10Flags)
bool FElysiumNpcKernelSounds10BulletSpreadTest::RunTest(const FString&)
{
	// `0x10268170`: a rejection sample of two sums of two `RandomFloat(-0.5, 0.5)` draws, redrawn
	// while `x*x + y*y > 1.0`, scaled by the spread's x and y along right and up.
	FSounds10Fixture F;
	if (F.Npc == nullptr) { AddError(TEXT("no NPC")); return false; }

	const FVector RightAxis(0.0, -1.0, 0.0);
	const FVector UpAxis(0.0, 0.0, 1.0);
	const FVector Spread(0.25, 0.5, 0.0);

	// Every accepted sample is inside the unit disc, so the offset never exceeds the spread's own
	// components — and it always lies in the right/up plane, never along the forward.
	for (int32 Pass = 0; Pass < 200; ++Pass)
	{
		const FVector Offset = F.Npc->BulletSpreadOffset(Spread, RightAxis, UpAxis);
		TestTrue(TEXT("the offset carries no forward component"),
			FMath::IsNearlyZero(Offset.X, 1e-6));
		TestTrue(TEXT("the right component is inside spread.x"),
			FMath::Abs(Offset.Y) <= 0.25 + 1e-6);
		TestTrue(TEXT("the up component is inside spread.y"),
			FMath::Abs(Offset.Z) <= 0.5 + 1e-6);
		// The sampler's own invariant: the (x, y) pair it accepted was inside the unit disc, so the
		// normalized pair is too.
		const double NormX = Offset.Y / -0.25;
		const double NormY = Offset.Z / 0.5;
		TestTrue(TEXT("the accepted sample is inside the unit disc"),
			NormX * NormX + NormY * NormY <= 1.0 + 1e-6);
	}

	// A zero spread cannot move the bullet, whatever the sampler drew.
	const FVector None = F.Npc->BulletSpreadOffset(FVector::ZeroVector, RightAxis, UpAxis);
	TestTrue(TEXT("a zero spread adds nothing"), None.IsNearlyZero());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
