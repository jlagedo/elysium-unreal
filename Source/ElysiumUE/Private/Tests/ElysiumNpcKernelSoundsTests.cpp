#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Sounds** — the NPC's sound and speech surface.
//
// Nine Troika-line slots, the five base-class halves behind them, and the per-species vocalization
// table. Every assertion below is read off the decompiled C or the listing, and the address it came
// from is named beside it.

static constexpr EAutomationTestFlags GElysiumNpcKernelSoundsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC, quiet, so nothing competes with the pass a case drives. Prefixed because the module
	// builds adaptive-unity and this anonymous namespace is merged with the other suites'.
	struct FSoundsFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		explicit FSoundsFixture(const TCHAR* Classname = TEXT("npc_VHumanCombatant"))
			: World([Classname]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("sounds_kernel"), 4242);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("guard"), FVector(200.f, 0.f, 0.f), Classname);
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("guard"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}
	};

	const FElysiumNpc::FVocalization* SoundsRow(const TCHAR* RetailClass, int32 Slot)
	{
		return FElysiumNpc::VocalizationFor(RetailClass, Slot);
	}
}

// =================================================================================================
// The species vocalization table — every row by name
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsTableTest,
	"Elysium.Substrate.NpcKernelSounds.Table", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsTableTest::RunTest(const FString&)
{
	using EKind = FElysiumNpc::EVocalization;

	int32 Count = 0;
	const FElysiumNpc::FVocalization* Rows = FElysiumNpc::Vocalizations(Count);
	TestNotNull(TEXT("the table exists"), Rows);
	// 4 CGeneric_NPC + 4 CGenericSabbat_NPC + 19 CNPC_VCamera + 2 CNPC_VSabbatLeader
	// + 8 CNPC_VTest + 2 CNPC_VTzimisce.
	TestEqual(TEXT("39 recovered species sound bodies"), Count, 39);

	// Every row names a class the census carries and a slot inside the sound band (or the Sabbat
	// leader's two extended slots), and every retail address is spelled the ledger's way.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FVocalization& Row = Rows[Index];
		const FString Where = FString::Printf(TEXT("row %d (%s#%d)"), Index, Row.RetailClass,
			Row.Slot);
		TestNotNull(*(Where + TEXT(" names a census class")),
			ElysiumNpcKernelClass::Find(Row.RetailClass));
		TestTrue(*(Where + TEXT(" cites a retail address")),
			Row.RetailAddress != nullptr && FString(Row.RetailAddress).StartsWith(TEXT("0x10")));
		TestTrue(*(Where + TEXT(" is a sound slot")),
			(Row.Slot >= 488 && Row.Slot <= 508) || Row.Slot == 620 || Row.Slot == 621);
		// Retail passes `EmitSound` attenuation 0.8 on every recovered row.
		TestEqual(*(Where + TEXT(" emits at ATTN_NORM")), Row.Attenuation, 0.8f);
		TestTrue(*(Where + TEXT(" emits on CHAN_VOICE or CHAN_BODY")),
			Row.Channel == 2 || Row.Channel == 4);
		if (Row.Kind == EKind::WavPool)
		{
			TestTrue(*(Where + TEXT(" carries a non-empty wav pool")),
				Row.Wavs != nullptr && Row.WavCount > 0);
		}
		else if (Row.Kind == EKind::Sentence)
		{
			TestTrue(*(Where + TEXT(" names a sentence group")),
				Row.Sentence != nullptr && *Row.Sentence != TEXT('\0'));
		}
		else
		{
			TestTrue(*(Where + TEXT(" is mute and carries no pool")), Row.Wavs == nullptr);
		}
	}

	// --- CGeneric_NPC, `0x10359f70`'s four precached tables -------------------------------------
	{
		const FElysiumNpc::FVocalization* Death = SoundsRow(TEXT("CGeneric_NPC"), 488);
		TestNotNull(TEXT("CGeneric_NPC#488 DeathSound"), Death);
		if (Death != nullptr)
		{
			TestEqual(TEXT("...is 0x1035a500"), FString(Death->RetailAddress),
				FString(TEXT("0x1035a500")));
			TestEqual(TEXT("...draws RandomInt(0, 0) over one wav"), Death->WavCount, 1);
			TestEqual(TEXT("...npc/metropolice/die1.wav"), FString(Death->Wavs[0]),
				FString(TEXT("npc/metropolice/die1.wav")));
			// `0x3f000000` at `1035a54c` — the death hook is the only one that is not 1.0.
			TestEqual(TEXT("...at volume 0.5"), Death->Volume, 0.5f);
			TestEqual(TEXT("...on CHAN_VOICE"), Death->Channel, 2);
			TestFalse(TEXT("...and is not gated"), Death->bGatedByFOkToMakeSound);
		}
		const FElysiumNpc::FVocalization* Alert = SoundsRow(TEXT("CGeneric_NPC"), 489);
		TestNotNull(TEXT("CGeneric_NPC#489 AlertSound"), Alert);
		if (Alert != nullptr)
		{
			TestEqual(TEXT("...is 0x1035a390"), FString(Alert->RetailAddress),
				FString(TEXT("0x1035a390")));
			TestEqual(TEXT("...npc/metropolice/alert1.wav"), FString(Alert->Wavs[0]),
				FString(TEXT("npc/metropolice/alert1.wav")));
			TestEqual(TEXT("...at volume 1.0"), Alert->Volume, 1.0f);
		}
		const FElysiumNpc::FVocalization* Pain = SoundsRow(TEXT("CGeneric_NPC"), 491);
		TestNotNull(TEXT("CGeneric_NPC#491 PainSound"), Pain);
		if (Pain != nullptr)
		{
			TestEqual(TEXT("...is 0x1035a670"), FString(Pain->RetailAddress),
				FString(TEXT("0x1035a670")));
			// `RandomInt(0, 3)` — the citizen pain table is four entries, the only pool wider
			// than one on either generic class.
			TestEqual(TEXT("...draws RandomInt(0, 3) over four wavs"), Pain->WavCount, 4);
			TestEqual(TEXT("...first is npc/citizen/pain1.wav"), FString(Pain->Wavs[0]),
				FString(TEXT("npc/citizen/pain1.wav")));
			TestEqual(TEXT("...last is npc/citizen/pain4.wav"), FString(Pain->Wavs[3]),
				FString(TEXT("npc/citizen/pain4.wav")));
		}
		const FElysiumNpc::FVocalization* Surprise = SoundsRow(TEXT("CGeneric_NPC"), 495);
		TestNotNull(TEXT("CGeneric_NPC#495 SurprisedSound"), Surprise);
		if (Surprise != nullptr)
		{
			TestEqual(TEXT("...is 0x1035a220"), FString(Surprise->RetailAddress),
				FString(TEXT("0x1035a220")));
			TestEqual(TEXT("...npc/metropolice/surprise1.wav"), FString(Surprise->Wavs[0]),
				FString(TEXT("npc/metropolice/surprise1.wav")));
		}
		// The hooks CGeneric_NPC does NOT override: the Troika-line body runs instead.
		TestNull(TEXT("CGeneric_NPC has no FearSound override"),
			SoundsRow(TEXT("CGeneric_NPC"), 492));
	}

	// --- CGenericSabbat_NPC: the same four hooks, its own copies of the same four wavs ----------
	{
		const int32 Slots[] = { 488, 489, 491, 495 };
		const TCHAR* const Addresses[] = { TEXT("0x1035bb70"), TEXT("0x1035ba00"),
			TEXT("0x1035bce0"), TEXT("0x1035b890") };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FElysiumNpc::FVocalization* Sabbat = SoundsRow(TEXT("CGenericSabbat_NPC"),
				Slots[Index]);
			const FElysiumNpc::FVocalization* Generic = SoundsRow(TEXT("CGeneric_NPC"),
				Slots[Index]);
			TestNotNull(*FString::Printf(TEXT("CGenericSabbat_NPC#%d"), Slots[Index]), Sabbat);
			if (Sabbat == nullptr || Generic == nullptr)
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("CGenericSabbat_NPC#%d is %s"), Slots[Index],
				Addresses[Index]), FString(Sabbat->RetailAddress), FString(Addresses[Index]));
			// Two separate `.rdata` pointer tables holding the same strings — the ports agree
			// because retail's bodies do.
			TestEqual(*FString::Printf(TEXT("...same pool width as CGeneric_NPC#%d"),
				Slots[Index]), Sabbat->WavCount, Generic->WavCount);
			TestEqual(*FString::Printf(TEXT("...same first wav as CGeneric_NPC#%d"),
				Slots[Index]), FString(Sabbat->Wavs[0]), FString(Generic->Wavs[0]));
			TestEqual(*FString::Printf(TEXT("...same volume as CGeneric_NPC#%d"), Slots[Index]),
				Sabbat->Volume, Generic->Volume);
		}
	}

	// --- CNPC_VCamera: nineteen empty overrides, inherited by CNPC_VCameraSecurity --------------
	{
		const int32 Mutes[] = { 488, 489, 490, 491, 492, 493, 494, 495, 496, 498, 499, 500, 501,
			502, 503, 504, 505, 507, 508 };
		const TCHAR* const Addresses[] = { TEXT("0x103680b0"), TEXT("0x103680d0"),
			TEXT("0x103680f0"), TEXT("0x10368110"), TEXT("0x10368130"), TEXT("0x10368150"),
			TEXT("0x10368170"), TEXT("0x10368190"), TEXT("0x103681b0"), TEXT("0x103681f0"),
			TEXT("0x10368210"), TEXT("0x10368230"), TEXT("0x10368250"), TEXT("0x10368270"),
			TEXT("0x10368290"), TEXT("0x103682b0"), TEXT("0x103682d0"), TEXT("0x10368310"),
			TEXT("0x10368330") };
		static_assert(UE_ARRAY_COUNT(Mutes) == UE_ARRAY_COUNT(Addresses), "one address per slot");
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Mutes); ++Index)
		{
			const FElysiumNpc::FVocalization* Row = SoundsRow(TEXT("CNPC_VCamera"), Mutes[Index]);
			TestNotNull(*FString::Printf(TEXT("CNPC_VCamera#%d is a row"), Mutes[Index]), Row);
			if (Row == nullptr)
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("CNPC_VCamera#%d is %s"), Mutes[Index],
				Addresses[Index]), FString(Row->RetailAddress), FString(Addresses[Index]));
			TestTrue(*FString::Printf(TEXT("CNPC_VCamera#%d is mute"), Mutes[Index]),
				Row->Kind == EKind::Mute);
			// The chain walk, not a name compare: `CNPC_VCameraSecurity`'s base is
			// `CNPC_VCamera`, so it inherits every one of these rather than carrying a copy.
			TestTrue(*FString::Printf(TEXT("CNPC_VCameraSecurity#%d inherits it"), Mutes[Index]),
				SoundsRow(TEXT("CNPC_VCameraSecurity"), Mutes[Index]) == Row);
		}
		// 497 and 506 are unnamed slots in the band and the camera does not fill them.
		TestNull(TEXT("CNPC_VCamera fills no slot 497"), SoundsRow(TEXT("CNPC_VCamera"), 497));
		TestNull(TEXT("CNPC_VCamera fills no slot 506"), SoundsRow(TEXT("CNPC_VCamera"), 506));
	}

	// --- CNPC_VSabbatLeader: two hooks, on CHAN_BODY ---------------------------------------------
	{
		const FElysiumNpc::FVocalization* Step = SoundsRow(TEXT("CNPC_VSabbatLeader"), 620);
		TestNotNull(TEXT("CNPC_VSabbatLeader#620 FootstepSound"), Step);
		if (Step != nullptr)
		{
			TestEqual(TEXT("...is 0x103aa5e0"), FString(Step->RetailAddress),
				FString(TEXT("0x103aa5e0")));
			// `PUSH 0x6` at `103aa6c9`: `RandomInt(0, 6)` over seven step wavs.
			TestEqual(TEXT("...draws RandomInt(0, 6) over seven wavs"), Step->WavCount, 7);
			TestEqual(TEXT("...first is step1.wav"), FString(Step->Wavs[0]),
				FString(TEXT("character/monster/andrei_transformed/step1.wav")));
			TestEqual(TEXT("...last is step7.wav"), FString(Step->Wavs[6]),
				FString(TEXT("character/monster/andrei_transformed/step7.wav")));
			// `PUSH 0x4` at `103aa6dd` — CHAN_BODY, not the voice channel the vocalizations use.
			TestEqual(TEXT("...on CHAN_BODY"), Step->Channel, 4);
			TestEqual(TEXT("...at volume 1.0"), Step->Volume, 1.0f);
		}
		const FElysiumNpc::FVocalization* Attack = SoundsRow(TEXT("CNPC_VSabbatLeader"), 621);
		TestNotNull(TEXT("CNPC_VSabbatLeader#621 AttackSound"), Attack);
		if (Attack != nullptr)
		{
			TestEqual(TEXT("...is 0x103aa7a0"), FString(Attack->RetailAddress),
				FString(TEXT("0x103aa7a0")));
			// `PUSH 0x2` at `103aa889`.
			TestEqual(TEXT("...draws RandomInt(0, 2) over three wavs"), Attack->WavCount, 3);
			TestEqual(TEXT("...first is exert_heavy_1.wav"), FString(Attack->Wavs[0]),
				FString(TEXT("character/monster/andrei_transformed/exert_heavy_1.wav")));
			TestEqual(TEXT("...on CHAN_BODY"), Attack->Channel, 4);
		}
	}

	// --- CNPC_VTest: eight hooks, exactly one of them gated -------------------------------------
	{
		const int32 Slots[] = { 488, 489, 490, 491, 492, 493, 494, 495 };
		const TCHAR* const Addresses[] = { TEXT("0x103b4320"), TEXT("0x103b4490"),
			TEXT("0x103b4600"), TEXT("0x103b4780"), TEXT("0x103b48f0"), TEXT("0x103b4a60"),
			TEXT("0x103b4bd0"), TEXT("0x103b4d40") };
		const TCHAR* const FirstWav[] = { TEXT("character/npc/test/death1.wav"),
			TEXT("character/npc/test/alert1.wav"), TEXT("character/npc/test/idle1.wav"),
			TEXT("character/npc/test/pain1.wav"), TEXT("character/npc/test/fear1.wav"),
			TEXT("character/npc/test/lostenemy1.wav"),
			TEXT("character/npc/test/foundenemy1.wav"),
			TEXT("character/npc/test/surprise1.wav") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Slots); ++Index)
		{
			const FElysiumNpc::FVocalization* Row = SoundsRow(TEXT("CNPC_VTest"), Slots[Index]);
			TestNotNull(*FString::Printf(TEXT("CNPC_VTest#%d"), Slots[Index]), Row);
			if (Row == nullptr)
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("CNPC_VTest#%d is %s"), Slots[Index],
				Addresses[Index]), FString(Row->RetailAddress), FString(Addresses[Index]));
			TestEqual(*FString::Printf(TEXT("CNPC_VTest#%d plays %s"), Slots[Index],
				FirstWav[Index]), FString(Row->Wavs[0]), FString(FirstWav[Index]));
			// `103b4609` is the ONLY `CALL [EAX+0x798]` in the eight bodies: slot 490 is the
			// only CNPC_VTest hook the sound gate rate-limits.
			TestEqual(*FString::Printf(TEXT("CNPC_VTest#%d gate"), Slots[Index]),
				Row->bGatedByFOkToMakeSound, Slots[Index] == 490);
		}
		// The death hook is the only one at volume 0.5 (`0x3f000000` at `103b436c`); the pain
		// hook is the only four-wide pool (`PUSH 0x3` at `103b47cc`).
		TestEqual(TEXT("CNPC_VTest#488 is volume 0.5"),
			SoundsRow(TEXT("CNPC_VTest"), 488)->Volume, 0.5f);
		TestEqual(TEXT("CNPC_VTest#491 draws RandomInt(0, 3)"),
			SoundsRow(TEXT("CNPC_VTest"), 491)->WavCount, 4);
	}

	// --- CNPC_VTzimisce: two sentence hooks -----------------------------------------------------
	{
		const FElysiumNpc::FVocalization* Idle = SoundsRow(TEXT("CNPC_VTzimisce"), 490);
		TestNotNull(TEXT("CNPC_VTzimisce#490 IdleSound"), Idle);
		if (Idle != nullptr)
		{
			TestEqual(TEXT("...is 0x103b9380"), FString(Idle->RetailAddress),
				FString(TEXT("0x103b9380")));
			TestTrue(TEXT("...answers a sentence group"), Idle->Kind == EKind::Sentence);
			TestEqual(TEXT("...SPI_IDLE"), FString(Idle->Sentence), FString(TEXT("SPI_IDLE")));
			TestTrue(TEXT("...gated on FOkToMakeSound"), Idle->bGatedByFOkToMakeSound);
			TestFalse(TEXT("...and does not re-arm the clock"), Idle->bCallsJustMadeSound);
		}
		const FElysiumNpc::FVocalization* Pain = SoundsRow(TEXT("CNPC_VTzimisce"), 491);
		TestNotNull(TEXT("CNPC_VTzimisce#491 PainSound"), Pain);
		if (Pain != nullptr)
		{
			TestEqual(TEXT("...is 0x103b9500"), FString(Pain->RetailAddress),
				FString(TEXT("0x103b9500")));
			TestEqual(TEXT("...SPI_TAKE_DAMAGE"), FString(Pain->Sentence),
				FString(TEXT("SPI_TAKE_DAMAGE")));
			TestTrue(TEXT("...gated on FOkToMakeSound"), Pain->bGatedByFOkToMakeSound);
			// `CALL [EDX+0x79c]` at `103b9592`: the only vocalization in the family that
			// re-arms the sound clock itself.
			TestTrue(TEXT("...and calls JustMadeSound (vtable +0x79c)"), Pain->bCallsJustMadeSound);
		}
	}

	// A class outside the table, and a slot outside the band, answer nothing.
	TestNull(TEXT("CAI_BaseNPCTroika fills no species row"),
		SoundsRow(TEXT("CAI_BaseNPCTroika"), 490));
	TestNull(TEXT("a class the census does not carry answers nothing"),
		SoundsRow(TEXT("CNotAClass"), 490));
	TestNull(TEXT("a null class answers nothing"), SoundsRow(nullptr, 490));
	return true;
}

// =================================================================================================
// `EmitVocalization` on a LIVE species — `npc_VSabbatLeader` is a registered classname
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsEmitTest,
	"Elysium.Substrate.NpcKernelSounds.Emit", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsEmitTest::RunTest(const FString&)
{
	FSoundsFixture F(TEXT("npc_VSabbatLeader"));
	if (!TestNotNull(TEXT("the Sabbat leader spawned"), F.Npc))
	{
		return false;
	}
	const FElysiumNpcClass* Cls = F.Npc->RetailClass();
	if (!TestNotNull(TEXT("npc_VSabbatLeader resolves to a census class"), Cls))
	{
		return false;
	}
	TestEqual(TEXT("...CNPC_VSabbatLeader"), FString(Cls->Name),
		FString(TEXT("CNPC_VSabbatLeader")));

	// Slot 620 `FootstepSound` (`0x103aa5e0`): one CHAN_BODY emit from the seven-wav step pool at
	// volume 1.0, soundlevel 75 (the inverse of attenuation 0.8) and pitch 1.0 (retail's 100).
	F.World.Services.BodySounds.Reset();
	TestTrue(TEXT("the species claims slot 620"), F.Npc->EmitVocalization(620));
	if (TestEqual(TEXT("...with exactly one emit"), F.World.Services.BodySounds.Num(), 1))
	{
		const FElysiumBodySound& Sound = F.World.Services.BodySounds[0];
		TestTrue(TEXT("...from the andrei step pool"),
			Sound.Rel.StartsWith(TEXT("character/monster/andrei_transformed/step"))
				&& Sound.Rel.EndsWith(TEXT(".wav")));
		TestEqual(TEXT("...volume 1.0"), Sound.Volume, 1.0f);
		TestEqual(TEXT("...soundlevel 75"), Sound.SoundLevelDb, 75);
		TestEqual(TEXT("...pitch 1.0 (retail's PITCH_NORM 100)"), Sound.Pitch, 1.0f);
		TestTrue(TEXT("...on CHAN_BODY"), Sound.Channel == EElysiumSoundChannel::Body);
	}

	// Slot 621 `AttackSound` (`0x103aa7a0`): the three-wav exert pool, same channel and level.
	F.World.Services.BodySounds.Reset();
	TestTrue(TEXT("the species claims slot 621"), F.Npc->EmitVocalization(621));
	if (TestEqual(TEXT("...with exactly one emit"), F.World.Services.BodySounds.Num(), 1))
	{
		TestTrue(TEXT("...from the exert_heavy pool"),
			F.World.Services.BodySounds[0].Rel.StartsWith(
				TEXT("character/monster/andrei_transformed/exert_heavy_")));
	}

	// The draw is `RandomInt(0, 6)` over seven wavs, so repeated calls must reach more than one
	// member of the pool and never step outside it.
	F.World.Services.BodySounds.Reset();
	TSet<FString> Seen;
	for (int32 Pass = 0; Pass < 64; ++Pass)
	{
		F.Npc->EmitVocalization(620);
	}
	for (const FElysiumBodySound& Sound : F.World.Services.BodySounds)
	{
		Seen.Add(Sound.Rel);
	}
	TestEqual(TEXT("64 draws made 64 emits"), F.World.Services.BodySounds.Num(), 64);
	TestTrue(TEXT("...and reached more than one of the seven step wavs"), Seen.Num() > 1);
	TestTrue(TEXT("...all seven at most"), Seen.Num() <= 7);

	// A slot this species does not override: the Troika-line body (story 29d) runs instead, and
	// nothing was emitted here.
	F.World.Services.BodySounds.Reset();
	TestFalse(TEXT("the Sabbat leader has no PainSound override"), F.Npc->EmitVocalization(491));
	TestEqual(TEXT("...and emitted nothing"), F.World.Services.BodySounds.Num(), 0);

	// A body whose species answers a SENTENCE group cannot be spawned here (`npc_VTzimisce` is not
	// a registered classname), so the refusal the seam makes is asserted where it can be: the row
	// names the group, and the group resolves to nothing because this runtime has no sentence
	// table. Slot 484 is the same seam and the same refusal.
	TestEqual(TEXT("the sentence seam refuses a group by name"),
		F.Npc->PlaySentence(TEXT("SPI_IDLE"), 0.f, 1.f, 75, nullptr), -1);
	return true;
}

// =================================================================================================
// Slot 486 `FOkToMakeSound` and its base half
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsOkToMakeSoundTest,
	"Elysium.Substrate.NpcKernelSounds.OkToMakeSound", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsOkToMakeSoundTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}
	const double Now = F.World.World.NowSeconds();

	// `0x102b4c10`: twenty-four bytes, `return !IsInDialog();`. Nothing else gags a Troika NPC —
	// the sound-wait clock the base body reads is not consulted at all.
	F.Npc->Senses.Memory.SoundWaitTime = Now + 100.0;
	TestTrue(TEXT("the Troika gate ignores the sound-wait clock"), F.Npc->FOkToMakeSound());

	F.Npc->TalkingUntil = Now + 5.0;
	TestFalse(TEXT("a talking body refuses"), F.Npc->FOkToMakeSound());
	F.Npc->TalkingUntil = -1.0;
	F.Npc->Dialogue.bInDialog = true;
	TestFalse(TEXT("a body in a dialogue session refuses"), F.Npc->FOkToMakeSound());
	F.Npc->Dialogue.bInDialog = false;
	TestTrue(TEXT("and nothing else does"), F.Npc->FOkToMakeSound());

	// `0x1027a5c0`, the base half: the clock first, and the comparison is `<=` (the `AND 0x4100`
	// mask at `1027a5d8` carries the equal bit), so a sound at exactly the deadline is refused.
	F.Npc->Senses.Memory.SoundWaitTime = Now + 100.0;
	TestFalse(TEXT("the base gate refuses inside the sound-wait window"),
		F.Npc->BaseFOkToMakeSound());
	F.Npc->Senses.Memory.SoundWaitTime = Now;
	TestFalse(TEXT("...and refuses at exactly the deadline"), F.Npc->BaseFOkToMakeSound());
	F.Npc->Senses.Memory.SoundWaitTime = Now - 0.001;
	TestTrue(TEXT("...and allows past it"), F.Npc->BaseFOkToMakeSound());

	// `SF_NPC_GAG` (`m_spawnflags & 2`) plus `m_NPCState != NPC_STATE_COMBAT`. Idle is the
	// spawn state, so a gagged idle NPC is silent.
	F.Npc->SpawnFlags |= 0x2;
	TestFalse(TEXT("a gagged idle NPC refuses"), F.Npc->BaseFOkToMakeSound());
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestTrue(TEXT("...but a gagged NPC in COMBAT does not"), F.Npc->BaseFOkToMakeSound());
	F.Npc->SpawnFlags &= ~0x2;

	// SEAM: the squad half (`m_iSquadDisconnected < 1 && m_pSquad`, then the squad's own
	// `+0x60` clock). `ConnectedSquad()` answers null on every NPC in this substrate, so the arm
	// is asked and never refuses.
	TestTrue(TEXT("the squad the second gate reads is not built yet"),
		F.Npc->ConnectedSquad() == nullptr);
	TestEqual(TEXT("and the NPC is squad-connected as far as the test goes"),
		F.Npc->ScheduleHost.SquadDisconnected, 0);
	return true;
}

// =================================================================================================
// Slot 487 `JustMadeSound` — the two draws and the Tzimisce override
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsJustMadeSoundTest,
	"Elysium.Substrate.NpcKernelSounds.JustMadeSound", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsJustMadeSoundTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}
	const double Now = F.World.World.NowSeconds();

	// `0x102b4c40`: `m_flSoundWaitTime = curtime + RandomFloat(0.25, 0.75)`. Over many draws the
	// answer must land inside the window every time and must not be constant.
	double Low = TNumericLimits<double>::Max();
	double High = -TNumericLimits<double>::Max();
	bool bInRange = true;
	for (int32 Pass = 0; Pass < 256; ++Pass)
	{
		F.Npc->Senses.Memory.SoundWaitTime = 0.0;
		F.Npc->JustMadeSound();
		const double Wait = F.Npc->Senses.Memory.SoundWaitTime - Now;
		bInRange &= (Wait >= 0.25 - KINDA_SMALL_NUMBER) && (Wait <= 0.75 + KINDA_SMALL_NUMBER);
		Low = FMath::Min(Low, Wait);
		High = FMath::Max(High, Wait);
	}
	TestTrue(TEXT("the Troika draw is inside [0.25, 0.75] every time"), bInRange);
	TestTrue(TEXT("...and is a draw, not a constant"), High - Low > 0.1);

	// `0x1027a640`, the base half: the same shape with a 1.5–2.0 draw.
	bInRange = true;
	Low = TNumericLimits<double>::Max();
	High = -TNumericLimits<double>::Max();
	for (int32 Pass = 0; Pass < 256; ++Pass)
	{
		F.Npc->Senses.Memory.SoundWaitTime = 0.0;
		F.Npc->BaseJustMadeSound();
		const double Wait = F.Npc->Senses.Memory.SoundWaitTime - Now;
		bInRange &= (Wait >= 1.5 - KINDA_SMALL_NUMBER) && (Wait <= 2.0 + KINDA_SMALL_NUMBER);
		Low = FMath::Min(Low, Wait);
		High = FMath::Max(High, Wait);
	}
	TestTrue(TEXT("the base draw is inside [1.5, 2.0] every time"), bInRange);
	TestTrue(TEXT("...and is a draw, not a constant"), High - Low > 0.1);

	// `CNPC_VTzimisce::vfunc487` (`0x103b9f10`) is the one species override of the slot: a
	// 0.5–0.75 draw and NO squad copy. `npc_VTzimisce` is not a registered classname here, so the
	// species arm is exercised by name through the dispatcher the body itself asks.
	TestFalse(TEXT("a human combatant is not CNPC_VTzimisce"),
		F.Npc->IsRetailClass(TEXT("CNPC_VTzimisce")));
	TestNotNull(TEXT("CNPC_VTzimisce is a census class the arm can dispatch on"),
		ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisce")));
	// And the body it fills slot 487 with is the one the arm reproduces.
	TestEqual(TEXT("CNPC_VTzimisce fills slot 487 with 0x103b9f10"),
		FString(ElysiumNpcKernelClass::BodyOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisce")),
			487)).ToLower(), FString(TEXT("0x103b9f10")));
	return true;
}

// =================================================================================================
// Slots 474 / 475 — the sound and scent memory readers
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsBestSoundTest,
	"Elysium.Substrate.NpcKernelSounds.BestSound", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsBestSoundTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}

	// `0x102b4520` is seven bytes: `return &this->m_BestSound;` (`+0x60b0`). Not a copy and not
	// the senses object's live answer — the address of the COMMITTED record.
	TestTrue(TEXT("GetBestSound answers &m_BestSound"),
		F.Npc->GetBestSound() == static_cast<void*>(&F.Npc->Senses.Memory.BestSound));
	F.Npc->Senses.Memory.BestSound.Serial = 77;
	TestEqual(TEXT("...and it is the live member, not a snapshot"),
		static_cast<const FElysiumGameSoundEvent*>(F.Npc->GetBestSound())->Serial, (uint64)77);

	// `0x1026aef0`, the base half — `m_pSenses->GetClosestSound(false)`, unreachable on the Troika
	// line. SEAM: no "closest of the live list" accessor exists, so it answers null and warns
	// exactly as retail does.
	TestNull(TEXT("the base GetBestSound seam answers nothing"), F.Npc->BaseGetBestSound());

	// `0x1026af30` fills slot 475 for CAI_BaseNPCTroika too — the Troika line does NOT override
	// it — so this IS the dispatched body. SEAM: no scent channel exists on the game-sound bus.
	TestNull(TEXT("GetBestScent answers nothing: this runtime emits no scents"),
		F.Npc->GetBestScent());
	return true;
}

// =================================================================================================
// Slots 484 / 485 — the sentence players
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsSentenceTest,
	"Elysium.Substrate.NpcKernelSounds.Sentence", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsSentenceTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}

	// `10278e3f`: a null name is retail's own first refusal, and -1 is what it answers.
	TestEqual(TEXT("a null sentence is -1"), F.Npc->PlaySentence(nullptr, 0.f, 1.f, 75, nullptr),
		-1);
	// SEAM: both surviving arms end in the sentence system this runtime has none of, so both
	// answer -1 — the raw-wave `'!'` arm (`10278e57`) and the group arm (`10278f5f`).
	TestEqual(TEXT("the '!' raw-sentence arm answers -1"),
		F.Npc->PlaySentence(TEXT("!SPEECH_LINE"), 0.f, 1.f, 75, nullptr), -1);
	TestEqual(TEXT("the sentence-group arm answers -1"),
		F.Npc->PlaySentence(TEXT("SPI_IDLE"), 0.f, 1.f, 75, nullptr), -1);

	// `0x10279000` is thirty-three bytes and all of them are the forward: slot 485 tail-calls slot
	// 484 with `(name, delay, volume, soundlevel, NULL)`, dropping its own bool and listener. So
	// for every input the two answer the same thing — which is the body's whole content.
	const TCHAR* const Names[] = { nullptr, TEXT("!SPEECH_LINE"), TEXT("SPI_IDLE"), TEXT("") };
	bool bForwards = true;
	for (const TCHAR* Name : Names)
	{
		bForwards &= F.Npc->PlayScriptedSentence(Name, 0.25f, 0.8f, 75, true, nullptr)
			== F.Npc->PlaySentence(Name, 0.25f, 0.8f, 75, nullptr);
	}
	TestTrue(TEXT("PlayScriptedSentence is the forward to PlaySentence and nothing else"),
		bForwards);
	return true;
}

// =================================================================================================
// Slot 509 `ShouldPlayIdleSound` and its base half
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsIdleGateTest,
	"Elysium.Substrate.NpcKernelSounds.IdleGate", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsIdleGateTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}

	// `0x10294040`: the Troika override is the dialogue gate and the delegation, nothing else.
	F.Npc->Dialogue.bInDialog = true;
	TestFalse(TEXT("a body in dialogue never rolls"), F.Npc->ShouldPlayIdleSound());
	F.Npc->Dialogue.bInDialog = false;

	// `0x1027a420`'s five refusals, in retail's order. The spawn state is IDLE, which passes the
	// first one; COMBAT does not (the table at `0x1027e660` orders None, Idle, Combat, Alert, so
	// the admitted pair is 1 and 3).
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestFalse(TEXT("a body in COMBAT never rolls"), F.Npc->BaseShouldPlayIdleSound());
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	// ALERT is admitted; the roll below decides, so only the refusal is asserted here.
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);

	F.Npc->SpawnFlags |= 0x2;
	TestFalse(TEXT("a gagged body never rolls"), F.Npc->BaseShouldPlayIdleSound());
	F.Npc->SpawnFlags &= ~0x2;

	F.Npc->NpcFlags.Set(EElysiumNpcFlag::D_IS_BUSY);
	TestTrue(TEXT("the discipline-busy predicate stands"), F.Npc->IsBusyWithDiscipline());
	TestFalse(TEXT("a body busy with a Discipline never rolls"),
		F.Npc->BaseShouldPlayIdleSound());
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::D_IS_BUSY);

	// The float-sound arm (`1027a4cc`), and it is the one the one-line walk missed: when slot 510
	// says yes the body plays slot 507 `FloatSound()` and RETURNS FALSE. The float sound is played
	// INSTEAD of an idle sound.
	//
	// Slot 510's Troika gates all pass on a spawned idle NPC except the closest-player cache, so
	// the case seeds it, and the base half's `RandomInt(0, 1)` is forced by driving until it hits.
	F.Npc->FloatSoundFrequency = 1;
	F.Npc->NextFloatSoundTime = 0.0;
	F.Npc->Senses.Memory.ClosestPlayer = F.World.World.PlayerHandle();
	F.Npc->Senses.Memory.ClosestPlayerDistanceCm = 10.0f * ElysiumMove::U;   // well inside 50 units
	// Slot 507 was a generated stub when this case was written, so the probe was the
	// `elysium.stubs` tally. Story **29d** (family Sounds10) ported `0x10294f40`, so the probe is
	// now the body's own output: `FloatSound` speaks the VSound concept `"Float"` and re-arms
	// `m_flNextFloatSoundTime`. Same question, answered off the real body.
	bool bFloated = false;
	for (int32 Pass = 0; Pass < 64 && !bFloated; ++Pass)
	{
		F.Npc->VSoundSpeakCalls.Reset();
		const bool bIdle = F.Npc->BaseShouldPlayIdleSound();
		const bool bPlayedFloat = F.Npc->VSoundSpeakCalls.ContainsByPredicate(
			[](const FElysiumNpc::FVSoundSpeak& Row)
			{
				return Row.Concept != nullptr && FCString::Strcmp(Row.Concept, TEXT("Float")) == 0;
			});
		if (bPlayedFloat)
		{
			bFloated = true;
			TestFalse(TEXT("a body that floated does not also vocalise on the same pass"), bIdle);
		}
	}
	TestTrue(TEXT("the idle gate dispatches slot 507 FloatSound when slot 510 says yes"),
		bFloated);
	F.Npc->VSoundSpeakCalls.Reset();

	// With the float hook disabled the gate falls to `RandomInt(0, 999) == 0`: over 400 passes a
	// 1-in-1000 roll must almost never fire, which is what the weight IS.
	F.Npc->FloatSoundFrequency = 0;
	int32 Fired = 0;
	for (int32 Pass = 0; Pass < 400; ++Pass)
	{
		Fired += F.Npc->BaseShouldPlayIdleSound() ? 1 : 0;
	}
	TestTrue(TEXT("RandomInt(0, 999) == 0 fires rarely"), Fired <= 8);

	// The comfort weight (`1027a49a`). With a schedule installed the body translates that
	// schedule's GLOBAL id through slot 447 `GetLocalScheduleId` (`0x101a6620`, which calls
	// `0x102ea280` over the class schedule id space) and compares the answer with `0x12f`
	// `SCHED_TROIKA_COMFORT`. ONLY a match drops the weight to 20, and a match also skips the
	// float-sound arm entirely; every other answer leaves both alone.
	//
	// The translation answers **-1** here, not 0: `0x102ea280` is `-1 stays -1; else the first
	// space whose local base is not the `9999` sentinel and whose range holds the id`, and this
	// runtime parses no schedule text, so every sub-space is the empty sentinel the static
	// constructor left and the walk falls off the end of the chain. -1 is the strongest answer
	// this gate could get — it is not 0, so an untranslatable id can never be read as schedule 0,
	// and it is not `0x12f`, so it can never take the comfort branch by accident.
	F.Npc->Schedule.Current = ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND);
	TestTrue(TEXT("a schedule is installed, so the translation arm is reached at all"),
		F.Npc->Schedule.IsRunning());
	TestEqual(TEXT("slot 447 translates the running schedule's global id to -1, not 0"),
		F.Npc->GetLocalScheduleId(ElysiumSched::IDLE_STAND), -1);
	TestEqual(TEXT("...and 0x12f itself does not translate either, so no program is COMFORT yet"),
		F.Npc->GetLocalScheduleId(0x12f), -1);

	// And the behaviour that turns on it: a running schedule whose id does not translate must
	// leave the gate on the ORDINARY path — weight 999 and the float-sound arm still evaluated.
	// The comfort branch skips that arm, so a float sound firing with a schedule installed is
	// proof the untranslatable id did not take it.
	F.Npc->FloatSoundFrequency = 1;
	F.Npc->NextFloatSoundTime = 0.0;
	bool bFloatedUnderSchedule = false;
	for (int32 Pass = 0; Pass < 64 && !bFloatedUnderSchedule; ++Pass)
	{
		F.Npc->VSoundSpeakCalls.Reset();
		const bool bIdle = F.Npc->BaseShouldPlayIdleSound();
		if (F.Npc->VSoundSpeakCalls.ContainsByPredicate(
			[](const FElysiumNpc::FVSoundSpeak& Row)
			{
				return Row.Concept != nullptr && FCString::Strcmp(Row.Concept, TEXT("Float")) == 0;
			}))
		{
			bFloatedUnderSchedule = true;
			TestFalse(TEXT("...and it still returns false on the pass it floated"), bIdle);
		}
	}
	TestTrue(TEXT("an untranslatable running schedule keeps the float arm, so it is not COMFORT"),
		bFloatedUnderSchedule);

	F.Npc->Schedule.Clear();
	F.Npc->VSoundSpeakCalls.Reset();
	return true;
}

// =================================================================================================
// Slot 510 `ShouldPlayFloatSound` and its base half
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsFloatGateTest,
	"Elysium.Substrate.NpcKernelSounds.FloatGate", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsFloatGateTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}

	// The base half first (`0x1027a530`), because the Troika body tail-calls it.
	F.Npc->NextFloatSoundTime = 0.0;
	F.Npc->FloatSoundFrequency = 0;
	TestFalse(TEXT("floatfreq 0 disables the hook"), F.Npc->BaseShouldPlayFloatSound());
	// Retail tests 0 and 8 as two SEPARATE equalities, so 8 is a hole in the middle of the range
	// rather than a ceiling: 7 and 9 both roll.
	F.Npc->FloatSoundFrequency = 8;
	TestFalse(TEXT("floatfreq 8 also disables it"), F.Npc->BaseShouldPlayFloatSound());
	F.Npc->FloatSoundFrequency = 1;
	F.Npc->NextFloatSoundTime = F.World.World.NowSeconds() + 100.0;
	TestFalse(TEXT("inside the next-float window it refuses"), F.Npc->BaseShouldPlayFloatSound());

	// `RandomInt(0, m_iFloatSoundFrequency) == 0` — "the sound plays 1 time in X". At frequency 1
	// that is one pass in two, so over 400 draws it must fire, must not always fire, and must land
	// near half.
	F.Npc->NextFloatSoundTime = 0.0;
	int32 Fired = 0;
	for (int32 Pass = 0; Pass < 400; ++Pass)
	{
		Fired += F.Npc->BaseShouldPlayFloatSound() ? 1 : 0;
	}
	TestTrue(TEXT("floatfreq 1 rolls about one pass in two"), Fired > 140 && Fired < 260);

	// Frequency 9 is the other side of the 8 hole: it rolls, at one pass in ten.
	F.Npc->FloatSoundFrequency = 9;
	Fired = 0;
	for (int32 Pass = 0; Pass < 400; ++Pass)
	{
		Fired += F.Npc->BaseShouldPlayFloatSound() ? 1 : 0;
	}
	TestTrue(TEXT("floatfreq 9 rolls about one pass in ten"), Fired > 10 && Fired < 90);

	// --- The Troika override's eight gates (`0x10294070`), each refusing on its own ------------
	F.Npc->FloatSoundFrequency = 1;
	F.Npc->NextFloatSoundTime = 0.0;
	F.Npc->Senses.Memory.ClosestPlayer = F.World.World.PlayerHandle();
	F.Npc->Senses.Memory.ClosestPlayerDistanceCm = 10.0f * ElysiumMove::U;

	// Gate 1 (`10294075`).
	F.Npc->Dialogue.bInDialog = true;
	TestFalse(TEXT("gate 1: a body in dialogue never floats"), F.Npc->ShouldPlayFloatSound());
	F.Npc->Dialogue.bInDialog = false;

	// Gate 3 (`102940be`): `m_iMiscFlags & 1`, the name table's `Unconscious`.
	F.Npc->MiscFlags |= 0x1;
	TestFalse(TEXT("gate 3: an unconscious body never floats"), F.Npc->ShouldPlayFloatSound());
	F.Npc->MiscFlags &= ~0x1u;

	// Gate 4 (`102940cb`): `m_bfAINPCFlags & 0x20000`, `SLEEPING`.
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestFalse(TEXT("gate 4: a SLEEPING body never floats"), F.Npc->ShouldPlayFloatSound());
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// Gates 5 and 6 (`102940db`, `102940ee`): BOTH the ideal and the current state must be
	// NPC_STATE_IDLE(1). The one-line walk recorded ALERT for both; `0x1027e660`'s name table
	// orders retail's enum None, Idle, Combat, Alert, so 1 is IDLE.
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	TestFalse(TEXT("gates 5/6: an ALERT body never floats"), F.Npc->ShouldPlayFloatSound());
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestFalse(TEXT("gates 5/6: a COMBAT body never floats"), F.Npc->ShouldPlayFloatSound());
	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);

	// Gate 7 (`102940fa`): a live `m_hClosestPlayer`.
	F.Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	TestFalse(TEXT("gate 7: no closest player, no float"), F.Npc->ShouldPlayFloatSound());
	F.Npc->Senses.Memory.ClosestPlayer = F.World.World.PlayerHandle();

	// The `Float_Sound_Info` distance (row 0, 50.0 Source units): `m_flPlayerDist <= threshold`,
	// and the `AND 0x4100` mask at `102941f3` carries the equal bit, so exactly 50 passes.
	F.Npc->Senses.Memory.ClosestPlayerDistanceCm = 400.0f * ElysiumMove::U;
	TestFalse(TEXT("a player past the Float_Sound_Info distance refuses"),
		F.Npc->ShouldPlayFloatSound());

	// Inside the distance, with every gate open, the Troika body's answer IS the base body's — so
	// it must fire sometimes and refuse sometimes at frequency 1.
	F.Npc->Senses.Memory.ClosestPlayerDistanceCm = 10.0f * ElysiumMove::U;
	Fired = 0;
	for (int32 Pass = 0; Pass < 400; ++Pass)
	{
		Fired += F.Npc->ShouldPlayFloatSound() ? 1 : 0;
	}
	TestTrue(TEXT("with every gate open the Troika body answers the base body"),
		Fired > 140 && Fired < 260);

	// Gate 2 (`10294082`): a LIVE grapple. SEAM in the sense that nothing in this fixture grapples;
	// the predicate is the port's own `IsGrappling()`, the same pair (`+0x1538`, `+0x153c`) the
	// move solver reads.
	TestFalse(TEXT("gate 2: this body is not grappling"), F.Npc->IsGrappling());
	return true;
}

// =================================================================================================
// Slot 511 `StopLoopingSounds`, and `CNPC_VManBat::m_bHasPlayedFlyBySound`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsStopLoopingTest,
	"Elysium.Substrate.NpcKernelSounds.StopLooping", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsStopLoopingTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}

	// `0x1027caa0` is one call: `IEngineSound::vfunc5(engine->IndexOfEdict(edict()), 1)`. The seam
	// answers nothing; what the case asserts is that it was ASKED, for this entity, with retail's
	// own literal second argument.
	F.World.Services.StopEntitySoundRequests.Reset();
	F.Npc->StopLoopingSounds();
	if (TestEqual(TEXT("StopLoopingSounds asks the substrate exactly once"),
		F.World.Services.StopEntitySoundRequests.Num(), 1))
	{
		TestEqual(TEXT("...for this NPC"), F.World.Services.StopEntitySoundRequests[0].Key.Index,
			F.Npc->Handle.Index);
		TestEqual(TEXT("...with retail's literal 1"),
			F.World.Services.StopEntitySoundRequests[0].Value, 1);
	}

	// `FUN_10390040` (`0x10390040`), eight bytes: `*(bool*)(this + 0x66b8) = false`.
	F.Npc->bHasPlayedFlyBySound = true;
	F.Npc->ClearHasPlayedFlyBySound();
	TestFalse(TEXT("the fly-by latch is cleared"), F.Npc->bHasPlayedFlyBySound);
	F.Npc->ClearHasPlayedFlyBySound();
	TestFalse(TEXT("...and clearing it twice is the same write"), F.Npc->bHasPlayedFlyBySound);
	return true;
}

// =================================================================================================
// `CDialog::PlayWhisper` (`0x100e0a40`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSoundsWhisperTest,
	"Elysium.Substrate.NpcKernelSounds.Whisper", GElysiumNpcKernelSoundsFlags)
bool FElysiumNpcKernelSoundsWhisperTest::RunTest(const FString&)
{
	FSoundsFixture F;
	if (!TestNotNull(TEXT("the NPC spawned"), F.Npc))
	{
		return false;
	}

	// An empty queue is retail's first refusal (`100e0a7c`) and touches nothing.
	TestTrue(TEXT("nothing is queued to begin with"), F.Npc->Dialogue.PendingWhisper.IsEmpty());
	F.Npc->Dialogue.PlayWhisper(*F.Npc, TEXT("whispers/crying"));
	TestTrue(TEXT("an empty queue stays empty"), F.Npc->Dialogue.PendingWhisper.IsEmpty());

	// A queued whisper is consumed ONCE: the text is handed to the display seam and cleared, so a
	// second call does nothing. That one-shot is the whole body.
	F.Npc->Dialogue.PendingWhisper = TEXT("The walls are listening.");
	F.Npc->Dialogue.PlayWhisper(*F.Npc, TEXT("whispers/crying"));
	TestTrue(TEXT("a played whisper is cleared"), F.Npc->Dialogue.PendingWhisper.IsEmpty());

	// The clear is UNCONDITIONAL once the text was non-empty: retail resolves the player handle,
	// and a dead one still falls through to the clear rather than leaving the whisper queued.
	F.Npc->Dialogue.PendingWhisper = TEXT("Again.");
	F.Npc->Dialogue.PlayWhisper(*F.Npc, FString());
	TestTrue(TEXT("a whisper with no sound name is still consumed"),
		F.Npc->Dialogue.PendingWhisper.IsEmpty());
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
