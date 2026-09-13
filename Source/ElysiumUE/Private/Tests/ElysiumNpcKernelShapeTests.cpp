#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumStub.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelShapeMap.h"
#include "Tests/ElysiumNpcTestFixture.h"

// The NPC kernel's shape, asserted rather than described.
//
// Content-free: the suite walks the committed census and the committed binding registry and
// requires the recovered shape back. What it catches is a bad regeneration, a hand-edit of a
// generated table, and — the reason the registry exists — a retail word that lost its port member
// without anyone noticing. Whether the member it names is WRITTEN by anything is 29c/29d/29e's
// question, and this file deliberately does not ask it.
//
// No game files and no world: the two committed tables are the whole input.

static constexpr EAutomationTestFlags GElysiumNpcShapeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	bool IsNpcLayer(const FElysiumNpcWord& Word)
	{
		// The NPC's own layers. Everything below them is the entity chain's, which the port carries
		// on `FElysiumEntity`, `FElysiumAnimating` and `FElysiumCombatCharacter`.
		return FCString::Strcmp(Word.Layer, TEXT("CAI_BaseNPC")) == 0
			|| FCString::Strcmp(Word.Layer, TEXT("CAI_BaseNPCTroika")) == 0;
	}

	bool IsTroikaTable(const FElysiumNpcWord& Word)
	{
		return FCString::Strcmp(Word.Table, TEXT("CAI_BaseNPCTroika")) == 0;
	}

	const FElysiumNpcClass* FindClass(const TCHAR* Name)
	{
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			if (FCString::Strcmp(Row.Name, Name) == 0)
			{
				return &Row;
			}
		}
		return nullptr;
	}

	// A per-branch virtual may be introduced by a class the family table does not list: the table
	// is the 77 classes whose own vtable spans the NPC slot range, and `CAI_BaseActor` — which
	// introduces seven of them — is a base one of those derives from rather than one of them.
	bool IsKnownClass(const TCHAR* Name)
	{
		if (FindClass(Name) != nullptr)
		{
			return true;
		}
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			if (FCString::Strcmp(Row.Base, Name) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

// --- The census ---------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelShapeCensusTest,
	"Elysium.Substrate.NpcKernelShape.Census", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelShapeCensusTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelShape;

	const FElysiumNpcShapeCensus& Stored = Census();

	// The round trip: the emitted rows are the rows the generator hashed. A hand-edit of a table
	// disagrees with the stored digest even when every count still adds up.
	{
		const uint64 Digest = DigestOfRows();
		TestTrue(FString::Printf(
			TEXT("the committed rows digest to the recovered stream (0x%016llx against 0x%016llx)"),
			Digest, Stored.RowDigest), Digest == Stored.RowDigest);
	}

	// Words. Each count twice: against the census the generator wrote, and against the literal
	// story 29b landed, so a regeneration that moved both is still visible.
	{
		TestEqual(TEXT("the emission carries the stored word count"), Words().Num(), Stored.Words);
		TestEqual(TEXT("which is 1,141 top-level words"), Words().Num(), 1141);

		int32 Troika = 0;
		int32 Species = 0;
		int32 Npc = 0;
		int32 Interiors = 0;
		int32 Unsettled = 0;
		TSet<FString> Seen;
		for (const FElysiumNpcWord& Word : Words())
		{
			const bool bTroika = IsTroikaTable(Word);
			Troika += bTroika ? 1 : 0;
			Species += bTroika ? 0 : 1;
			Npc += (bTroika && IsNpcLayer(Word)) ? 1 : 0;
			Interiors += Word.Interiors;
			Unsettled += Word.Tier == EElysiumNpcShapeTier::Unsettled ? 1 : 0;

			TestTrue(TEXT("every word names its table, member, type and layer"),
				Word.Table != nullptr && Word.Member != nullptr && Word.Type != nullptr
					&& Word.Layer != nullptr && FCString::Strlen(Word.Member) > 0);
			TestTrue(FString::Printf(TEXT("+0x%04x %s carries a tier"), Word.Offset, Word.Member),
				Word.Tier != EElysiumNpcShapeTier::Count);
			TestTrue(FString::Printf(TEXT("%s +0x%04x is a row once"), Word.Table, Word.Offset),
				!Seen.Contains(FString::Printf(TEXT("%s|%d"), Word.Table, Word.Offset)));
			Seen.Add(FString::Printf(TEXT("%s|%d"), Word.Table, Word.Offset));
		}
		TestEqual(TEXT("the flattened Troika table is 756 words"), Troika, Stored.TroikaWords);
		TestEqual(TEXT("and 756 exactly"), Troika, 756);
		TestEqual(TEXT("the species tables add 385"), Species, Stored.SpeciesWords);
		TestEqual(TEXT("388 of the Troika words are the NPC's own layers"), Npc, Stored.NpcWords);
		TestEqual(TEXT("and 388 exactly"), Npc, 388);
		TestEqual(TEXT("the collapsed interiors are counted on their owner"), Interiors,
			Stored.Interiors);
		// 29b-0's `unsettled` rows carry over as recorded rather than being dropped.
		TestEqual(TEXT("seven top-level words are still unsettled"), Unsettled,
			Stored.UnsettledWords);
		TestEqual(TEXT("which is 29b-0's residue, carried"), Unsettled, 7);
	}

	// Slots.
	{
		TestEqual(TEXT("the emission carries the stored slot count"), Slots().Num(), Stored.Slots);
		TestEqual(TEXT("which is 666 rows"), Slots().Num(), 666);

		TArray<int32> TroikaLine;
		int32 Branch = 0;
		int32 Ported = 0;
		int32 Unsettled = 0;
		for (const FElysiumNpcSlot& Slot : Slots())
		{
			TestTrue(TEXT("every slot names its declaration and the port's callable"),
				Slot.Declaration != nullptr && FCString::Strlen(Slot.Declaration) > 0
					&& Slot.PortMethod != nullptr && FCString::Strlen(Slot.PortMethod) > 0);
			if (Slot.Class != nullptr && FCString::Strlen(Slot.Class) > 0)
			{
				++Branch;
				TestTrue(FString::Printf(
					TEXT("slot %d's branch names a class the registry knows (%s)"), Slot.Slot,
					Slot.Class), IsKnownClass(Slot.Class));
				continue;
			}
			TroikaLine.Add(Slot.Slot);
			Ported += Slot.bPorted ? 1 : 0;
			Unsettled += Slot.Tier == EElysiumNpcShapeTier::Unsettled ? 1 : 0;
			// The story band is derived from the body's layer, so one is present exactly when the
			// other is.
			TestTrue(FString::Printf(TEXT("slot %d's story and layer agree"), Slot.Slot),
				(Slot.Layer >= 0) == (FCString::Strlen(Slot.Story) > 0));
		}

		TestEqual(TEXT("the Troika line is 617 slots"), TroikaLine.Num(), Stored.TroikaSlots);
		TestEqual(TEXT("and 617 exactly — Troika's table, not the base's 583"), TroikaLine.Num(),
			617);
		TestEqual(TEXT("the per-branch virtuals past it are 49"), Branch, Stored.BranchSlots);
		TestEqual(TEXT("the port already implements 32 of the Troika line"), Ported,
			Stored.PortedSlots);
		TestEqual(TEXT("seven slots are still unsettled"), Unsettled, Stored.UnsettledSlots);

		// The verdict columns story 29c added. A row carries a `Default` only where a verdict
		// read the body, and the emission counts both.
		int32 Verdicted = 0;
		int32 Defaults = 0;
		for (const FElysiumNpcSlot& Slot : Slots())
		{
			if (Slot.Class != nullptr && FCString::Strlen(Slot.Class) > 0)
			{
				continue;
			}
			const bool bVerdict = FCString::Strlen(Slot.Verdict) > 0;
			const bool bDefault = FCString::Strlen(Slot.Default) > 0;
			Verdicted += bVerdict ? 1 : 0;
			Defaults += bDefault ? 1 : 0;
			TestTrue(FString::Printf(
				TEXT("slot %d carries a default only where a verdict read the body"), Slot.Slot),
				!bDefault || bVerdict);
		}
		TestEqual(TEXT("the emission carries the stored verdicted-slot count"), Verdicted,
			Stored.VerdictedSlots);
		TestEqual(TEXT("and the stored recovered-default count"), Defaults, Stored.DefaultSlots);

		// Every index once, 0 through 616, in order. A shuffled or gapped emission passes a count.
		TroikaLine.Sort();
		for (int32 Index = 0; Index < TroikaLine.Num(); ++Index)
		{
			TestEqual(TEXT("the Troika line has no gap and no duplicate"), TroikaLine[Index],
				Index);
		}
	}

	// Classes and the species overrides — rows, not subclasses.
	{
		TestEqual(TEXT("the emission carries the stored class count"), Classes().Num(),
			Stored.Classes);
		TestEqual(TEXT("which is the 77-class family"), Classes().Num(), 77);

		int32 Classnames = 0;
		for (const FElysiumNpcClass& Row : Classes())
		{
			TestTrue(TEXT("every class names itself and its direct base"),
				Row.Name != nullptr && FCString::Strlen(Row.Name) > 0 && Row.Base != nullptr);
			TestTrue(FString::Printf(TEXT("%s spans the NPC slot range"), Row.Name),
				Row.Slots >= 580);
			TestTrue(TEXT("a class's classname array and its count agree"),
				(Row.Classnames != nullptr) == (Row.ClassnameCount > 0));
			Classnames += Row.ClassnameCount;
		}
		TestEqual(TEXT("the entity classnames the family claims"), Classnames, Stored.Classnames);

		TestEqual(TEXT("the emission carries the stored override count"), Overrides().Num(),
			Stored.Overrides);
		TestEqual(TEXT("which is 2,344 species slot bodies"), Overrides().Num(), 2344);
		int32 VerdictedOverrides = 0;
		int32 RegistryValues = 0;
		for (const FElysiumNpcClassSlot& Row : Overrides())
		{
			const FElysiumNpcClass* Owner = FindClass(Row.Class);
			if (TestNotNull(TEXT("an override names a class the registry knows"), Owner))
			{
				TestTrue(FString::Printf(TEXT("%s#%d is inside its own table"), Row.Class,
					Row.Slot), Row.Slot < Owner->Slots);
			}
			TestTrue(TEXT("an override names the body that fills the slot"),
				Row.Address != nullptr && FCString::Strlen(Row.Address) > 0);
			const bool bVerdict = FCString::Strlen(Row.Verdict) > 0;
			const bool bValue = FCString::Strlen(Row.Default) > 0;
			VerdictedOverrides += bVerdict ? 1 : 0;
			RegistryValues += bValue ? 1 : 0;
			// The value is read off the body only where a verdict said the body is a class →
			// slot → value row, so a value without a verdict would be an invented constant.
			TestTrue(FString::Printf(TEXT("%s#%d carries a value only under a verdict"), Row.Class,
				Row.Slot), !bValue || bVerdict);
		}
		TestEqual(TEXT("the emission carries the stored verdicted-override count"),
			VerdictedOverrides, Stored.VerdictedOverrides);
		TestEqual(TEXT("and the stored registry-value count"), RegistryValues,
			Stored.RegistryValues);
	}

	return true;
}

// --- The binding registry -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelShapeMapTest,
	"Elysium.Substrate.NpcKernelShape.Map", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelShapeMapTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelShape;

	TMap<int32, const FElysiumNpcWordBinding*> ByOffset;
	for (const FElysiumNpcWordBinding& Row : ElysiumNpcKernelShapeMap::Bindings())
	{
		TestFalse(FString::Printf(TEXT("+0x%04x is bound once"), Row.Offset),
			ByOffset.Contains(Row.Offset));
		ByOffset.Add(Row.Offset, &Row);

		const bool bNamed = Row.Home == EElysiumNpcWordHome::Member
			|| Row.Home == EElysiumNpcWordHome::Chain;
		TestTrue(FString::Printf(TEXT("+0x%04x names a port member exactly when it has one"),
			Row.Offset), bNamed == (Row.PortPath != nullptr));
		if (Row.Home == EElysiumNpcWordHome::Implicit || Row.Home == EElysiumNpcWordHome::Absent)
		{
			// A word with no member has to say why. "Not got to it" is not a reason this file
			// accepts, and an empty note is how that would look.
			TestTrue(FString::Printf(TEXT("+0x%04x says why it has no member"), Row.Offset),
				Row.Note != nullptr && FCString::Strlen(Row.Note) > 0);
		}
	}

	// Every word of the NPC's own layers is bound. This is the whole point: `FElysiumNpc` used to
	// cite about thirty offsets in comments and assert none of them.
	int32 NpcWords = 0;
	int32 Missing = 0;
	for (const FElysiumNpcWord& Word : Words())
	{
		if (!IsTroikaTable(Word) || !IsNpcLayer(Word))
		{
			continue;
		}
		++NpcWords;
		const FElysiumNpcWordBinding* const* Found = ByOffset.Find(Word.Offset);
		if (Found == nullptr)
		{
			++Missing;
			AddError(FString::Printf(TEXT("+0x%04x %s (%s) has no binding"), Word.Offset,
				Word.Member, Word.Type));
			continue;
		}
		const FElysiumNpcWordBinding& Row = **Found;
		// Where the row could take the member's size, it has to be at least the width the datamap
		// declares. Four exclusions, each for a reason rather than to make the case pass: a word
		// the datamap does not size states no width to disagree with; an aggregate (> 8 bytes) is
		// one port member per retail record and their layouts are unrelated; the port deliberately
		// widens retail floats to double, so only a NARROWER port member is a fault; and a row
		// carrying a note has already explained itself — that is what the note column is for, and
		// the deliberate narrowings (a `uint8` enum standing for a retail `int`) all carry one.
		if (Row.PortSize > 0 && Row.Note == nullptr && Word.Size > 0 && Word.Size <= 8
			&& Word.Count == 1)
		{
			TestTrue(FString::Printf(
				TEXT("+0x%04x %s: the port member is at least the retail width (%d against %d)"),
				Word.Offset, Word.Member, Row.PortSize, Word.Size), Row.PortSize >= Word.Size);
		}
	}
	TestEqual(TEXT("the registry covers the NPC's own 388 words"), NpcWords, 388);
	TestEqual(TEXT("with none missing"), Missing, 0);

	// A binding that names no census word would be a row nothing keeps honest.
	for (const TPair<int32, const FElysiumNpcWordBinding*>& Pair : ByOffset)
	{
		bool bFound = false;
		for (const FElysiumNpcWord& Word : Words())
		{
			if (IsTroikaTable(Word) && Word.Offset == Pair.Key)
			{
				bFound = true;
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("+0x%04x is a word of the flattened Troika table"),
			Pair.Key), bFound);
	}

	return true;
}

// --- The recovered slot defaults ------------------------------------------------------------------

// Story 29c's `rule` rows whose whole retail body is one literal.
//
// Those slots' port bodies are generated from the recovered literal rather than written, which
// makes this suite the thing that keeps the two joined: it stands one NPC, calls every generated
// virtual through the probe the generator emitted for it, and requires retail's own answer back.
// It also requires that the call tallied no stub — that is the half that proves the generated body
// replaced the stub rather than sitting beside it.
//
// One suite covers every `default:` row, which is deliberate and is the same argument the story
// makes for species overrides: the literal is data, and data is tested once over its table, not
// once per row.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSlotDefaultsTest,
	"Elysium.Substrate.NpcKernelSlots.Defaults", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelSlotDefaultsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_slot_defaults"), 29003u);
	Builder.AddNpc(TEXT("subject"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the fixture stands one NPC"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({Npc});

	// The slot rows say which slots carry a default; the probe table has to agree with them, or a
	// row was emitted into one and not the other.
	TSet<int32> Declared;
	for (const FElysiumNpcSlot& Slot : ElysiumNpcKernelShape::Slots())
	{
		if ((Slot.Class == nullptr || FCString::Strlen(Slot.Class) == 0)
			&& FCString::Strlen(Slot.Default) > 0)
		{
			Declared.Add(Slot.Slot);
		}
	}

	TArrayView<const FElysiumNpcSlotDefault> Rows = ElysiumNpcKernelShape::SlotDefaults();
	TestEqual(TEXT("every slot the census gives a default has a probe"), Rows.Num(),
		Declared.Num());

	ElysiumStub::ClearTally();
	for (const FElysiumNpcSlotDefault& Row : Rows)
	{
		if (!TestNotNull(FString::Printf(TEXT("slot %d carries a probe"), Row.Slot), Row.Invoke))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("slot %d's probe is a slot the census declared"), Row.Slot),
			Declared.Contains(Row.Slot));
		const int64 Answer = Row.Invoke(*Npc);
		if (!Row.bVoid)
		{
			TestEqual(FString::Printf(TEXT("slot %d (%s, %s) answers retail's %s"), Row.Slot,
				Row.Address, Row.PortMethod, Row.Retail), Answer, Row.Value);
		}
	}

	// Not one of them may report itself unimplemented: a `default:` verdict says retail's answer
	// is recovered, and a stub firing would mean the generator emitted the comment without the
	// body.
	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	for (const ElysiumStub::FTally& Fired : Tally)
	{
		AddError(FString::Printf(TEXT("a recovered default still tallies a stub: %s %s (%s)"),
			*Fired.Surface, *Fired.Address, *Fired.Story));
	}
	TestEqual(TEXT("calling every recovered default tallies no stub"), Tally.Num(), 0);
	ElysiumStub::ClearTally();

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
