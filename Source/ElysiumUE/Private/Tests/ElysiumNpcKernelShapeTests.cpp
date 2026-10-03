#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Find.h"
#include "ElysiumAnimatingOverlay.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumFlex.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelShapeMap.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcKernelOverrideCensus.h"
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
			// A slot with no port callable is a dead stub deleted in story 5 step 6 (`SLOT_PORT_MAP`
			// `DELETED`), or a row 0019/6 closed with no dispatch site: its verdict is `dead` or
			// `mechanism` (closed at a service word), and nothing else may lose its callable.
			const bool bDeleted = Slot.PortMethod != nullptr && FCString::Strlen(Slot.PortMethod) == 0
				&& Slot.Verdict != nullptr && (FCString::Strcmp(Slot.Verdict, TEXT("dead")) == 0
					|| FCString::Strcmp(Slot.Verdict, TEXT("mechanism")) == 0);
			TestTrue(TEXT("every slot names its declaration and the port's callable"),
				Slot.Declaration != nullptr && FCString::Strlen(Slot.Declaration) > 0
					&& Slot.PortMethod != nullptr && (FCString::Strlen(Slot.PortMethod) > 0 || bDeleted));
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

// --- The per-class slot tables --------------------------------------------------------------------

namespace
{
	// The retail class each port class stands for (0019 story 5 step 6). The port has no
	// `CBaseToggle` node; its bodies stand on `FElysiumAnimating`, the nearest port class below it.
	const TCHAR* PortClassOf(const TCHAR* Retail)
	{
		static const TCHAR* const Map[][2] =
		{
			{ TEXT("CBaseEntity"), TEXT("FElysiumEntity") },
			{ TEXT("CBaseToggle"), TEXT("FElysiumAnimating") },
			{ TEXT("CBaseAnimating"), TEXT("FElysiumAnimating") },
			{ TEXT("CBaseAnimatingOverlay"), TEXT("FElysiumAnimatingOverlay") },
			{ TEXT("CBaseFlex"), TEXT("FElysiumFlex") },
			{ TEXT("CBaseCombatCharacter"), TEXT("FElysiumCombatCharacter") },
			{ TEXT("CAI_BaseNPC"), TEXT("FElysiumNpcBase") },
			{ TEXT("CAI_BaseNPCTroika"), TEXT("FElysiumNpc") },
		};
		for (const TCHAR* const* Row : Map)
		{
			if (FCString::Strcmp(Row[0], Retail) == 0)
			{
				return Row[1];
			}
		}
		return nullptr;
	}

	// The registry descriptor that stands for a retail layer.
	FName DescriptorOf(const TCHAR* Retail)
	{
		return FCString::Strcmp(Retail, TEXT("CBaseToggle")) == 0 ? FName(TEXT("CBaseAnimating"))
			: FName(Retail);
	}

	// Receivers for the defaults probes: a bare instance of each port class, so a virtual call runs
	// exactly that class's own body. The pattern is `FElysiumActivatePassProbe`'s.
	class FSlotProbeEntity final : public FElysiumEntity {};
	class FSlotProbeAnimating final : public FElysiumAnimating {};
	class FSlotProbeAnimatingOverlay final : public FElysiumAnimatingOverlay {};
	class FSlotProbeFlex final : public FElysiumFlex {};
	class FSlotProbeCombatCharacter final : public FElysiumCombatCharacter {};
	// A base-only NPC (story 5 step 5): the interface's four pure hooks answer the inert value.
	class FSlotProbeNpcBase final : public FElysiumNpcBase
	{
	public:
	};

	using FSlotKey = TPair<int32, FString>;

	// Calls every `Default` row of one class's table on a receiver of that class and requires
	// retail's literal back; records each probed (slot, body).
	template <typename TReceiver>
	void ProbeDefaults(FAutomationTestBase& Test, const TCHAR* PortClass,
		TArrayView<const TElysiumNpcSlotRow<TReceiver>> Rows, TReceiver& Receiver, TSet<FSlotKey>& OutProbed)
	{
		for (const TElysiumNpcSlotRow<TReceiver>& Row : Rows)
		{
			if (Row.Body != EElysiumNpcSlotBody::Default)
			{
				continue;
			}
			OutProbed.Add(FSlotKey(Row.Slot, Row.Address));
			if (!Test.TestNotNull(FString::Printf(TEXT("%s slot %d carries a probe"), PortClass, Row.Slot),
				Row.Invoke))
			{
				continue;
			}
			const int64 Answer = Row.Invoke(Receiver);
			if (!Row.bVoid)
			{
				Test.TestEqual(FString::Printf(TEXT("%s slot %d (%s, %s) answers retail's %s"), PortClass,
					Row.Slot, Row.Address, Row.PortMethod, Row.Default), Answer, Row.Value);
			}
		}
	}

	// One class's table against the chain it models: every row is declared on the class itself,
	// names a retail owner the class stands for, and overrides exactly when a more-base class
	// declares the slot. `InOutBaseSlots` carries the slots the more-base tables declared.
	template <typename TReceiver>
	void CheckSlotTable(FAutomationTestBase& Test, const TCHAR* PortClass,
		TArrayView<const TElysiumNpcSlotRow<TReceiver>> Rows, TSet<int32>& InOutBaseSlots,
		TSet<FSlotKey>& OutRows)
	{
		Test.TestTrue(FString::Printf(TEXT("%s declares generated slots"), PortClass), Rows.Num() > 0);
		TSet<int32> Own;
		for (const TElysiumNpcSlotRow<TReceiver>& Row : Rows)
		{
			const FString What = FString::Printf(TEXT("%s slot %d %s (%s)"), PortClass, Row.Slot,
				Row.PortMethod, Row.Address);
			Test.TestTrue(What + TEXT(" is declared on the class itself"), Row.bDeclaredHere);
			const TCHAR* Owner = PortClassOf(Row.Retail);
			// A stub tallies under `Retail::PortMethod`, so this is also the owner prefix of its text.
			Test.TestTrue(What + FString::Printf(TEXT(": its body's owner %s is this class's retail node"),
				Row.Retail), Owner != nullptr && FCString::Strcmp(Owner, PortClass) == 0);
			Test.TestEqual(What + TEXT(" overrides exactly when a more-base class declares the slot"),
				Row.bOverride, InOutBaseSlots.Contains(Row.Slot));
			Test.TestEqual(What + TEXT(": a probe exactly on a default row"), Row.Invoke != nullptr,
				Row.Body == EElysiumNpcSlotBody::Default);
			Own.Add(Row.Slot);
			OutRows.Add(FSlotKey(Row.Slot, Row.Address));
		}
		InOutBaseSlots.Append(Own);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSlotOwnersTest,
	"Elysium.Substrate.NpcKernelShape.SlotOwners", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelSlotOwnersTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelShape;

	// Base first, so each table sees the slots its bases declared.
	TSet<int32> BaseSlots;
	TSet<FSlotKey> Rows;
	CheckSlotTable(*this, TEXT("FElysiumEntity"), EntitySlotRows(), BaseSlots, Rows);
	CheckSlotTable(*this, TEXT("FElysiumAnimating"), AnimatingSlotRows(), BaseSlots, Rows);
	CheckSlotTable(*this, TEXT("FElysiumAnimatingOverlay"), AnimatingOverlaySlotRows(), BaseSlots, Rows);
	CheckSlotTable(*this, TEXT("FElysiumFlex"), FlexSlotRows(), BaseSlots, Rows);
	CheckSlotTable(*this, TEXT("FElysiumCombatCharacter"), CombatCharacterSlotRows(), BaseSlots, Rows);
	CheckSlotTable(*this, TEXT("FElysiumNpcBase"), NpcBaseSlotRows(), BaseSlots, Rows);
	CheckSlotTable(*this, TEXT("FElysiumNpc"), NpcSlotRows(), BaseSlots, Rows);

	// Every generated body a Troika instance runs is a row of the class that owns it. A slot the
	// port implements elsewhere (`bPorted`, e.g. 118 `AcceptInput` on the world's chokepoint) and a
	// deleted dead stub (582) carry no generated row.
	int32 Joined = 0;
	for (const FElysiumNpcSlot& Slot : Slots())
	{
		if ((Slot.Class != nullptr && FCString::Strlen(Slot.Class) > 0) || Slot.bPorted
			|| FCString::Strlen(Slot.PortMethod) == 0 || FCString::Strlen(Slot.Address) == 0)
		{
			continue;
		}
		++Joined;
		TestTrue(FString::Printf(TEXT("slot %d %s (%s) is a row of its owner's table"), Slot.Slot,
			Slot.PortMethod, Slot.Address), Rows.Contains(FSlotKey(Slot.Slot, Slot.Address)));
	}
	TestTrue(TEXT("the census has generated Troika rows to join"), Joined > 0);
	return true;
}

// One override per ported (class, slot) own-body row (0019 story 5 commit B, the census clause of the
// story's job). The rows are the generated override census; each carries two proofs evaluated at
// compile time. A method name that no longer exists anywhere stops the build; an override deleted
// from its class while a base still declares the name reads `bDeclared` false, which this case
// catches at run time; and `gen_kernel_shape --check` catches a stale table. This case also holds
// the table to the census: every row is a live own-body row of a live class and stands on the class
// its factory builds; the ported rows plus `kernel_shape --unported`'s `no-override` rows are the
// whole live set.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelOverridesTest,
	"Elysium.Substrate.NpcKernelShape.Overrides", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelOverridesTest::RunTest(const FString&)
{
	const TArrayView<const FElysiumNpcPortedOverride> Rows = ElysiumNpcKernelOverrideCensus::Rows();
	TestTrue(TEXT("the port carries overrides"), Rows.Num() > 0);
	TestTrue(TEXT("and no more than the census's live own-body rows"),
		Rows.Num() <= ElysiumNpcKernelOverrideCensus::LiveRows());
	TSet<FString> Seen;
	for (const FElysiumNpcPortedOverride& Row : Rows)
	{
		const FString What = FString::Printf(TEXT("%s slot %d (%s) as %s::%s"), Row.Class, Row.Slot,
			Row.Address, Row.DeclaringClass, Row.Method);
		TestTrue(What + TEXT(" is declared on its class with the recorded signature"), Row.bDeclared);
		TestTrue(What + FString::Printf(TEXT(" is inherited by %s"), Row.PortClass), Row.bInherits);
		const FElysiumNpcClass* Own = Row.PortRow != nullptr ? Row.PortRow() : nullptr;
		TestTrue(What + TEXT(": its port class answers the row's retail class"),
			Own != nullptr && FCString::Strcmp(Own->Name, Row.Class) == 0);
		TestFalse(What + TEXT(" is on a live class"), ElysiumNpcDeadClasses::Contains(Row.Class));
		const bool bCensus = Algo::FindByPredicate(ElysiumNpcKernelShape::Overrides(),
			[&Row](const FElysiumNpcClassSlot& Census)
			{
				return Census.Slot == Row.Slot && FCString::Strcmp(Census.Class, Row.Class) == 0
					&& FCString::Strcmp(Census.Address, Row.Address) == 0
					&& FCString::Strcmp(Census.Verdict, Row.Verdict) == 0;
			}) != nullptr;
		TestTrue(What + TEXT(" is a census own-body row with its verdict"), bCensus);
		const FString Key = FString::Printf(TEXT("%s#%d"), Row.Class, Row.Slot);
		TestFalse(What + TEXT(" is listed once"), Seen.Contains(Key));
		Seen.Add(Key);
	}
	return true;
}

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFieldOwnersTest,
	"Elysium.Arm.NpcKernelShape.FieldOwners", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelFieldOwnersTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelShape;

	// The storage each shape-map owner type is: a port class, or a component the named class holds.
	// Held components per `gen_kernel_bindings.py` `NPC_COMPONENT_PATHS` / `BASE_COMPONENTS`.
	static const TCHAR* const Holders[][2] =
	{
		{ TEXT("FElysiumEntity"), TEXT("FElysiumEntity") },
		{ TEXT("FElysiumAnimating"), TEXT("FElysiumAnimating") },
		{ TEXT("FElysiumAnimatingOverlay"), TEXT("FElysiumAnimatingOverlay") },
		{ TEXT("FElysiumFlex"), TEXT("FElysiumFlex") },
		{ TEXT("FElysiumCombatCharacter"), TEXT("FElysiumCombatCharacter") },
		{ TEXT("FElysiumNpcFlags"), TEXT("FElysiumCombatCharacter") },
		{ TEXT("FElysiumNpcBase"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumNpcBaseScheduleHost"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumNpcBaseMemory"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumNpcMind"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumNpcEnemyMemory"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumNpcCognition"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumScheduleState"), TEXT("FElysiumNpcBase") },
		{ TEXT("FElysiumNpc"), TEXT("FElysiumNpc") },
		{ TEXT("FElysiumNpcScheduleHost"), TEXT("FElysiumNpc") },
		{ TEXT("FElysiumNpcSenses"), TEXT("FElysiumNpc") },
		{ TEXT("FElysiumNpcPerception"), TEXT("FElysiumNpc") },
		{ TEXT("FElysiumNpcMemory"), TEXT("FElysiumNpc") },
		{ TEXT("FElysiumNpcWitness"), TEXT("FElysiumNpc") },
		{ TEXT("FElysiumNpcDialogue"), TEXT("FElysiumNpc") },
	};
	auto HolderOf = [](const FString& Type) -> const TCHAR*
	{
		for (const TCHAR* const* Row : Holders)
		{
			if (Type == Row[0])
			{
				return Row[1];
			}
		}
		return nullptr;
	};
	auto WordAt = [](int32 Offset) -> const FElysiumNpcWord*
	{
		for (const FElysiumNpcWord& Word : Words())
		{
			if (IsTroikaTable(Word) && Word.Offset == Offset)
			{
				return &Word;
			}
		}
		return nullptr;
	};

	// No word is stored away from its owner any more: `m_pSenses` (`+0x5cdc`), the last transitional
	// home, moved to `FElysiumNpcBase` in story 5 fold A3.

	// The shape map: each word the NPC stores is a member of the port class of its declaring retail
	// class (or of a component that class holds). A `Chain` row is a word the port carries beside
	// the NPC's storage, so it must not name NPC storage; one naming a chain class names its own.
	int32 Members = 0;
	for (const FElysiumNpcWordBinding& Row : ElysiumNpcKernelShapeMap::Bindings())
	{
		if (Row.PortPath == nullptr)
		{
			continue;
		}
		const FElysiumNpcWord* Word = WordAt(Row.Offset);
		if (!TestNotNull(FString::Printf(TEXT("+0x%04x is a census word"), Row.Offset), Word))
		{
			continue;
		}
		FString Type;
		FString Member;
		FString(Row.PortPath).Split(TEXT("::"), &Type, &Member);
		const TCHAR* Holder = HolderOf(Type);
		const TCHAR* Expected = PortClassOf(Word->Layer);
		if (Row.Home == EElysiumNpcWordHome::Member)
		{
			++Members;
			TestTrue(FString::Printf(TEXT("+0x%04x %s (%s) is stored on %s, not %s"), Row.Offset, Word->Member,
				Word->Layer, Expected, Holder != nullptr ? Holder : *Type),
				Holder != nullptr && Expected != nullptr && FCString::Strcmp(Holder, Expected) == 0);
		}
		else if (Row.Home == EElysiumNpcWordHome::Chain && Holder != nullptr)
		{
			const bool bNpcStorage = FCString::Strcmp(Holder, TEXT("FElysiumNpcBase")) == 0
				|| FCString::Strcmp(Holder, TEXT("FElysiumNpc")) == 0;
			TestFalse(FString::Printf(TEXT("+0x%04x %s: a chain row does not name NPC storage (%s)"),
				Row.Offset, Word->Member, *Type), bNpcStorage);
			if (!IsNpcLayer(*Word))
			{
				TestTrue(FString::Printf(TEXT("+0x%04x %s: a chain word is its own layer's (%s)"), Row.Offset,
					Word->Member, Word->Layer), Expected != nullptr && FCString::Strcmp(Holder, Expected) == 0);
			}
		}
	}
	TestTrue(TEXT("the shape map binds member words"), Members > 0);

	// The registry: every census word the Troika's descriptor chain binds by its datamap name is
	// bound on the descriptor of its declaring layer, down the whole chain (CBaseEntity up to the
	// Troika), never copied onto another.
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	int32 Bound = 0;
	for (const FElysiumClassDesc* Desc = Reg.Find(FName(TEXT("CAI_BaseNPCTroika"))); Desc != nullptr;
		Desc = Desc->BaseName.IsNone() ? nullptr : Reg.Find(Desc->BaseName))
	{
		for (const TPair<FName, FElysiumFieldAccessor>& Field : Desc->Fields)
		{
			const FString Name = Field.Key.ToString();
			bool bCensus = false;
			bool bOwnLayer = false;
			FString Layers;
			for (const FElysiumNpcWord& Word : Words())
			{
				if (IsTroikaTable(Word) && Name == Word.Member)
				{
					bCensus = true;
					bOwnLayer |= DescriptorOf(Word.Layer) == Desc->ClassName;
					Layers += FString(Layers.IsEmpty() ? TEXT("") : TEXT(" ")) + Word.Layer;
				}
			}
			if (!bCensus)
			{
				continue;
			}
			++Bound;
			TestTrue(FString::Printf(TEXT("%s is bound on %s, the descriptor of its layer (%s)"), *Name,
				*Desc->ClassName.ToString(), *Layers), bOwnLayer);
		}
	}
	TestTrue(TEXT("the descriptor chain binds census words"), Bound > 0);
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// --- The recovered slot defaults ------------------------------------------------------------------

// Story 29c's `rule` rows whose whole retail body is one literal.
//
// Those slots' port bodies are generated from the recovered literal rather than written, which
// makes this suite the thing that keeps the two joined: it calls every generated default through
// the probe its class's slot table carries, on a receiver of exactly that class, and requires
// retail's own answer back.
//
// One suite covers every `default:` row, which is deliberate and is the same argument the story
// makes for species overrides: the literal is data, and data is tested once over its table, not
// once per row. The receivers are one bare instance per chain class, a base-only NPC for
// `CAI_BaseNPC`'s own bodies (step 5's typed probe) and a Troika NPC for the Troika's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSlotDefaultsTest,
	"Elysium.Substrate.NpcKernelSlots.Defaults", GElysiumNpcShapeFlags)
bool FElysiumNpcKernelSlotDefaultsTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelShape;

	FElysiumNpcWorldBuilder Builder(TEXT("kernel_slot_defaults"), 29003u);
	Builder.AddNpc(TEXT("subject"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the fixture stands one NPC"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({Npc});

	FSlotProbeEntity Entity;
	FSlotProbeAnimating Animating;
	FSlotProbeAnimatingOverlay Overlay;
	FSlotProbeFlex Flex;
	FSlotProbeCombatCharacter Combat;
	FSlotProbeNpcBase Base;

	ElysiumStub::ClearTally();
	TSet<FSlotKey> Probed;
	ProbeDefaults<FElysiumEntity>(*this, TEXT("FElysiumEntity"), EntitySlotRows(), Entity, Probed);
	ProbeDefaults<FElysiumAnimating>(*this, TEXT("FElysiumAnimating"), AnimatingSlotRows(), Animating, Probed);
	ProbeDefaults<FElysiumAnimatingOverlay>(*this, TEXT("FElysiumAnimatingOverlay"), AnimatingOverlaySlotRows(),
		Overlay, Probed);
	ProbeDefaults<FElysiumFlex>(*this, TEXT("FElysiumFlex"), FlexSlotRows(), Flex, Probed);
	ProbeDefaults<FElysiumCombatCharacter>(*this, TEXT("FElysiumCombatCharacter"), CombatCharacterSlotRows(),
		Combat, Probed);
	ProbeDefaults<FElysiumNpcBase>(*this, TEXT("FElysiumNpcBase"), NpcBaseSlotRows(), Base, Probed);
	ProbeDefaults<FElysiumNpc>(*this, TEXT("FElysiumNpc"), NpcSlotRows(), *Npc, Probed);

	// Every default the census gives the Troika table (the body a Troika instance runs) is one of
	// the probed rows; the rest are a class's own constant body under a more-derived one.
	int32 Declared = 0;
	for (const FElysiumNpcSlot& Slot : Slots())
	{
		if ((Slot.Class == nullptr || FCString::Strlen(Slot.Class) == 0)
			&& FCString::Strlen(Slot.Default) > 0)
		{
			++Declared;
			TestTrue(FString::Printf(TEXT("slot %d (%s)'s default is probed on its owner"), Slot.Slot,
				Slot.Address), Probed.Contains(FSlotKey(Slot.Slot, Slot.Address)));
		}
	}
	TestTrue(TEXT("the census gives defaults"), Declared > 0);
	TestTrue(TEXT("and the tables probe at least those"), Probed.Num() >= Declared);

	ElysiumStub::ClearTally();

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
