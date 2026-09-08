// Content-free Substrate automation: dialogue parsing, branching, automatic rows, starting lines, and CPython writers.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumDlgSheet.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumDialogueTestHelpers.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumDialogueTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

namespace ElysiumDialogueTestHelpers
{
	// Build one 13-field `.dlg` row in the on-disk shape (`{ TAB content TAB }` concatenated) from the
	// fields the tests care about; cols 6-11 are empty. Handy so the fixtures read like the data.
	FString ElysiumDlgRow(int32 Id, const FString& Text, const FString& Link,
		const FString& Cond, const FString& Action, const FString& Malk)
	{
		auto F = [](const FString& S) { return FString::Printf(TEXT("{\t%s\t}"), *S); };
		FString R;
		R += F(FString::FromInt(Id)); // 0
		R += F(Text);                 // 1 male
		R += F(Text);                 // 2 female (same)
		R += F(Link);                 // 3
		R += F(Cond);                 // 4
		R += F(Action);               // 5
		for (int32 i = 6; i <= 11; ++i) { R += F(FString()); }
		R += F(Malk);                 // 12 — Malkavian-PC variant
		return R;
	}

	TArray<uint8> ElysiumDlgBytes(const TArray<FString>& Rows)
	{
		FString Joined = FString::Join(Rows, TEXT("\r\n")) + TEXT("\r\n");
		TArray<uint8> Bytes;
		Bytes.Reserve(Joined.Len());
		for (const TCHAR C : Joined) { Bytes.Add(static_cast<uint8>(C)); }   // Latin-1 round-trip
		return Bytes;
	}
}

namespace ElysiumDialogueTests
{
using ElysiumDialogueTestHelpers::ElysiumDlgBytes;
using ElysiumDialogueTestHelpers::ElysiumDlgRow;


// 9.1 / B4 — `.dlg` parser, the dlgexpr normalizer, and the branch state machine. All
// content-free: a synthetic in-memory `.dlg` and injected condition/action callbacks, so
// the branch logic is tested independently of both the normalizer and the script host.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgParseTest, "Elysium.Substrate.DlgParse", GElysiumTestFlags)
bool FElysiumDlgParseTest::RunTest(const FString&)
{
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(11, TEXT("Greeting."), TEXT("#"), TEXT("G.Story_State = -3"), TEXT("")));
	Rows.Add(ElysiumDlgRow(12, TEXT("Who are you?"), TEXT("21"), TEXT("not IsClan(pc,\"Malkavian\")"), TEXT(""), TEXT("The rain of ages?")));
	Rows.Add(ElysiumDlgRow(13, TEXT("Padding"), TEXT(""), TEXT(""), TEXT("")));   // empty link = padding

	FElysiumDlgFile File;
	TestTrue(TEXT("parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File));
	TestEqual(TEXT("row count"), File.Lines.Num(), 3);

	const FElysiumDlgLine* Npc = File.FindById(11);
	if (TestNotNull(TEXT("finds id 11"), Npc))
	{
		TestTrue(TEXT("11 is an NPC line"), Npc->IsNpcLine());
		TestEqual(TEXT("11 text"), Npc->Text(true), FString(TEXT("Greeting.")));
		TestEqual(TEXT("11 col-4 kept raw"), Npc->Condition, FString(TEXT("G.Story_State = -3")));
	}
	const FElysiumDlgLine* Pc = File.FindById(12);
	if (TestNotNull(TEXT("finds id 12"), Pc))
	{
		TestTrue(TEXT("12 is a PC choice"), Pc->IsPcChoice());
		TestEqual(TEXT("12 link target"), Pc->LinkTarget(), 21);
		// col-12 is the Malkavian variant: a non-Malkavian sees col-1, a Malkavian sees col-12.
		TestEqual(TEXT("12 normal text (non-malk)"), Pc->RawFor(true, false), FString(TEXT("Who are you?")));
		TestEqual(TEXT("12 malkavian variant"), Pc->RawFor(true, true), FString(TEXT("The rain of ages?")));
	}
	TestTrue(TEXT("13 is padding"), File.FindById(13)->Role == EElysiumDlgRole::Padding);

	// 14-field tolerance (the kiki.dlg typo): a valid 13-field prefix parses, the extra is ignored.
	FString FourteenField = ElysiumDlgRow(1, TEXT("hi"), TEXT("#"), TEXT(""), TEXT("")) + TEXT("{\textra\t}");
	FElysiumDlgFile Wide;
	TestTrue(TEXT("14-field row parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({ FourteenField }), Wide));
	TestEqual(TEXT("14-field row yields one line"), Wide.Lines.Num(), 1);
	TestEqual(TEXT("14-field text intact"), Wide.Lines[0].Text(true), FString(TEXT("hi")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgExprTest, "Elysium.Substrate.DlgExpr", GElysiumTestFlags)
bool FElysiumDlgExprTest::RunTest(const FString&)
{
	using namespace ElysiumDlgExpr;

	// A skill-only condition: implicit `>=`, wrapped in CalcFeat. The receiver is explicit, because
	// `CalcFeat` is a Character method — a bare call resolves to nothing in either host.
	TestEqual(TEXT("bare skillcheck"), ConditionToPython(TEXT("Seduction 7")),
		FString(TEXT("pc.CalcFeat(\"Seduction\") >= 7")));
	// Explicit relop preserved.
	TestEqual(TEXT("skillcheck relop"), ConditionToPython(TEXT("Humanity >= 5")),
		FString(TEXT("pc.CalcFeat(\"Humanity\") >= 5")));
	// Skillcheck joined to a python expr by `&` -> `and`.
	TestEqual(TEXT("skillcheck & expr"), ConditionToPython(TEXT("Seduction 7 & G.Johnny_Dead == 0")),
		FString(TEXT("pc.CalcFeat(\"Seduction\") >= 7 and G.Johnny_Dead == 0")));
	// `|` -> `or`.
	TestEqual(TEXT("pipe -> or"), ConditionToPython(TEXT("Persuasion 7 | G.x == 1")),
		FString(TEXT("pc.CalcFeat(\"Persuasion\") >= 7 or G.x == 1")));
	// The `M_`/`F_` prefix is the engine dependency's SEX GATE, not part of the trait name — the
	// corpus authors the same beat twice, once per sex, with different lines (`cal.dlg`).
	TestEqual(TEXT("male-gated skillcheck"), ConditionToPython(TEXT("M_Persuasion 3")),
		FString(TEXT("(pc.IsMale() and pc.CalcFeat(\"Persuasion\") >= 3)")));
	TestEqual(TEXT("female-gated skillcheck"), ConditionToPython(TEXT("F_Seduction 8 & G.x == 0")),
		FString(TEXT("(not pc.IsMale() and pc.CalcFeat(\"Seduction\") >= 8) and G.x == 0")));
	// A trait that merely STARTS with a letter and an underscore is not gated.
	TestEqual(TEXT("an ordinary underscored trait is untouched"),
		ConditionToPython(TEXT("Max_Health 5")),
		FString(TEXT("pc.CalcFeat(\"Max_Health\") >= 5")));
	// A pure python condition (no skillcheck) round-trips (normalised spacing).
	TestEqual(TEXT("pure python"), ConditionToPython(TEXT("G.Patch_Plus == 0")),
		FString(TEXT("G.Patch_Plus == 0")));
	// A member/call ident that happens to precede a number is NOT a skillcheck.
	TestEqual(TEXT("member not skillcheck"), ConditionToPython(TEXT("pc.humanity >= 5")),
		FString(TEXT("pc.humanity >= 5")));
	TestEqual(TEXT("call not skillcheck"), ConditionToPython(TEXT("OneOfSet(1,4)")),
		FString(TEXT("OneOfSet(1,4)")));
	// Empty -> empty.
	TestEqual(TEXT("empty condition"), ConditionToPython(TEXT("")), FString());

	// Actions: `&` between statements -> `;`; a lone assignment round-trips.
	TestEqual(TEXT("action assign"), ActionToPython(TEXT("G.Tut_Jack = 1")),
		FString(TEXT("G.Tut_Jack = 1")));
	TestEqual(TEXT("action &-join -> ;"), ActionToPython(TEXT("G.a = 1 & G.b = 2")),
		FString(TEXT("G.a = 1 ; G.b = 2")));

	return true;
}

// D1/D2/D4 — the col-4 dependency object, the retail turn rules and the seven clan columns.
// Included by ElysiumDialogueTests.cpp inside its namespace; kept in its own file so the
// dependency fixtures do not swell the branch-machine tests.

// The two injected interfaces, faked. Nothing here loads a table or touches a world: the whole
// point of `IElysiumDlgTraitResolver` / `IElysiumDlgSheet` is that the parser and the machine can
// be driven from a struct literal.
struct FFakeDlgCharacter
{
	struct FTrait
	{
		EElysiumDlgTraitClass Class = EElysiumDlgTraitClass::Unknown;
		int32 Id = 0;
		int32 Value = 0;
	};

	TMap<FString, FTrait> Traits;
	int32 Blood = 10;
	bool bMale = true;
	int32 Clan = ElysiumDlgClan::None;

	// What `Charge` did.
	int32 BloodSpent = 0;
	TArray<FString> FakedEffects;

	FFakeDlgCharacter& Add(const TCHAR* Name, EElysiumDlgTraitClass Class, int32 Id, int32 Value)
	{
		Traits.Add(FString(Name), { Class, Id, Value });
		return *this;
	}

	// The shipped vocabulary of the 147 files, with the real class of each: `Persuasion` and
	// `Seduction` are feats, `Intimidate` is a feat (the ABILITY is spelled `Intimidation`),
	// `Humanity`/`Appearance`/`Wits` are attributes, `Brawl`/`Firearms` are abilities, and
	// `Dominate` is discipline slot 6 — the slot `TestSimple` gates on Ventrue.
	static FFakeDlgCharacter Shipped()
	{
		FFakeDlgCharacter C;
		C.Add(TEXT("Humanity"),    EElysiumDlgTraitClass::Attribute, 27, 7)
		 .Add(TEXT("Appearance"),  EElysiumDlgTraitClass::Attribute, 6, 3)
		 .Add(TEXT("Wits"),        EElysiumDlgTraitClass::Attribute, 9, 2)
		 .Add(TEXT("Brawl"),       EElysiumDlgTraitClass::Ability, 1, 2)
		 .Add(TEXT("Firearms"),    EElysiumDlgTraitClass::Ability, 5, 1)
		 .Add(TEXT("Dominate"),    EElysiumDlgTraitClass::Discipline, 6, 2)
		 .Add(TEXT("Dementation"), EElysiumDlgTraitClass::Discipline, 5, 0)
		 .Add(TEXT("Presence"),    EElysiumDlgTraitClass::Discipline, 9, 0)
		 .Add(TEXT("Persuasion"),  EElysiumDlgTraitClass::Feat, 7, 4)
		 .Add(TEXT("Seduction"),   EElysiumDlgTraitClass::Feat, 8, 3)
		 .Add(TEXT("Intimidate"),  EElysiumDlgTraitClass::Feat, 6, 7)
		 .Add(TEXT("Haggle"),      EElysiumDlgTraitClass::Feat, 5, 0)
		 .Add(TEXT("Frenzy_Feat"), EElysiumDlgTraitClass::Feat, 0x16, 0);
		return C;
	}
};

class FFakeDlgResolver final : public IElysiumDlgTraitResolver
{
public:
	explicit FFakeDlgResolver(const FFakeDlgCharacter& InChar) : Char(&InChar) {}

	virtual bool ResolveTrait(const FString& Name, EElysiumDlgTraitClass& OutClass,
		int32& OutId) const override
	{
		if (const FFakeDlgCharacter::FTrait* T = Char->Traits.Find(Name))
		{
			OutClass = T->Class;
			OutId = T->Id;
			return true;
		}
		return false;
	}
	virtual FString DisciplineName(int32 Id) const override
	{
		return Id == 6 ? FString(TEXT("Dominate")) : FString();
	}

private:
	const FFakeDlgCharacter* Char = nullptr;
};

class FFakeDlgSheet final : public IElysiumDlgSheet
{
public:
	explicit FFakeDlgSheet(FFakeDlgCharacter& InChar) : Char(&InChar) {}

	virtual int32 CalcFeat(const FString& FeatName) const override
	{
		const FFakeDlgCharacter::FTrait* T = Char->Traits.Find(FeatName);
		return T ? T->Value : 0;
	}
	virtual int32 Stat(EElysiumDlgTraitClass Class, int32 Id) const override
	{
		return Lookup(Class, Id);
	}
	virtual int32 Discipline(int32 Id) const override
	{
		return Lookup(EElysiumDlgTraitClass::Discipline, Id);
	}
	virtual int32 BloodPool() const override { return Char->Blood; }
	virtual bool IsMale() const override { return Char->bMale; }
	virtual int32 ClanOffset() const override { return Char->Clan; }
	virtual void SpendBlood(int32 Points) override
	{
		Char->BloodSpent += Points;
		Char->Blood -= Points;
	}
	virtual void AddFakedDisciplineEffect(const FString& Trait, int32 DisciplineId,
		int32 Level) override
	{
		Char->FakedEffects.Add(FString::Printf(TEXT("%s:%d:%d"), *Trait, DisciplineId, Level));
	}

private:
	int32 Lookup(EElysiumDlgTraitClass Class, int32 Id) const
	{
		for (const TPair<FString, FFakeDlgCharacter::FTrait>& Row : Char->Traits)
		{
			if (Row.Value.Class == Class && Row.Value.Id == Id)
			{
				return Row.Value.Value;
			}
		}
		return 0;
	}
	FFakeDlgCharacter* Char = nullptr;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgDependencyTest,
	"Elysium.Substrate.DlgDependency", GElysiumTestFlags)
bool FElysiumDlgDependencyTest::RunTest(const FString&)
{
	FFakeDlgCharacter Char = FFakeDlgCharacter::Shipped();
	FFakeDlgResolver Resolver(Char);
	FFakeDlgSheet Sheet(Char);

	// Never called unless the dependency really has a Python half.
	TArray<FString> PythonSeen;
	TSet<FString> PythonTrue;
	auto Python = [&PythonSeen, &PythonTrue](const FString& Source)
	{
		PythonSeen.Add(Source);
		return PythonTrue.Contains(Source);
	};

	// --- the parse ----------------------------------------------------------------------------
	{
		// `Humanity -8` — the 459 corpus rows the string rewrite could never pass. The `-` is the
		// inversion MARKER, not a relop: threshold 8, compare `<`.
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(TEXT("Humanity -8"), &Resolver);
		TestEqual(TEXT("Humanity -8 trait"), D.Trait, FString(TEXT("Humanity")));
		TestTrue(TEXT("Humanity -8 is an attribute"), D.Class == EElysiumDlgTraitClass::Attribute);
		TestEqual(TEXT("Humanity -8 threshold is positive 8"), D.Threshold, 8);
		TestTrue(TEXT("Humanity -8 is inverted"), D.bInverted);
		TestTrue(TEXT("Humanity -8 is skill-only"), D.Compound == EElysiumDlgCompound::SkillOnly);
		TestEqual(TEXT("Humanity -8 costs no blood"), D.BloodCost, 0);
		TestFalse(TEXT("Humanity -8 has no python"), D.bHasPython);
		TestFalse(TEXT("Humanity -8 is not an unresolved front"), D.bUnresolvedSkillFront);
	}
	{
		// `F_Seduction 4` — the `F_` is the dependency's sex gate, not part of the trait name.
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(TEXT("F_Seduction 4"), &Resolver);
		TestEqual(TEXT("F_Seduction trait"), D.Trait, FString(TEXT("Seduction")));
		TestTrue(TEXT("F_Seduction is a feat"), D.Class == EElysiumDlgTraitClass::Feat);
		TestTrue(TEXT("F_Seduction requires a female PC"), D.SexGate == EElysiumDlgSexGate::Female);
		TestEqual(TEXT("F_Seduction threshold"), D.Threshold, 4);
	}
	{
		const FElysiumDlgDependency D =
			FElysiumDlgDependency::Parse(TEXT("Seduction 3 & OneOfSet(1,4)"), &Resolver);
		TestTrue(TEXT("compound AND"), D.Compound == EElysiumDlgCompound::And);
		TestTrue(TEXT("skill-first precedence"), D.Precedence == EElysiumDlgPrecedence::SkillFirst);
		TestEqual(TEXT("python half is the remainder"), D.Python, FString(TEXT("OneOfSet(1,4)")));
	}
	{
		// The python half first: precedence flips, and the skill front still parses.
		const FElysiumDlgDependency D =
			FElysiumDlgDependency::Parse(TEXT("G.Patch_Plus == 1 & Intimidate 7"), &Resolver);
		TestTrue(TEXT("python-first precedence"), D.Precedence == EElysiumDlgPrecedence::PythonFirst);
		TestEqual(TEXT("python half"), D.Python, FString(TEXT("G.Patch_Plus == 1")));
		TestEqual(TEXT("skill half"), D.Trait, FString(TEXT("Intimidate")));
		TestEqual(TEXT("skill threshold"), D.Threshold, 7);
	}
	{
		// A discipline carries its blood price; nothing else does.
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(TEXT("Dominate 2"), &Resolver);
		TestTrue(TEXT("Dominate is a discipline"), D.Class == EElysiumDlgTraitClass::Discipline);
		TestEqual(TEXT("Dominate id"), D.TraitId, 6);
		TestEqual(TEXT("Dominate blood cost is the threshold"), D.BloodCost, 2);
	}
	{
		// A pure python condition never claims the simple slot.
		const FElysiumDlgDependency D =
			FElysiumDlgDependency::Parse(TEXT("npc.times_talked == 0"), &Resolver);
		TestTrue(TEXT("python-only"), D.Compound == EElysiumDlgCompound::PythonOnly);
		TestFalse(TEXT("no skill front"), D.bHasSkill);
		TestFalse(TEXT("a member access is not a skill-shaped front"), D.bUnresolvedSkillFront);
	}
	{
		// A skill-SHAPED front naming nothing is the corpus diagnostic (zero rows ship like this).
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(TEXT("Persausion 7"), &Resolver);
		TestTrue(TEXT("a misspelled check is flagged"), D.bUnresolvedSkillFront);
		TestTrue(TEXT("...and falls to the python half"), D.Compound == EElysiumDlgCompound::PythonOnly);
	}
	{
		// An empty col-4 is the caller's open gate and is never parsed by retail at all.
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(TEXT(""), &Resolver);
		TestTrue(TEXT("empty is empty"), D.bEmpty);
		TestTrue(TEXT("empty passes"), D.Test(&Sheet, Python));
	}

	// --- TestSimple ---------------------------------------------------------------------------
	auto Eval = [&](const TCHAR* Raw)
	{
		return FElysiumDlgDependency::Parse(Raw, &Resolver).Test(&Sheet, Python);
	};

	// Attribute compare, both ways round. Humanity is 7.
	TestTrue(TEXT("Humanity 5 passes at 7"), Eval(TEXT("Humanity 5")));
	TestFalse(TEXT("Humanity 8 fails at 7"), Eval(TEXT("Humanity 8")));
	TestTrue(TEXT("Humanity -8 passes at 7"), Eval(TEXT("Humanity -8")));
	TestFalse(TEXT("Humanity -5 fails at 7"), Eval(TEXT("Humanity -5")));
	// Ability and feat take the same comparison through their own accessors.
	TestTrue(TEXT("Brawl 2 passes at 2"), Eval(TEXT("Brawl 2")));
	TestTrue(TEXT("Intimidate 7 passes at 7"), Eval(TEXT("Intimidate 7")));
	TestFalse(TEXT("Persuasion 7 fails at 4"), Eval(TEXT("Persuasion 7")));

	// The sex gate refuses before the class switch is even reached.
	Char.bMale = true;
	TestFalse(TEXT("F_Seduction 3 refuses a male PC"), Eval(TEXT("F_Seduction 3")));
	TestTrue(TEXT("M_Seduction 3 admits a male PC"), Eval(TEXT("M_Seduction 3")));
	Char.bMale = false;
	TestTrue(TEXT("F_Seduction 3 admits a female PC"), Eval(TEXT("F_Seduction 3")));
	TestFalse(TEXT("M_Seduction 3 refuses a female PC"), Eval(TEXT("M_Seduction 3")));
	Char.bMale = true;

	// Discipline: rating AND blood, plus the hard-coded Ventrue gate on slot 6.
	Char.Clan = ElysiumDlgClan::Ventrue;
	Char.Blood = 3;
	TestTrue(TEXT("Dominate 2 passes with rating 2 and blood 3"), Eval(TEXT("Dominate 2")));
	Char.Blood = 1;
	TestFalse(TEXT("Dominate 2 fails with blood 1"), Eval(TEXT("Dominate 2")));
	Char.Blood = 10;
	TestFalse(TEXT("Dominate 3 fails with rating 2"), Eval(TEXT("Dominate 3")));
	Char.Clan = ElysiumDlgClan::Tremere;
	TestFalse(TEXT("Dominate 2 is refused outside Ventrue (TestSimple's id-6 arm)"),
		Eval(TEXT("Dominate 2")));
	Char.Clan = ElysiumDlgClan::Ventrue;
	// An inverted discipline reads the rating only — retail never tests the pool on a failure route.
	Char.Blood = 0;
	TestTrue(TEXT("Dominate -3 passes on rating alone"), Eval(TEXT("Dominate -3")));
	Char.Blood = 10;

	// The frenzy feat is a named seam: it answers false, and its inverted form true.
	TestFalse(TEXT("Frenzy_Feat 1 answers false until FrenzyComparison lands"),
		Eval(TEXT("Frenzy_Feat 1")));
	TestTrue(TEXT("Frenzy_Feat -1 answers true until FrenzyComparison lands"),
		Eval(TEXT("Frenzy_Feat -1")));

	// --- Test's compound table ------------------------------------------------------------------
	PythonTrue.Reset();
	PythonTrue.Add(TEXT("G.Patch_Plus == 1"));
	// AND, skill-first: both halves.
	TestTrue(TEXT("Intimidate 7 & G.Patch_Plus == 1"), Eval(TEXT("Intimidate 7 & G.Patch_Plus == 1")));
	TestFalse(TEXT("Persuasion 7 & G.Patch_Plus == 1 (skill fails)"),
		Eval(TEXT("Persuasion 7 & G.Patch_Plus == 1")));
	TestFalse(TEXT("Intimidate 7 & G.Off == 1 (python fails)"),
		Eval(TEXT("Intimidate 7 & G.Off == 1")));
	// OR: either half.
	TestTrue(TEXT("Persuasion 7 | G.Patch_Plus == 1 (python carries it)"),
		Eval(TEXT("Persuasion 7 | G.Patch_Plus == 1")));
	TestTrue(TEXT("Intimidate 7 | G.Off == 1 (skill carries it)"),
		Eval(TEXT("Intimidate 7 | G.Off == 1")));
	TestFalse(TEXT("Persuasion 7 | G.Off == 1"), Eval(TEXT("Persuasion 7 | G.Off == 1")));
	// The corpus's two real joins.
	PythonTrue.Add(TEXT("pc.humanity >= 5"));
	PythonTrue.Add(TEXT("not IsMale(pc)"));
	TestTrue(TEXT("Persuasion 4 & pc.humanity >= 5"), Eval(TEXT("Persuasion 4 & pc.humanity >= 5")));
	TestTrue(TEXT("Seduction 3 & not IsMale(pc)"), Eval(TEXT("Seduction 3 & not IsMale(pc)")));
	// Short-circuit: a skill-first AND whose check fails never reaches the host.
	PythonSeen.Reset();
	TestFalse(TEXT("Persuasion 9 & G.Patch_Plus == 1"), Eval(TEXT("Persuasion 9 & G.Patch_Plus == 1")));
	TestEqual(TEXT("a failed skill-first AND does not evaluate python"), PythonSeen.Num(), 0);
	// A skill-only dependency never calls the host at all.
	PythonSeen.Reset();
	Eval(TEXT("Humanity 5"));
	TestEqual(TEXT("a skill-only gate never reaches the host"), PythonSeen.Num(), 0);

	// Both halves Python: retail's single buffer keeps the LAST one, and the compound then tests a
	// simple dependency that was never claimed — which is its `Unhandled dialog dependency` false.
	// One shipped row does this (`IsClan(pc,"Ventrue") & G.Patch_Plus == 1`); it is dead in retail
	// and stays dead here.
	{
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(
			TEXT("IsClan(pc,\"Ventrue\") & G.Patch_Plus == 1"), &Resolver);
		TestEqual(TEXT("the second python half overwrites the first"), D.Python,
			FString(TEXT("G.Patch_Plus == 1")));
		TestFalse(TEXT("a two-python compound cannot pass"), D.Test(&Sheet, Python));
	}

	// --- Explain: the label and the counterfactual ------------------------------------------------
	{
		const FElysiumDlgDependency D = FElysiumDlgDependency::Parse(TEXT("Persuasion 7"), &Resolver);
		const FElysiumDlgGateResult R = D.Explain(&Sheet, Python);
		TestFalse(TEXT("Persuasion 7 fails at 4"), R.bPasses);
		TestTrue(TEXT("...only on the skill front"), R.bSkillFailedOnly);
		TestTrue(TEXT("...and carries a label"), R.Label.bValid);
		TestEqual(TEXT("label trait"), R.Label.Trait, FString(TEXT("Persuasion")));
		TestEqual(TEXT("label required"), R.Label.Required, 7);
		TestEqual(TEXT("label have"), R.Label.Have, 4);
		TestEqual(TEXT("label costs no blood"), R.Label.BloodCost, 0);
	}
	{
		// A passing row is labelled too (M-REQ shows `[ INTIMIDATE 7/7 ]`).
		const FElysiumDlgGateResult R =
			FElysiumDlgDependency::Parse(TEXT("Intimidate 7"), &Resolver).Explain(&Sheet, Python);
		TestTrue(TEXT("Intimidate 7 passes"), R.bPasses);
		TestTrue(TEXT("a passing row is still labelled"), R.Label.bValid);
		TestEqual(TEXT("label have == required"), R.Label.Have, 7);
	}
	{
		// An inverted row is an authored failure route: never labelled, never disabled.
		const FElysiumDlgGateResult R =
			FElysiumDlgDependency::Parse(TEXT("Humanity -5"), &Resolver).Explain(&Sheet, Python);
		TestFalse(TEXT("Humanity -5 fails at 7"), R.bPasses);
		TestFalse(TEXT("an inverted row is never disabled"), R.bSkillFailedOnly);
		TestFalse(TEXT("an inverted row carries no label"), R.Label.bValid);
	}
	{
		// The python half decides: the skill front cannot rescue it, so the row hides.
		PythonTrue.Reset();
		const FElysiumDlgGateResult R = FElysiumDlgDependency::Parse(
			TEXT("Persuasion 7 & G.Patch_Plus == 1"), &Resolver).Explain(&Sheet, Python);
		TestFalse(TEXT("both halves fail"), R.bPasses);
		TestFalse(TEXT("a failing python half is not a skill-only failure"), R.bSkillFailedOnly);
		PythonTrue.Add(TEXT("G.Patch_Plus == 1"));
		const FElysiumDlgGateResult R2 = FElysiumDlgDependency::Parse(
			TEXT("Persuasion 7 & G.Patch_Plus == 1"), &Resolver).Explain(&Sheet, Python);
		TestTrue(TEXT("with the python half true, the skill front is the only failure"),
			R2.bSkillFailedOnly);
	}
	{
		// A discipline short of blood but not of rating is a skill failure carrying its cost.
		Char.Clan = ElysiumDlgClan::Ventrue;
		Char.Blood = 1;
		const FElysiumDlgGateResult R =
			FElysiumDlgDependency::Parse(TEXT("Dominate 2"), &Resolver).Explain(&Sheet, Python);
		TestFalse(TEXT("Dominate 2 fails on blood"), R.bPasses);
		TestTrue(TEXT("a blood shortfall is a skill-front failure"), R.bSkillFailedOnly);
		TestEqual(TEXT("the label carries the cost"), R.Label.BloodCost, 2);
		TestEqual(TEXT("the label carries the rating"), R.Label.Have, 2);
		// The clan gate is NOT a skill failure — a Tremere never sees the row at all.
		Char.Clan = ElysiumDlgClan::Tremere;
		const FElysiumDlgGateResult Clan =
			FElysiumDlgDependency::Parse(TEXT("Dominate 2"), &Resolver).Explain(&Sheet, Python);
		TestFalse(TEXT("the clan gate hides rather than disables"), Clan.bSkillFailedOnly);
		Char.Clan = ElysiumDlgClan::Ventrue;
		Char.Blood = 10;
	}

	// --- the charge ---------------------------------------------------------------------------
	{
		Char.BloodSpent = 0;
		Char.FakedEffects.Reset();
		FElysiumDlgDependency::Parse(TEXT("Dominate 2"), &Resolver).Charge(&Sheet);
		TestEqual(TEXT("picking a discipline row spends its cost"), Char.BloodSpent, 2);
		TestEqual(TEXT("...and fires the faked-effect seam once"), Char.FakedEffects.Num(), 1);
		if (Char.FakedEffects.Num() == 1)
		{
			TestEqual(TEXT("...with the trait, its id and the level"), Char.FakedEffects[0],
				FString(TEXT("Dominate:6:2")));
		}
		Char.BloodSpent = 0;
		Char.FakedEffects.Reset();
		FElysiumDlgDependency::Parse(TEXT("Persuasion 4"), &Resolver).Charge(&Sheet);
		TestEqual(TEXT("a non-discipline row charges nothing"), Char.BloodSpent, 0);
		TestEqual(TEXT("...and fires no effect"), Char.FakedEffects.Num(), 0);
	}

	// A dependency with a skill front and no sheet fails closed rather than passing.
	TestFalse(TEXT("no sheet fails the skill front closed"),
		FElysiumDlgDependency::Parse(TEXT("Humanity 1"), &Resolver).Test(nullptr, Python));

	return true;
}

namespace ElysiumDlgTestFixture
{
	// Build a conversation over synthetic rows with the fake sheet bound.
	struct FBand
	{
		TSharedPtr<FElysiumDlgFile> File;
		TSharedPtr<FElysiumDlgConversation> Conv;
		TArray<FString> Ran;
		TSharedPtr<FFakeDlgCharacter> Char;

		bool Build(FAutomationTestBase& Test, const TArray<FString>& Rows,
			TSet<FString> PythonTrue = TSet<FString>())
		{
			Char = MakeShared<FFakeDlgCharacter>(FFakeDlgCharacter::Shipped());
			File = MakeShared<FElysiumDlgFile>();
			File->SourcePath = TEXT("dlg/test/band.dlg");
			if (!Test.TestTrue(TEXT("band fixture parses"),
				FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), *File)))
			{
				return false;
			}
			File->SourcePath = TEXT("dlg/test/band.dlg");
			TSharedPtr<FFakeDlgCharacter> C = Char;
			Conv = MakeShared<FElysiumDlgConversation>(File.ToSharedRef(), /*bMale*/ true,
				/*bMalk*/ false,
				[PythonTrue](const FString& Source) { return PythonTrue.Contains(Source); },
				[this](const FString& Action) { Ran.Add(Action); });
			Conv->SetGateContext(MakeShared<FFakeDlgSheet>(*C), MakeShared<FFakeDlgResolver>(*C),
				ElysiumDlgClan::Ventrue);
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgDisabledRowsTest,
	"Elysium.Substrate.DlgDisabledRows", GElysiumTestFlags)
bool FElysiumDlgDisabledRowsTest::RunTest(const FString&)
{
	// M-DISABLED: a row failing only on its skill front stays visible and greyed; a row failing on
	// anything else hides; a negative-threshold row is an authored failure route and never greys;
	// a disabled row whose sentence already appears on an enabled row is dropped.
	ElysiumDlgTestFixture::FBand Band;
	if (!Band.Build(*this, {
		ElysiumDlgRow(11, TEXT("So?"), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("I have the words."), TEXT("21"), TEXT("Intimidate 7"), TEXT("")),
		ElysiumDlgRow(13, TEXT("Let me persuade you."), TEXT("21"), TEXT("Persuasion 7"), TEXT("")),
		ElysiumDlgRow(14, TEXT("Fine, whatever."), TEXT("21"), TEXT("Humanity -5"), TEXT("")),
		ElysiumDlgRow(15, TEXT("Secret path."), TEXT("21"), TEXT("G.Locked == 1"), TEXT("")),
		ElysiumDlgRow(16, TEXT("Ordinary."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		return false;
	}
	Band.Conv->Start();

	// 12 passes (7/7), 13 is disabled (4/7), 14's inverted check fails and hides, 15's python fails
	// and hides, 16 is ungated.
	const TArray<FElysiumDlgVisibleChoice>& Rows = Band.Conv->VisibleChoices();
	TestEqual(TEXT("three rows survive"), Rows.Num(), 3);
	if (Rows.Num() == 3)
	{
		TestEqual(TEXT("row 1 is the passing check"), Band.Conv->VisibleChoice(0)->Id, 12);
		TestTrue(TEXT("row 1 is enabled"), Rows[0].bEnabled);
		TestEqual(TEXT("row 2 is the failed check"), Band.Conv->VisibleChoice(1)->Id, 13);
		TestFalse(TEXT("row 2 is disabled"), Rows[1].bEnabled);
		TestEqual(TEXT("row 2 label"), Rows[1].Gate.Label.Trait, FString(TEXT("Persuasion")));
		TestEqual(TEXT("row 2 shows what the player has"), Rows[1].Gate.Label.Have, 4);
		TestEqual(TEXT("row 2 shows what is required"), Rows[1].Gate.Label.Required, 7);
		TestEqual(TEXT("row 3 is the ungated row"), Band.Conv->VisibleChoice(2)->Id, 16);
	}
	TestEqual(TEXT("two rows are pickable"), Band.Conv->NumEnabledChoices(), 2);
	TestFalse(TEXT("the disabled row is not enabled"), Band.Conv->IsChoiceEnabled(1));
	TestFalse(TEXT("a band with enabled rows is not terminal"), Band.Conv->IsTerminalLine());

	// A disabled row's key does nothing at all: no action, no turn change.
	const uint32 Before = Band.Conv->Revision();
	Band.Conv->Choose(1);
	TestEqual(TEXT("choosing a disabled row is refused"), Band.Conv->Revision(), Before);
	TestEqual(TEXT("row 12 still current"), Band.Conv->CurrentNpcLine()->Id, 11);

	// Dedup: the pass and fail routes of one beat share their sentence, so only the enabled one
	// prints — the corpus authors 48 such pairs.
	ElysiumDlgTestFixture::FBand Twins;
	if (!Twins.Build(*this, {
		ElysiumDlgRow(11, TEXT("So?"), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Trust me on this."), TEXT("21"), TEXT("Persuasion 7"), TEXT("")),
		ElysiumDlgRow(13, TEXT("Trust me on this."), TEXT("31"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Sure."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		return false;
	}
	Twins.Conv->Start();
	TestEqual(TEXT("the disabled twin is dropped"), Twins.Conv->VisibleChoices().Num(), 1);
	TestEqual(TEXT("the surviving row is the enabled fail route"),
		Twins.Conv->VisibleChoice(0)->Id, 13);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgGateLabelTest,
	"Elysium.Substrate.DlgGateLabel", GElysiumTestFlags)
bool FElysiumDlgGateLabelTest::RunTest(const FString&)
{
	// M-REQ's label has to name WHICH half of a discipline check refused. `TestSimple`
	// (`0x100e9760`) compares the threshold twice for class 2 — once against the rating, once
	// against attributes slot `0xc` — so a greyed `[ DOMINATE 2/2 ]` row is only intelligible if
	// the label also carries the pool.
	{
		ElysiumDlgTestFixture::FBand Band;
		if (!Band.Build(*this, {
			ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
			ElysiumDlgRow(12, TEXT("Look at me."), TEXT("21"), TEXT("Dominate 2"), TEXT("")),
			ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
		}))
		{
			return false;
		}
		// `TestSimple` refuses discipline slot 6 outside clan_offset 5, so the sheet has to answer
		// Ventrue for the Dominate rows to be reached at all.
		Band.Char->Clan = ElysiumDlgClan::Ventrue;
		Band.Char->Blood = 1;   // rating 2 passes, the pool does not
		Band.Conv->Start();
		const TArray<FElysiumDlgVisibleChoice>& Rows = Band.Conv->VisibleChoices();
		if (!TestEqual(TEXT("the blood-short discipline row stays visible"), Rows.Num(), 1))
		{
			return false;
		}
		TestFalse(TEXT("...disabled"), Rows[0].bEnabled);
		TestTrue(TEXT("...carrying a label"), Rows[0].Gate.Label.bValid);
		TestEqual(TEXT("...the rating the player has"), Rows[0].Gate.Label.Have, 2);
		TestEqual(TEXT("...the rating required"), Rows[0].Gate.Label.Required, 2);
		TestEqual(TEXT("...the blood price"), Rows[0].Gate.Label.BloodCost, 2);
		TestEqual(TEXT("...and the pool the player actually has"), Rows[0].Gate.Label.Pool, 1);
		TestTrue(TEXT("the pool is the half that refused"), Rows[0].Gate.Label.bBloodShort);
	}
	{
		// The other way round: the pool is full and the RATING is short, so the row is disabled for
		// the rating and must not claim to be blood-short.
		ElysiumDlgTestFixture::FBand Band;
		if (!Band.Build(*this, {
			ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
			ElysiumDlgRow(12, TEXT("Look at me."), TEXT("21"), TEXT("Dominate 3"), TEXT("")),
			ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
		}))
		{
			return false;
		}
		Band.Char->Clan = ElysiumDlgClan::Ventrue;
		Band.Char->Blood = 10;
		Band.Conv->Start();
		const TArray<FElysiumDlgVisibleChoice>& Rows = Band.Conv->VisibleChoices();
		if (!TestEqual(TEXT("the rating-short discipline row stays visible"), Rows.Num(), 1))
		{
			return false;
		}
		TestFalse(TEXT("...disabled"), Rows[0].bEnabled);
		TestEqual(TEXT("...the pool is reported even when it is not the problem"),
			Rows[0].Gate.Label.Pool, 10);
		TestFalse(TEXT("...and it is not flagged short"), Rows[0].Gate.Label.bBloodShort);
	}
	{
		// S10c — the frenzy seam. `TestSimple`'s class-4 id 0x16 arm routes to
		// `CBaseCombatCharacter::FrenzyComparison`, which the port cannot answer, so a non-inverted
		// frenzy check reads false for a PORT reason. It must not become a disabled
		// `[ FRENZY 0/1 ]` row advertising a requirement retail never draws: it hides, like any
		// other gate the port cannot pass.
		ElysiumDlgTestFixture::FBand Band;
		if (!Band.Build(*this, {
			ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
			ElysiumDlgRow(12, TEXT("*snarl*"), TEXT("21"), TEXT("Frenzy_Feat 1"), TEXT("")),
			ElysiumDlgRow(13, TEXT("Ordinary."), TEXT("21"), TEXT(""), TEXT("")),
			ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
		}))
		{
			return false;
		}
		Band.Conv->Start();
		if (!TestEqual(TEXT("the unrecovered frenzy row is hidden, not greyed"),
			Band.Conv->VisibleChoices().Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("only the ungated row survives"), Band.Conv->VisibleChoice(0)->Id, 13);

		FFakeDlgCharacter Char = FFakeDlgCharacter::Shipped();
		const FFakeDlgResolver Resolver(Char);
		const FElysiumDlgDependency Dep =
			FElysiumDlgDependency::Parse(TEXT("Frenzy_Feat 1"), &Resolver);
		TestTrue(TEXT("the frenzy front still parses as a feat"),
			Dep.Class == EElysiumDlgTraitClass::Feat && Dep.TraitId == ElysiumDlgFeat::Frenzy);
		TestTrue(TEXT("...and is named as the seam it is"), Dep.IsUnrecoveredFrenzyFront());
		TestFalse(TEXT("...so presentation never labels it"), Dep.HasLabelledSkillFront());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgCloseDuringPickTest,
	"Elysium.Substrate.DlgCloseDuringPick", GElysiumTestFlags)
bool FElysiumDlgCloseDuringPickTest::RunTest(const FString&)
{
	// `CDialog::Pick` (`0x100e4bd0`) flushes the NPC's parked col-5 BEFORE it charges the
	// dependency. That col-5 is authored script and one shipped shape of it releases the dialog
	// (`EndDialog`), which is `CDialog::Release` — and past a Release there is no Pick left to
	// finish: nothing is charged and no line is entered.
	TSharedRef<FFakeDlgCharacter> Char =
		MakeShared<FFakeDlgCharacter>(FFakeDlgCharacter::Shipped());
	Char->Clan = ElysiumDlgClan::Ventrue;
	Char->Blood = 10;

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("close-on-pick fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
			ElysiumDlgRow(11, TEXT("Speaking."), TEXT("#"), TEXT(""), TEXT("CLOSE_ME")),
			ElysiumDlgRow(12, TEXT("Look at me."), TEXT("21"), TEXT("Dominate 2"), TEXT("PICK_COL5")),
			ElysiumDlgRow(13, TEXT("Say nothing."), TEXT("21"), TEXT(""), TEXT("")),
			ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
		}), File.Get())))
	{
		return false;
	}
	File->SourcePath = TEXT("dlg/test/close_on_pick.dlg");

	TArray<FString> Ran;
	TSharedPtr<FElysiumDlgConversation> Conv;
	Conv = MakeShared<FElysiumDlgConversation>(File, /*bMale*/ true, /*bMalk*/ false,
		[](const FString&) { return true; },
		[&Ran, &Conv](const FString& Action)
		{
			Ran.Add(Action);
			if (Action == TEXT("CLOSE_ME") && Conv.IsValid())
			{
				Conv->Close();   // the authored `EndDialog` half of a parked col-5
			}
		});
	Conv->SetGateContext(MakeShared<FFakeDlgSheet>(*Char), MakeShared<FFakeDlgResolver>(*Char),
		ElysiumDlgClan::Ventrue);
	Conv->Start();

	// The band is offered exactly as usual: the discipline row passes on rating AND pool.
	if (!TestEqual(TEXT("both rows are offered"), Conv->VisibleChoices().Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("the discipline row is pickable"), Conv->IsChoiceEnabled(0));
	// S8 — presentation's stale-pick key: the visible row's own `.dlg` line id.
	TestEqual(TEXT("visible row 0 names line 12"), Conv->VisibleChoiceLineId(0), 12);
	TestEqual(TEXT("visible row 1 names line 13"), Conv->VisibleChoiceLineId(1), 13);
	TestEqual(TEXT("an out-of-range row names nothing"), Conv->VisibleChoiceLineId(7), INDEX_NONE);
	TestEqual(TEXT("...including a negative index"), Conv->VisibleChoiceLineId(-1), INDEX_NONE);

	Conv->Choose(0);
	TestTrue(TEXT("the parked col-5 ran"), Ran.Contains(TEXT("CLOSE_ME")));
	TestTrue(TEXT("...and closed the conversation"), Conv->IsOver());
	TestEqual(TEXT("no blood is spent on a pick the release cut short"), Char->BloodSpent, 0);
	TestTrue(TEXT("...and no faked discipline effect is raised"), Char->FakedEffects.IsEmpty());
	TestFalse(TEXT("the picked row's own col-5 never runs"), Ran.Contains(TEXT("PICK_COL5")));
	TestNull(TEXT("a closed conversation has no current NPC line"), Conv->CurrentNpcLine());
	TestEqual(TEXT("...and no response band"), Conv->VisibleChoices().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgCorpusMountTest,
	"Elysium.Substrate.DlgCorpusMount", GElysiumTestFlags)
bool FElysiumDlgCorpusMountTest::RunTest(const FString&)
{
	// One path, one byte source. The runtime loads a conversation through
	// `FElysiumContentPaths::DlgFromDialogname` (the corpus), and VtMB's own scripts probe that
	// same tree by hand (`fileutil.isFile` gating a dialogue line, vamputil.py:1039) through the
	// script filesystem. Both spellings have to land on the same directory or a script can gate a
	// line on a file the loader will not read.
	//
	// `sound/` used to be mounted beside it and is not any more: audio is baked asset content
	// (AUD1.2) and no shipped script opens a file under `sound/`.
	FString Real;
	if (TestTrue(TEXT("dlg/ is mounted"),
		FElysiumScriptFS::MapToMirror(TEXT("dlg/main characters/jack_tutorial.dlg"), Real)))
	{
		TestTrue(TEXT("dlg/ serves from the corpus, not the legacy export root"),
			Real.Replace(TEXT("\\"), TEXT("/")).StartsWith(
				FElysiumContentPaths::DlgDir().Replace(TEXT("\\"), TEXT("/")) + TEXT("/"),
				ESearchCase::IgnoreCase));
	}
	// The mount point itself resolves, which is what `nt.listdir` on a tree asks for.
	if (TestTrue(TEXT("the dlg mount point resolves"),
		FElysiumScriptFS::MapToMirror(TEXT("dlg"), Real)))
	{
		TestTrue(TEXT("...to the corpus dlg directory"),
			FPaths::IsSamePath(Real, FElysiumContentPaths::DlgDir()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgPendingActionTest,
	"Elysium.Substrate.DlgPendingAction", GElysiumTestFlags)
bool FElysiumDlgPendingActionTest::RunTest(const FString&)
{
	// `process_npc_line` runs col-4 NOW and parks col-5; `CallPendingNPCEventScript` runs the
	// parked one once, and a pick's own col-5 comes after it.
	auto MakeBand = [this](ElysiumDlgTestFixture::FBand& Out)
	{
		return Out.Build(*this, {
			ElysiumDlgRow(11, TEXT("Speaking."), TEXT("#"), TEXT("NPC_COL4"), TEXT("NPC_COL5")),
			ElysiumDlgRow(12, TEXT("Answer."), TEXT("21"), TEXT(""), TEXT("PICK_COL5")),
			ElysiumDlgRow(13, TEXT("Leave."), TEXT("0"), TEXT(""), TEXT("BYE_COL5")),
			ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
		});
	};

	{
		ElysiumDlgTestFixture::FBand Band;
		if (!MakeBand(Band)) { return false; }
		Band.Conv->Start();
		TestTrue(TEXT("col-4 ran on entry"), Band.Ran.Contains(TEXT("NPC_COL4")));
		TestFalse(TEXT("col-5 is parked, not run"), Band.Ran.Contains(TEXT("NPC_COL5")));
		TestTrue(TEXT("the parked action is reported"), Band.Conv->HasPendingNpcAction());

		Band.Conv->FlushPendingNpcAction();
		TestTrue(TEXT("the flush runs col-5"), Band.Ran.Contains(TEXT("NPC_COL5")));
		TestFalse(TEXT("the park is cleared"), Band.Conv->HasPendingNpcAction());
		Band.Conv->FlushPendingNpcAction();
		int32 Count = 0;
		for (const FString& A : Band.Ran) { Count += A == TEXT("NPC_COL5") ? 1 : 0; }
		TestEqual(TEXT("a second flush runs nothing"), Count, 1);
	}
	{
		// A pick flushes the parked action BEFORE running its own col-5.
		ElysiumDlgTestFixture::FBand Band;
		if (!MakeBand(Band)) { return false; }
		Band.Conv->Start();
		Band.Ran.Reset();
		Band.Conv->Choose(0);
		TestEqual(TEXT("two actions ran"), Band.Ran.Num(), 2);
		if (Band.Ran.Num() == 2)
		{
			TestEqual(TEXT("the NPC's parked col-5 goes first"), Band.Ran[0], FString(TEXT("NPC_COL5")));
			TestEqual(TEXT("the pick's own col-5 follows"), Band.Ran[1], FString(TEXT("PICK_COL5")));
		}
	}
	{
		// Close flushes too — `CDialog::Release` runs the pending script before it clears state.
		ElysiumDlgTestFixture::FBand Band;
		if (!MakeBand(Band)) { return false; }
		Band.Conv->Start();
		Band.Ran.Reset();
		Band.Conv->Close();
		TestTrue(TEXT("close flushes the parked col-5"), Band.Ran.Contains(TEXT("NPC_COL5")));
	}
	{
		// The skip verb's edge is the same call the world makes at voice completion.
		ElysiumDlgTestFixture::FBand Band;
		if (!MakeBand(Band)) { return false; }
		Band.Conv->Start();
		Band.Ran.Reset();
		Band.Conv->FlushPendingNpcAction();
		TestTrue(TEXT("skip flushes"), Band.Ran.Contains(TEXT("NPC_COL5")));
		TestEqual(TEXT("skip leaves the band alone"), Band.Conv->VisibleChoices().Num(), 2);
		TestEqual(TEXT("skip does not change the turn"), Band.Conv->CurrentNpcLine()->Id, 11);
		Band.Ran.Reset();
		Band.Conv->Choose(0);
		TestEqual(TEXT("a pick after a skip runs only its own action"), Band.Ran.Num(), 1);
		TestEqual(TEXT("...which is the pick's col-5"), Band.Ran[0], FString(TEXT("PICK_COL5")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgNoValidReplyTest,
	"Elysium.Substrate.DlgNoValidReply", GElysiumTestFlags)
bool FElysiumDlgNoValidReplyTest::RunTest(const FString&)
{
	// A band that authored responses and gated every one of them out is retail's "no valid reply":
	// the NPC's own text is replaced and one Continue closes.
	ElysiumDlgTestFixture::FBand Gated;
	if (!Gated.Build(*this, {
		ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Locked."), TEXT("21"), TEXT("G.Locked == 1"), TEXT("")),
		ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		return false;
	}
	Gated.Conv->Start();
	TestEqual(TEXT("no row survives"), Gated.Conv->VisibleChoices().Num(), 0);
	TestTrue(TEXT("the band reports no valid reply"), Gated.Conv->NoValidReply());
	TestTrue(TEXT("...and offers the Continue"), Gated.Conv->IsTerminalLine());
	TestEqual(TEXT("the substitute text is retail's"),
		FString(FElysiumDlgConversation::NoValidReplyText()),
		FString(TEXT("I do not have a valid reply.")));
	Gated.Conv->AdvanceTerminal();
	TestTrue(TEXT("the Continue closes"), Gated.Conv->IsOver());

	// A band with no PC rows at all is terminal by design and keeps the authored line.
	ElysiumDlgTestFixture::FBand Last;
	if (!Last.Build(*this, {
		ElysiumDlgRow(11, TEXT("That is all."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		return false;
	}
	Last.Conv->Start();
	TestTrue(TEXT("a bandless line is terminal"), Last.Conv->IsTerminalLine());
	TestFalse(TEXT("...and is NOT a no-valid-reply substitution"), Last.Conv->NoValidReply());

	// A disabled-only band is a no-valid-reply too: the player can read what they lack but cannot
	// answer, so the Continue has to be there.
	ElysiumDlgTestFixture::FBand Disabled;
	if (!Disabled.Build(*this, {
		ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Talk them round."), TEXT("21"), TEXT("Persuasion 9"), TEXT("")),
		ElysiumDlgRow(21, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		return false;
	}
	Disabled.Conv->Start();
	TestEqual(TEXT("the disabled row is shown"), Disabled.Conv->VisibleChoices().Num(), 1);
	TestEqual(TEXT("...but nothing is pickable"), Disabled.Conv->NumEnabledChoices(), 0);
	TestTrue(TEXT("so the turn still offers a way out"), Disabled.Conv->NoValidReply());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgClanColumnsTest,
	"Elysium.Substrate.DlgClanColumns", GElysiumTestFlags)
bool FElysiumDlgClanColumnsTest::RunTest(const FString&)
{
	// Seven clan columns, cols 6-12, in clan_offset order. `ElysiumDlgRow` fills col-12 only, so
	// build the wide row by hand.
	auto Row = [](int32 Id, const TCHAR* Male, const TCHAR* Female,
		const TCHAR* Ventrue, const TCHAR* Malkavian)
	{
		auto F = [](const FString& S) { return FString::Printf(TEXT("{\t%s\t}"), *S); };
		FString R = F(FString::FromInt(Id)) + F(Male) + F(Female) + F(TEXT("#")) + F(FString())
			+ F(FString());
		for (int32 Clan = 0; Clan < ElysiumDlgClan::Num; ++Clan)
		{
			R += F(Clan == ElysiumDlgClan::Ventrue ? FString(Ventrue)
				: (Clan == ElysiumDlgClan::Malkavian ? FString(Malkavian) : FString()));
		}
		return R;
	};

	FElysiumDlgFile File;
	if (!TestTrue(TEXT("clan fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		Row(11, TEXT("Male line."), TEXT("Female line."), TEXT("Ventrue line."), TEXT("Malk line.")),
		Row(21, TEXT("Plain male."), TEXT(""), TEXT(""), TEXT("")),
	}), File)))
	{
		return false;
	}
	const FElysiumDlgLine& Wide = File.Lines[0];
	TestEqual(TEXT("col-11 is the Ventrue column"), Wide.TextClan[ElysiumDlgClan::Ventrue],
		FString(TEXT("Ventrue line.")));
	TestEqual(TEXT("col-12 is the Malkavian column"), Wide.TextMalkavian(),
		FString(TEXT("Malk line.")));
	TestTrue(TEXT("an unauthored clan column is empty"),
		Wide.TextClan[ElysiumDlgClan::Brujah].IsEmpty());

	// `get_display_text`: a filled clan column wins over the gendered text.
	TestEqual(TEXT("a Ventrue PC reads col-11"), Wide.RawFor(true, ElysiumDlgClan::Ventrue),
		FString(TEXT("Ventrue line.")));
	TestEqual(TEXT("a Malkavian PC reads col-12"), Wide.RawFor(false, ElysiumDlgClan::Malkavian),
		FString(TEXT("Malk line.")));
	TestEqual(TEXT("a Brujah PC falls back to the gendered text"),
		Wide.RawFor(false, ElysiumDlgClan::Brujah), FString(TEXT("Female line.")));
	TestEqual(TEXT("a clanless male PC reads col-1"), Wide.RawFor(true, ElysiumDlgClan::None),
		FString(TEXT("Male line.")));
	// An empty clan column is not a variant.
	const FElysiumDlgLine& Plain = File.Lines[1];
	TestEqual(TEXT("an empty Ventrue column falls back"), Plain.RawFor(true, ElysiumDlgClan::Ventrue),
		FString(TEXT("Plain male.")));

	// The sheet's 2..8 clan encoding is NOT the clan_offset order.
	TestEqual(TEXT("sheet clan 8 is Ventrue offset 5"),
		ElysiumDlgClan::OffsetFromSheetClan(8), 5);
	TestEqual(TEXT("sheet clan 4 is Malkavian offset 6"),
		ElysiumDlgClan::OffsetFromSheetClan(4), 6);
	TestEqual(TEXT("sheet clan 5 is Nosferatu offset 2"),
		ElysiumDlgClan::OffsetFromSheetClan(5), 2);
	TestEqual(TEXT("an unset clan has no offset"),
		ElysiumDlgClan::OffsetFromSheetClan(0), (int32)ElysiumDlgClan::None);

	// The take letter is the COLUMN actually shown, not a language.
	auto Take = [](const FElysiumDlgLine& L, bool bIsMale, int32 Clan)
	{
		const TCHAR C = ElysiumDlgText::ChosenTakeLetter(L, bIsMale, Clan);
		return FString(1, &C);
	};
	TestEqual(TEXT("a male PC takes col_e"), Take(Wide, true, ElysiumDlgClan::None),
		FString(TEXT("e")));
	TestEqual(TEXT("a female PC with a col-2 variant takes col_f"),
		Take(Wide, false, ElysiumDlgClan::None), FString(TEXT("f")));
	TestEqual(TEXT("a female PC with no col-2 variant still takes col_e"),
		Take(Plain, false, ElysiumDlgClan::None), FString(TEXT("e")));
	TestEqual(TEXT("a Ventrue PC on a filled Ventrue column takes col_m"),
		Take(Wide, true, ElysiumDlgClan::Ventrue), FString(TEXT("m")));
	TestEqual(TEXT("a Malkavian PC on a filled Malkavian column takes col_n"),
		Take(Wide, true, ElysiumDlgClan::Malkavian), FString(TEXT("n")));
	TestEqual(TEXT("an unpinned clan letter falls back to the gendered take"),
		Take(Wide, true, ElysiumDlgClan::Brujah), FString(TEXT("e")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgDisplayTest, "Elysium.Substrate.DlgDisplay", GElysiumTestFlags)
bool FElysiumDlgDisplayTest::RunTest(const FString&)
{
	using ElysiumDlgText::StripStageDirections;

	// The real jack_tutorial line 11 opener: a leading `[...]` and an interior one, no space after either.
	TestEqual(TEXT("jack opener stripped"),
		StripStageDirections(TEXT("[laughing at something no one else thinks is funny]What a scene, man! [chuckle]How 'bout that?")),
		FString(TEXT("What a scene, man! How 'bout that?")));
	// A direction between words leaves a single space, not a doubled one.
	TestEqual(TEXT("interior collapse"), StripStageDirections(TEXT("a [nods] b")), FString(TEXT("a b")));
	// No brackets -> verbatim (fast path).
	TestEqual(TEXT("no directions"), StripStageDirections(TEXT("Who are you?")), FString(TEXT("Who are you?")));
	// An unterminated `[` is kept (not a stage direction).
	TestEqual(TEXT("unterminated bracket kept"), StripStageDirections(TEXT("cost is [50")), FString(TEXT("cost is [50")));
	// A direction-only string strips to empty.
	TestEqual(TEXT("direction-only -> empty"), StripStageDirections(TEXT("[sighs]")), FString());

	// The line accessor path: raw Text() keeps the direction, DisplayText() drops it. col-12 (Malkavian)
	// replaces the text for a Malkavian player, and is stage-direction-stripped the same way.
	FElysiumDlgLine Line;
	Line.TextMale = TEXT("[chuckle]Hey there.");
	Line.TextMalkavian() = TEXT("[cackles]The walls whisper hello.");
	Line.Role = EElysiumDlgRole::NpcLine;
	TestEqual(TEXT("raw text verbatim"), Line.Text(true), FString(TEXT("[chuckle]Hey there.")));
	TestEqual(TEXT("display non-malk stripped"), Line.DisplayText(true, false), FString(TEXT("Hey there.")));
	TestEqual(TEXT("display malk variant stripped"), Line.DisplayText(true, true), FString(TEXT("The walls whisper hello.")));
	// A line with no col-12 falls back to the gendered text even for a Malkavian.
	FElysiumDlgLine Plain;
	Plain.TextMale = TEXT("Plain.");
	TestEqual(TEXT("no malk variant -> col-1"), Plain.DisplayText(true, true), FString(TEXT("Plain.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgBranchTest, "Elysium.Substrate.DlgBranch", GElysiumTestFlags)
bool FElysiumDlgBranchTest::RunTest(const FString&)
{
	// A miniature of the jack_tutorial shape: a starting sentinel selects the real entry (11),
	// a gated + an ungated choice, a follow NPC line (21) with a choice that sets a flag and ends.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(100, TEXT("(Starting Condition)"), TEXT("11"), TEXT("START"), TEXT("")));
	// Two characters of text: `read_line_data` stores a row only when its male text is at least
	// that long, so an empty NPC line would not be in retail's table at all.
	Rows.Add(ElysiumDlgRow(1, TEXT(".."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(11, TEXT("Greeting."), TEXT("#"), TEXT("SPEAK_11"), TEXT("")));
	Rows.Add(ElysiumDlgRow(12, TEXT("Gated"), TEXT("21"), TEXT("SHOW"), TEXT("PICK_12")));
	Rows.Add(ElysiumDlgRow(13, TEXT("Hidden"), TEXT("31"), TEXT("HIDE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(14, TEXT("Always"), TEXT("21"), TEXT(""), TEXT("")));    // ungated
	Rows.Add(ElysiumDlgRow(21, TEXT("More."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(22, TEXT("Set flag & bye"), TEXT("0"), TEXT(""), TEXT("SET_FLAG")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}

	// Record actions; a condition passes unless it is the string "HIDE".
	TArray<FString> Ran;
	auto Cond = [](const FString& C) { return C != TEXT("HIDE"); };
	auto Act = [&Ran](const FString& A) { Ran.Add(A); };

	FElysiumDlgConversation Conv(File, /*bMale*/ true, /*bMalk*/ false, Cond, Act);
	Conv.Start();

	// The sentinel selects line 11 rather than the physical first NPC line, then its col-4 action runs.
	if (TestNotNull(TEXT("opened on an NPC line"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("entry is line 11"), Conv.CurrentNpcLine()->Id, 11);
	}
	TestTrue(TEXT("ran SPEAK_11"), Ran.Contains(TEXT("SPEAK_11")));

	// Two of the three following rows are visible (gated SHOW passes, HIDE fails, ungated shows).
	TestEqual(TEXT("visible choice count"), Conv.VisibleChoices().Num(), 2);
	TestEqual(TEXT("first visible is 12"), Conv.VisibleChoice(0)->Id, 12);
	TestEqual(TEXT("second visible is 14"), Conv.VisibleChoice(1)->Id, 14);

	// Pick the first choice -> its action runs, jump to NPC 21.
	Conv.Choose(0);
	TestTrue(TEXT("ran PICK_12"), Ran.Contains(TEXT("PICK_12")));
	if (TestNotNull(TEXT("advanced to a line"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("now on line 21"), Conv.CurrentNpcLine()->Id, 21);
	}

	// Line 21 offers one ending choice; picking it runs the flag action then ends the conversation.
	TestEqual(TEXT("21 has one choice"), Conv.VisibleChoices().Num(), 1);
	Conv.Choose(0);
	TestTrue(TEXT("ran SET_FLAG"), Ran.Contains(TEXT("SET_FLAG")));
	TestTrue(TEXT("conversation is over"), Conv.IsOver());
	TestNull(TEXT("no current line after end"), Conv.CurrentNpcLine());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgAutomaticTest,
	"Elysium.Substrate.DlgAutomatic", GElysiumTestFlags)
bool FElysiumDlgAutomaticTest::RunTest(const FString&)
{
	// The exact authored shape behind Jack's tutorial transition: the first NPC line stays current,
	// a synthetic PC-role row silently follows to the next NPC line, and an Auto-End closes only
	// after that second spoken line. A preceding ordinary response proves the automatic row owns the
	// whole response band rather than merely hiding itself.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(1, TEXT("[like the Fonz]Alright."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(2, TEXT("Must not be shown"), TEXT("10"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(3, TEXT("  (auto-link)  "), TEXT("10"), TEXT(""), TEXT("AUTO_LINK")));
	Rows.Add(ElysiumDlgRow(10, TEXT("Uhh... why don't we, uh, step out back here."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(11, TEXT("(Auto-End)"), TEXT("0"), TEXT(""), TEXT("AUTO_END")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	File->SourcePath = TEXT("dlg/main characters/automatic_test.dlg");
	if (!TestTrue(TEXT("automatic fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}
	File->SourcePath = TEXT("dlg/main characters/automatic_test.dlg");

	FElysiumDlgLine Ordinary;
	Ordinary.Role = EElysiumDlgRole::PcChoice;
	Ordinary.TextMale = TEXT("I mention (Auto-Link), but I am dialogue.");
	TestFalse(TEXT("classification is exact rather than substring-based"), Ordinary.IsAutomatic());

	TArray<FString> Ran;
	TSharedRef<FElysiumDlgConversation> Conv = MakeShared<FElysiumDlgConversation>(
		File, /*bMale*/ true, /*bMalk*/ false,
		[](const FString&) { return true; },
		[&Ran](const FString& Action) { Ran.Add(Action); });
	Conv->Start();
	if (TestNotNull(TEXT("first spoken line remains active"), Conv->CurrentNpcLine()))
	{
		TestEqual(TEXT("the presented line is Alright"),
			Conv->CurrentNpcLine()->DisplayText(true, false), FString(TEXT("Alright.")));
	}
	TestTrue(TEXT("the Auto-Link is pending"), Conv->IsAwaitingAutomatic());
	TestFalse(TEXT("an automatic turn is not terminal"), Conv->IsTerminalLine());
	TestEqual(TEXT("no synthetic or ordinary response is visible"), Conv->VisibleChoices().Num(), 0);
	if (TestNotNull(TEXT("the pending control row is inspectable"), Conv->PendingAutomatic()))
	{
		TestEqual(TEXT("the first passing automatic row wins"), Conv->PendingAutomatic()->Id, 3);
	}
	const uint32 WaitingRevision = Conv->Revision();
	Conv->Choose(0);
	TestEqual(TEXT("choice input cannot skip an automatic wait"), Conv->Revision(), WaitingRevision);
	TestTrue(TEXT("the automatic action has not run before completion"), Ran.IsEmpty());

	Conv->ResolveAutomatic();
	TestTrue(TEXT("Auto-Link action runs at resolution"), Ran.Contains(TEXT("AUTO_LINK")));
	if (TestNotNull(TEXT("Auto-Link reaches the next spoken line"), Conv->CurrentNpcLine()))
	{
		TestEqual(TEXT("the follow-up line is current"), Conv->CurrentNpcLine()->Id, 10);
	}
	TestTrue(TEXT("the follow-up Auto-End is independently pending"), Conv->IsAwaitingAutomatic());
	Conv->ResolveAutomatic();
	TestTrue(TEXT("Auto-End action runs at resolution"), Ran.Contains(TEXT("AUTO_END")));
	TestTrue(TEXT("Auto-End closes the conversation"), Conv->IsOver());

	// The world owns the timing join. Completing voice 1 advances exactly one automatic edge and
	// submits voice 2; it cannot cascade through Auto-End until that new handle also completes.
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dlg_automatic_test__");
	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Defs.Defs.Add(MoveTemp(Jack));
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // admit the NPC mind/body before dialogue acquires it
	FElysiumEntity* JackEntity = World.FindByName(TEXT("Jack"));
	if (!TestNotNull(TEXT("world fixture has Jack"), JackEntity))
	{
		return false;
	}

	Ran.Reset();
	TSharedRef<FElysiumDlgConversation> Timed = MakeShared<FElysiumDlgConversation>(
		File, true, false, [](const FString&) { return true; },
		[&Ran](const FString& Action) { Ran.Add(Action); });
	Timed->Start();
	World.OpenDialog(JackEntity->Handle, Timed);
	TestEqual(TEXT("opening Alright submits one voice"), Services.NumLiveVoices(), 1);
	if (!TestNotNull(TEXT("the timed conversation opens"), World.GetOpenDialog()))
	{
		return false;
	}
	World.Tick(0.1);
	TestEqual(TEXT("a live Alright voice keeps its line displayed"),
		World.GetOpenDialog()->CurrentNpcLine()->Id, 1);

	Services.CompleteAllVoices();
	World.Tick(0.2);
	if (TestNotNull(TEXT("voice completion keeps the conversation open"), World.GetOpenDialog()))
	{
		TestEqual(TEXT("voice 1 completion advances to the follow-up line"),
			World.GetOpenDialog()->CurrentNpcLine()->Id, 10);
	}
	TestEqual(TEXT("the follow-up line owns a new live voice"), Services.NumLiveVoices(), 1);
	World.Tick(0.3);
	TestNotNull(TEXT("the live follow-up voice prevents same-frame Auto-End"), World.GetOpenDialog());

	Services.CompleteAllVoices();
	World.Tick(0.4);
	TestNull(TEXT("Auto-End resolves only after the follow-up voice completes"), World.GetOpenDialog());
	TestTrue(TEXT("both automatic row actions ran once"),
		Ran.Num() == 2 && Ran[0] == TEXT("AUTO_LINK") && Ran[1] == TEXT("AUTO_END"));

	// An automatic row action is allowed to run arbitrary script and can replace the conversation
	// synchronously. The completion which invoked it must not then advance or close that new session.
	TSharedRef<FElysiumDlgFile> ReplacingFile = MakeShared<FElysiumDlgFile>();
	TestTrue(TEXT("re-entrant source fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		ElysiumDlgRow(101, TEXT("Old turn."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(102, TEXT("(Auto-Link)"), TEXT("103"), TEXT(""), TEXT("REPLACE")),
		ElysiumDlgRow(103, TEXT("Old follow-up."), TEXT("#"), TEXT(""), TEXT("")),
	}), ReplacingFile.Get()));
	ReplacingFile->SourcePath = TEXT("dlg/replacing.dlg");
	TSharedRef<FElysiumDlgFile> ReplacementFile = MakeShared<FElysiumDlgFile>();
	TestTrue(TEXT("re-entrant replacement fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
			ElysiumDlgRow(201, TEXT("Replacement remains."), TEXT("#"), TEXT(""), TEXT("")),
		}), ReplacementFile.Get()));
	ReplacementFile->SourcePath = TEXT("dlg/replacement.dlg");
	TSharedRef<FElysiumDlgConversation> Replacement = MakeShared<FElysiumDlgConversation>(
		ReplacementFile, true, false, [](const FString&) { return true; }, [](const FString&) {});
	Replacement->Start();
	TSharedRef<FElysiumDlgConversation> Replacing = MakeShared<FElysiumDlgConversation>(
		ReplacingFile, true, false, [](const FString&) { return true; },
		[&World, JackEntity, Replacement](const FString& Action)
		{
			if (Action == TEXT("REPLACE"))
			{
				World.OpenDialog(JackEntity->Handle, Replacement);
			}
		});
	Replacing->Start();
	World.OpenDialog(JackEntity->Handle, Replacing);
	Services.CompleteAllVoices();
	World.Tick(0.5);
	TestTrue(TEXT("the old completion cannot reclaim a synchronously replaced session"),
		World.GetOpenDialog() == &Replacement.Get());
	if (TestNotNull(TEXT("the replacement session remains presented"), World.GetOpenDialog()))
	{
		TestEqual(TEXT("the replacement line remains current"),
			World.GetOpenDialog()->CurrentNpcLine()->Id, 201);
	}
	World.CloseDialog(/*bSilent*/ true);

	return true;
}

// The world's half of a session: its identity, the teardown flush retail's `CDialog::Release`
// (`0x100e5240`) owes every path, the order `CDialog::Acquire` runs the opening line's col-4 in,
// and the two edges (`NPCNotifyDoneTalking`, a stale pick) that act on a band in flight.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueSessionIdentityTest,
	"Elysium.Substrate.Dialogue.SessionIdentity", GElysiumTestFlags)
bool FElysiumDialogueSessionIdentityTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dlg_session_identity__");
	for (const TCHAR* Name : { TEXT("Jack"), TEXT("Nines") })
	{
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = Name;
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));
	}
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);   // admit the NPC minds/bodies before dialogue acquires them
	FElysiumEntity* Jack = World.FindByName(TEXT("Jack"));
	FElysiumEntity* Nines = World.FindByName(TEXT("Nines"));
	if (!TestNotNull(TEXT("world fixture has Jack"), Jack)
		|| !TestNotNull(TEXT("world fixture has Nines"), Nines))
	{
		return false;
	}

	// One shared fixture builder: rows in, a conversation whose actions land in `Ran`.
	auto Build = [this](TArray<FString> Rows, const TCHAR* Path, TArray<FString>* Ran,
		TFunction<void(const FString&)> Extra = TFunction<void(const FString&)>())
		-> TSharedPtr<FElysiumDlgConversation>
	{
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		if (!TestTrue(TEXT("session fixture parses"),
			FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
		{
			return nullptr;
		}
		File->SourcePath = Path;
		return MakeShared<FElysiumDlgConversation>(File, /*bMale*/ true, /*bMalk*/ false,
			[](const FString&) { return true; },
			[Ran, Extra](const FString& Action)
			{
				Ran->Add(Action);
				if (Extra)
				{
					Extra(Action);
				}
			});
	};

	// --- S7: the session serial -----------------------------------------------------------
	TestEqual(TEXT("no open dialogue has serial 0"),
		static_cast<int32>(World.GetOpenDialogSerial()), 0);
	TArray<FString> RanA;
	TSharedPtr<FElysiumDlgConversation> ConvA = Build({
		ElysiumDlgRow(11, TEXT("Jack speaks."), TEXT("#"), TEXT(""), TEXT("A_COL5")),
		ElysiumDlgRow(12, TEXT("Answer."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(13, TEXT("Other answer."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Jack again."), TEXT("#"), TEXT(""), TEXT("")),
	}, TEXT("dlg/test/session_a.dlg"), &RanA);
	if (!ConvA.IsValid())
	{
		return false;
	}
	World.OpenDialog(Jack->Handle, ConvA.ToSharedRef());
	const uint32 SerialA = World.GetOpenDialogSerial();
	TestTrue(TEXT("an open session has a non-zero serial"), SerialA != 0u);
	TestTrue(TEXT("the NPC line is live and its col-5 still parked"),
		ConvA->HasPendingNpcAction());
	TestFalse(TEXT("...so it has not run"), RanA.Contains(TEXT("A_COL5")));

	// --- S4: replacing a session mid-line still flushes the displaced NPC's col-5 -----------
	TArray<FString> RanB;
	TSharedPtr<FElysiumDlgConversation> ConvB = Build({
		ElysiumDlgRow(31, TEXT("Nines speaks."), TEXT("#"), TEXT(""), TEXT("B_COL5")),
		ElysiumDlgRow(32, TEXT("Answer."), TEXT("0"), TEXT(""), TEXT("")),
	}, TEXT("dlg/test/session_b.dlg"), &RanB);
	if (!ConvB.IsValid())
	{
		return false;
	}
	World.OpenDialog(Nines->Handle, ConvB.ToSharedRef());
	TestEqual(TEXT("the displaced NPC's parked col-5 runs exactly once"),
		RanA.FilterByPredicate([](const FString& A) { return A == TEXT("A_COL5"); }).Num(), 1);
	TestTrue(TEXT("...and the displaced conversation is over"), ConvA->IsOver());
	const uint32 SerialB = World.GetOpenDialogSerial();
	TestTrue(TEXT("a second session takes a fresh serial"), SerialB != 0u && SerialB != SerialA);
	TestTrue(TEXT("the replacement owns the panel"),
		World.GetOpenDialogOwner() == Nines->Handle);

	// Tearing the replacement down flushes its own parked col-5 through the same door.
	World.CloseDialog(/*bSilent*/ true);
	TestEqual(TEXT("the teardown flush is the same one, once"),
		RanB.FilterByPredicate([](const FString& A) { return A == TEXT("B_COL5"); }).Num(), 1);
	TestEqual(TEXT("a closed session reports serial 0"),
		static_cast<int32>(World.GetOpenDialogSerial()), 0);
	TestNull(TEXT("...and no conversation"), World.GetOpenDialog());

	// --- S10b: the opening line's col-4 acts on THIS session --------------------------------
	uint32 SerialSeenByColumn4 = 0;
	FElysiumEntityHandle OwnerSeenByColumn4;
	TArray<FString> RanC;
	TSharedPtr<FElysiumDlgConversation> ConvC = Build({
		ElysiumDlgRow(41, TEXT("Opening."), TEXT("#"), TEXT("OPEN_PROBE"), TEXT("")),
		ElysiumDlgRow(42, TEXT("Answer."), TEXT("0"), TEXT(""), TEXT("")),
	}, TEXT("dlg/test/session_c.dlg"), &RanC,
		[&World, &SerialSeenByColumn4, &OwnerSeenByColumn4](const FString& Action)
		{
			if (Action == TEXT("OPEN_PROBE"))
			{
				SerialSeenByColumn4 = World.GetOpenDialogSerial();
				OwnerSeenByColumn4 = World.GetOpenDialogOwner();
			}
		});
	if (!ConvC.IsValid())
	{
		return false;
	}
	World.OpenDialog(Jack->Handle, ConvC.ToSharedRef());
	TestTrue(TEXT("the opening col-4 ran"), RanC.Contains(TEXT("OPEN_PROBE")));
	TestTrue(TEXT("...with this conversation's own session already installed"),
		SerialSeenByColumn4 != 0u && SerialSeenByColumn4 == World.GetOpenDialogSerial());
	TestTrue(TEXT("...owned by the NPC being opened"), OwnerSeenByColumn4 == Jack->Handle);
	World.CloseDialog(/*bSilent*/ true);

	// The same order is what lets an opening col-4 end its own conversation instead of the one it
	// replaced: `EndDialog` in the opener acts on the session this call built.
	TArray<FString> RanD;
	TSharedPtr<FElysiumDlgConversation> ConvD = Build({
		ElysiumDlgRow(51, TEXT("Never seen."), TEXT("#"), TEXT("END_ME"), TEXT("")),
		ElysiumDlgRow(52, TEXT("Answer."), TEXT("0"), TEXT(""), TEXT("")),
	}, TEXT("dlg/test/session_d.dlg"), &RanD,
		[&World](const FString& Action)
		{
			if (Action == TEXT("END_ME"))
			{
				World.CloseDialog(/*bSilent*/ true);
			}
		});
	if (!ConvD.IsValid())
	{
		return false;
	}
	World.OpenDialog(Jack->Handle, ConvD.ToSharedRef());
	TestNull(TEXT("an opening col-4 that ends the dialogue leaves nothing open"),
		World.GetOpenDialog());
	TestEqual(TEXT("...and no serial"), static_cast<int32>(World.GetOpenDialogSerial()), 0);
	TestTrue(TEXT("...on the conversation it belonged to"), ConvD->IsOver());

	// --- S8: a pick that names a line the band no longer holds is refused --------------------
	TArray<FString> RanE;
	TSharedPtr<FElysiumDlgConversation> ConvE = Build({
		ElysiumDlgRow(61, TEXT("Speaking."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(62, TEXT("First."), TEXT("71"), TEXT(""), TEXT("PICK_62")),
		ElysiumDlgRow(63, TEXT("Second."), TEXT("71"), TEXT(""), TEXT("PICK_63")),
		ElysiumDlgRow(71, TEXT("Next."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(72, TEXT("Bye."), TEXT("0"), TEXT(""), TEXT("PICK_72")),
	}, TEXT("dlg/test/session_e.dlg"), &RanE);
	if (!ConvE.IsValid())
	{
		return false;
	}
	World.OpenDialog(Jack->Handle, ConvE.ToSharedRef());
	if (!TestNotNull(TEXT("the pick fixture opened"), World.GetOpenDialog()))
	{
		return false;
	}
	const uint32 BeforePick = World.GetOpenDialog()->Revision();
	World.PlayerDialogChoose(0, /*ExpectedLineId*/ 63);   // index 0 is line 62, not 63
	TestEqual(TEXT("a stale pick changes no turn"), World.GetOpenDialog()->Revision(), BeforePick);
	TestEqual(TEXT("...and stays on the same line"),
		World.GetOpenDialog()->CurrentNpcLine()->Id, 61);
	TestTrue(TEXT("...running neither row's col-5"), RanE.IsEmpty());
	World.PlayerDialogChoose(2, /*ExpectedLineId*/ 62);   // out of range: nothing at index 2
	TestEqual(TEXT("an out-of-range checked pick is refused too"),
		World.GetOpenDialog()->Revision(), BeforePick);

	World.PlayerDialogChoose(0, /*ExpectedLineId*/ 62);
	if (TestNotNull(TEXT("the matching pick is taken"), World.GetOpenDialog()))
	{
		TestEqual(TEXT("...and advances the turn"), World.GetOpenDialog()->CurrentNpcLine()->Id, 71);
	}
	TestTrue(TEXT("...running exactly that row's col-5"),
		RanE.Num() == 1 && RanE[0] == TEXT("PICK_62"));
	// The console path keeps picking by position with no expectation at all.
	World.PlayerDialogChoose(0);
	TestNull(TEXT("an unchecked pick still works (the console path)"), World.GetOpenDialog());
	TestTrue(TEXT("...and ran the row it named"), RanE.Contains(TEXT("PICK_72")));

	// --- S10a: the skip resolves a pending automatic row rather than forcing a Continue -----
	TArray<FString> RanF;
	TSharedPtr<FElysiumDlgConversation> ConvF = Build({
		ElysiumDlgRow(81, TEXT("Alright."), TEXT("#"), TEXT(""), TEXT("NPC_COL5")),
		ElysiumDlgRow(82, TEXT("(Auto-Link)"), TEXT("91"), TEXT(""), TEXT("AUTO_LINK")),
		ElysiumDlgRow(91, TEXT("Step out back."), TEXT("#"), TEXT(""), TEXT("")),
	}, TEXT("dlg/test/session_f.dlg"), &RanF);
	if (!ConvF.IsValid())
	{
		return false;
	}
	World.OpenDialog(Jack->Handle, ConvF.ToSharedRef());
	TestEqual(TEXT("the automatic turn submits a voice"), Services.NumLiveVoices(), 1);
	TestTrue(TEXT("the Auto-Link waits on that voice"), ConvF->IsAwaitingAutomatic());
	TestFalse(TEXT("no Continue is offered while the voice runs"),
		World.CanPlayerAdvanceAutomatic());

	// M-SKIP is retail's `NPCNotifyDoneTalking` edge: it flushes the parked col-5 AND takes the
	// turn's automatic continuation. Cutting the voice must not degrade into the no-audio forced
	// response (`process_pc_line` `0x100e8520`), which would cost the player a second key.
	World.PlayerDialogSkip();
	TestTrue(TEXT("the skip flushed the parked col-5"), RanF.Contains(TEXT("NPC_COL5")));
	TestTrue(TEXT("...and resolved the Auto-Link"), RanF.Contains(TEXT("AUTO_LINK")));
	if (TestNotNull(TEXT("the conversation is still open"), World.GetOpenDialog()))
	{
		TestEqual(TEXT("...on the line the Auto-Link led to"),
			World.GetOpenDialog()->CurrentNpcLine()->Id, 91);
	}
	TestFalse(TEXT("no forced Continue is left behind"), World.CanPlayerAdvanceAutomatic());
	World.CloseDialog(/*bSilent*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueBodySceneTest,
	"Elysium.Substrate.Dialogue.BodyScene", GElysiumTestFlags)
bool FElysiumDialogueBodySceneTest::RunTest(const FString&)
{
	ElysiumScene::ClearCache();
	ON_SCOPE_EXIT { ElysiumScene::ClearCache(); };

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("Jack dialogue fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		ElysiumDlgRow(11, TEXT("Opening line."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Continue."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Follow-up line."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(22, TEXT("Done."), TEXT("0"), TEXT(""), TEXT("")),
	}), File.Get())))
	{
		return false;
	}
	File->SourcePath = TEXT("dlg/main characters/jack_tutorial.dlg");

	auto RegisterLineScene = [&File](int32 LineId, const FString& Clip)
	{
		const FString Key = FPaths::SetExtension(
			FElysiumLineService::DialogueLineSource(File->SourcePath, LineId), TEXT("vcd"));
		const FString Text = FString::Printf(
			TEXT("actor \"Jack\"\n{\n")
			TEXT(" channel \"Speech\"\n {\n  event speak \"NPC Line\"\n  {\n")
			TEXT("   time 0.0 1.5\n   param \"character/dlg/test/line%d_col_e.wav\"\n")
			TEXT("  }\n }\n channel \"Gestures\"\n {\n  event gesture \"body\"\n  {\n")
			TEXT("   time 0.0 2.0\n   param \"%s\"\n  }\n }\n}\n"),
			LineId, *Clip);
		ElysiumScene::RegisterInline(Key, Text);
	};
	RegisterLineScene(11, TEXT("Smiling_Jack_line11_col_E"));
	RegisterLineScene(21, TEXT("Smiling_Jack_line21_col_E"));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ClipSeconds = 2.533333f; // Jack's authored shared-male waveover01 duration
	Services.StanceClips.Idle[0] = TEXT("Stance_Neutral_Idle_1");
	Services.StanceClips.Idle[1] = TEXT("Stance_Neutral_Idle_2");
	Services.StanceClips.Idle[2] = TEXT("Stance_Neutral_Idle_3");
	ElysiumStance::ApplyPrecacheFallbacks(Services.StanceClips);
	FElysiumDisposition Neutral;
	Neutral.Name = TEXT("Neutral");
	Neutral.AnimName = TEXT("Neutral");
	FElysiumDisposition Joy;
	Joy.Name = TEXT("Joy");
	Joy.AnimName = TEXT("Joy");
	Services.DispositionRows.Add(TEXT("neutral|1"), Neutral);
	Services.DispositionRows.Add(TEXT("joy|1"), Joy);
	// This model authors the Neutral<->Joy cross-disposition transition; HasNpcClip gates
	// SetDisposition's PlayNpcClip attempt on it (B4).
	Services.KnownNpcClips.Add(TEXT("smiling_jack"), { TEXT("Stance_Trans_Neutral_1_Joy_1") });

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dialogue_body_scene_test__");
	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Keys.Add(TEXT("model"),
		TEXT("models/character/npc/unique/smiling_jack/smiling_jack.mdl"));
	// Jack starts Neutral so the disposition assertion below exercises a real Neutral -> Joy
	// transition: with no key here `Disposition` is empty, its row does not resolve, and the
	// transition bails on an empty old stance before it names a clip.
	Jack.Keys.Add(TEXT("default_disposition"), TEXT("Neutral"));
	Defs.Defs.Add(MoveTemp(Jack));
	FElysiumEntityDef Waveover;
	Waveover.Classname = TEXT("scripted_sequence");
	Waveover.TargetName = TEXT("sJack_waveover");
	Waveover.Keys.Add(TEXT("m_iszEntity"), TEXT("Jack"));
	Waveover.Keys.Add(TEXT("m_iszPlay"), TEXT("waveover01"));
	Waveover.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(Waveover));
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // admit the NPC mind before dialogue acquires the body
	FElysiumEntity* JackEntity = World.FindByName(TEXT("Jack"));
	FElysiumEntity* WaveoverEntity = World.FindByName(TEXT("sJack_waveover"));
	if (!TestNotNull(TEXT("world fixture has Jack"), JackEntity)
		|| !TestNotNull(TEXT("world fixture has Jack's waveover beat"), WaveoverEntity))
	{
		return false;
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), WaveoverEntity->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("beat two starts Jack's authored waveover"),
		Services.Count(TEXT("PlayNpcClip smiling_jack waveover01 loop=0")), 1);
	TestTrue(TEXT("the waveover beat claims Jack until dialogue interrupts it"),
		JackEntity->ScriptOwner == WaveoverEntity->Handle);

	TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
		File, true, false, [](const FString&) { return true; }, [](const FString&) {});
	Conversation->Start();
	Services.Calls.Reset();
	World.OpenDialog(JackEntity->Handle, Conversation);
	TestFalse(TEXT("dialogue cancels the older waveover body claim"), JackEntity->ScriptOwner.IsSet());
	TestTrue(TEXT("the cancelled waveover has no delayed action deadline"),
		WaveoverEntity->SaveBlockReason() == nullptr);
	TestTrue(TEXT("line 11 owns Jack's body immediately"),
		World.HasActiveDialogueBodyClip(JackEntity->Handle));
	TestEqual(TEXT("line 11 submits its authored body clip exactly once"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Smiling_Jack_line11_col_E loop=0")), 1);
	const int32 IdleRefreshesAfterHandoff = Services.Count(TEXT("RefreshNpcIdle smiling_jack"));
	World.Tick(3.0); // beyond waveover01's old deadline, while the dialogue gesture remains live
	TestEqual(TEXT("the cancelled beat cannot reset the newer dialogue clip at its old deadline"),
		Services.Count(TEXT("RefreshNpcIdle smiling_jack")), IdleRefreshesAfterHandoff);
	TestTrue(TEXT("the dialogue body scene survives the old waveover deadline"),
		World.HasActiveDialogueBodyClip(JackEntity->Handle));

	World.Tick(3.1);
	TestEqual(TEXT("the dialogue stance think cannot overwrite a live line clip"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Stance_Neutral_Idle_1")), 0);
	TestTrue(TEXT("the instanced scene seeks the body on its own clock"),
		Services.Saw(TEXT("SeekCinematicClip 0.100")));

	World.PlayerDialogChoose(0);
	TestEqual(TEXT("advancing replaces line 11 with line 21's body clip"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Smiling_Jack_line21_col_E loop=0")), 1);
	TestTrue(TEXT("line replacement releases the outgoing pose to the stance path"),
		Services.Saw(TEXT("RefreshNpcIdle smiling_jack")));

	World.CloseDialog(/*bSilent=*/true);
	TestFalse(TEXT("dialogue close releases the active body scene"),
		World.HasActiveDialogueBodyClip(JackEntity->Handle));
	const int32 SeeksAtClose = Services.Count(TEXT("SeekCinematicClip"));
	World.Tick(3.2);
	TestEqual(TEXT("a closed line scene receives no stale body tick"),
		Services.Count(TEXT("SeekCinematicClip")), SeeksAtClose);

	// The dialogue owner is intentionally accepted by SetDisposition. IsFeedBusy() also includes
	// bInDialog for feed refusal, so the transition gate must ask the combat-character feed state
	// directly rather than rejecting every open conversation.
	TSharedRef<FElysiumDlgFile> DispositionFile = MakeShared<FElysiumDlgFile>();
	TestTrue(TEXT("disposition dialogue fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		ElysiumDlgRow(101, TEXT("No body event."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(102, TEXT("Done."), TEXT("0"), TEXT(""), TEXT("")),
	}), DispositionFile.Get()));
	DispositionFile->SourcePath = TEXT("dlg/main characters/disposition_dialog.dlg");
	const FString DispositionSceneKey = FPaths::SetExtension(
		FElysiumLineService::DialogueLineSource(DispositionFile->SourcePath, 101), TEXT("vcd"));
	ElysiumScene::RegisterInline(DispositionSceneKey,
		TEXT("actor \"Jack\"\n{\n channel \"Speech\"\n {\n  event speak \"line\"\n  {\n")
		TEXT("   time 0.0 1.0\n   param \"character/dlg/test/line101_col_e.wav\"\n  }\n }\n}\n"));
	TSharedRef<FElysiumDlgConversation> DispositionConversation = MakeShared<FElysiumDlgConversation>(
		DispositionFile, true, false, [](const FString&) { return true; }, [](const FString&) {});
	DispositionConversation->Start();
	World.OpenDialog(JackEntity->Handle, DispositionConversation);
	Services.Calls.Reset();
	TestTrue(TEXT("SetDisposition succeeds while dialogue owns the body"),
		static_cast<FElysiumAnimating*>(JackEntity)->SetDisposition(TEXT("Joy"), 1));
	TestEqual(TEXT("dialogue no longer makes the authored disposition transition unreachable"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Stance_Trans_Neutral_1_Joy_1 loop=0")), 1);
	World.CloseDialog(/*bSilent=*/true);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgStartingLineTest,
	"Elysium.Substrate.DlgStartingLine", GElysiumTestFlags)
bool FElysiumDlgStartingLineTest::RunTest(const FString&)
{
	// The retail classifier is a case-insensitive substring test over raw col-1 and accepts all three
	// spellings. These rows are PC-role records because their col-3 is the target NPC line.
	for (const TCHAR* Text : { TEXT("(Starting Condition)"), TEXT("prefix STARTING-CONDITION suffix"),
		TEXT("starting_condition") })
	{
		FElysiumDlgLine Line;
		Line.TextMale = Text;
		TestTrue(FString::Printf(TEXT("'%s' classifies as a starting sentinel"), Text),
			Line.IsStartingCondition());
	}
	FElysiumDlgLine Ordinary;
	Ordinary.TextMale = TEXT("A normal response");
	TestFalse(TEXT("ordinary dialogue is not a sentinel"), Ordinary.IsStartingCondition());

	// A passing dangling link does not stop the scan; the first later passing valid link wins, and a
	// still-later valid row cannot override it. The selected NPC action proves EnterNpcLine is reused.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(100, TEXT("(Starting Condition)"), TEXT("999"), TEXT("BAD_LINK"), TEXT("")));
	Rows.Add(ElysiumDlgRow(101, TEXT("(starting-condition)"), TEXT("30"), TEXT("FALSE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(102, TEXT("(STARTING_CONDITION)"), TEXT("20"), TEXT("FIRST_TRUE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(103, TEXT("(Starting Condition)"), TEXT("30"), TEXT("LATER_TRUE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(1, TEXT("Fallback."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(20, TEXT("Selected."), TEXT("#"), TEXT("ENTER_20"), TEXT("")));
	Rows.Add(ElysiumDlgRow(30, TEXT("Too late."), TEXT("#"), TEXT("ENTER_30"), TEXT("")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("selector fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}
	TArray<FString> Ran;
	FElysiumDlgConversation Ordered(File, true, false,
		[](const FString& Condition)
		{
			return Condition == TEXT("BAD_LINK") || Condition == TEXT("FIRST_TRUE")
				|| Condition == TEXT("LATER_TRUE");
		},
		[&Ran](const FString& Action) { Ran.Add(Action); });
	Ordered.Start();
	if (TestNotNull(TEXT("ordered selector opens"), Ordered.CurrentNpcLine()))
	{
		TestEqual(TEXT("first passing valid link wins"), Ordered.CurrentNpcLine()->Id, 20);
	}
	TestTrue(TEXT("selected line enters through the ordinary action path"), Ran.Contains(TEXT("ENTER_20")));
	TestFalse(TEXT("later passing row is not entered"), Ran.Contains(TEXT("ENTER_30")));

	// A usescript integer overrides line 1 when no sentinel passes; no usescript means line 1.
	FElysiumDlgConversation ScriptFallback(File, true, false,
		[](const FString&) { return false; }, [](const FString&) {},
		[]() -> TOptional<int32> { return 30; });
	ScriptFallback.Start();
	if (TestNotNull(TEXT("usescript fallback opens"), ScriptFallback.CurrentNpcLine()))
	{
		TestEqual(TEXT("usescript integer is the starting line"), ScriptFallback.CurrentNpcLine()->Id, 30);
	}

	FElysiumDlgConversation LineOneFallback(File, true, false,
		[](const FString&) { return false; }, [](const FString&) {});
	LineOneFallback.Start();
	if (TestNotNull(TEXT("line-1 fallback opens"), LineOneFallback.CurrentNpcLine()))
	{
		TestEqual(TEXT("absent usescript selects line 1"), LineOneFallback.CurrentNpcLine()->Id, 1);
	}

	// A used script that returns no integer produces 0 in retail. Acquire then substitutes the first
	// stored line id; use a file with neither line 0 nor line 1 to make that final fallback observable.
	TSharedRef<FElysiumDlgFile> FirstStored = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("first-stored fixture parses"), FElysiumDlgFile::ParseBytes(
		ElysiumDlgBytes({ ElysiumDlgRow(50, TEXT("First stored."), TEXT("#"), TEXT(""), TEXT("")) }),
		FirstStored.Get())))
	{
		return false;
	}
	FElysiumDlgConversation InvalidScriptResult(FirstStored, true, false,
		[](const FString&) { return false; }, [](const FString&) {},
		[]() -> TOptional<int32> { return 0; });
	InvalidScriptResult.Start();
	if (TestNotNull(TEXT("first-stored fallback opens"), InvalidScriptResult.CurrentNpcLine()))
	{
		TestEqual(TEXT("invalid usescript result falls back to first stored line"),
			InvalidScriptResult.CurrentNpcLine()->Id, 50);
	}

	return true;
}


// 9.3 CPython end-to-end — the writers and the two-phase spawn driven through the REAL
// Python glue (arg parsing, __getattr__ dispatch, the module globals), not just the C++
// substrate. Self-skips when the embedded VM is unavailable, so it never yields a false
// failure on a non-CPython build/host.


#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON
#endif // ELYSIUM_WITH_CPYTHON

} // namespace ElysiumDialogueTests

#endif // WITH_DEV_AUTOMATION_TESTS
