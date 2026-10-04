// The one-shot visibility contract, on a probe character, and the carrier's corpus discovery.
//
// The two cases that pinned the world-tick event poll (`Elysium.Arm.AnimEventWindow` over
// `ElysiumAnimEvents::Advance`, `Elysium.Substrate.AnimEventDispatch` over
// `FElysiumEntityWorld::AdvanceAnimEvents`) went with the poll (spec 0002 V4a, K3); the dispatcher
// that replaced it (`0x10091880`, `0x10098cd0`) is asserted in `ElysiumNpcKernelAnimEventsTests.cpp`.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

namespace ElysiumAnimEventTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr const TCHAR* GOwner = TEXT("cast_bank");
	constexpr const TCHAR* GLabel = TEXT("walk");
	// The probe classname. It is registered below purely so a test can stand an entity that CLAIMS
	// an id — no shipped class claims one this slice, and asserting the handler hop without one
	// would only assert that nothing happens.
	constexpr const TCHAR* GProbeClass = TEXT("elysium_animevent_probe");

	// A character that claims what it is given, so the handler hop is observable. It derives from
	// `FElysiumAnimating` rather than from an NPC leaf because the pass under test is CBaseAnimating's
	// and nothing about it needs AI, a sheet or a schedule.
	class FElysiumAnimEventProbe final : public FElysiumAnimating
	{
	public:
		// What reached `HandleAnimEvent`, in dispatch order, as `<id>:<options>` — the payload is
		// carried because a handler that received the record but not its argument would look
		// identical without it.
		TArray<FString> Handled;
		// Ids this probe refuses. A refusal has to be a per-id decision: the census's whole claim is
		// that an id a handler declined is counted, and a probe that either took everything or
		// nothing could not make that statement.
		TSet<int32> Refuse;

		virtual void Spawn() override
		{
			FElysiumAnimating::Spawn();
			BuildBody();
		}
		virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override
		{
			if (Refuse.Contains(Event.Event))
			{
				return false;
			}
			Handled.Add(FString::Printf(TEXT("%d:%s"), Event.Event, *Event.Options));
			return true;
		}
	};

	TUniquePtr<FElysiumEntity> MakeProbe() { return MakeUnique<FElysiumAnimEventProbe>(); }

	// Registered once at module load, like every other class. It is a development-only translation
	// unit (`WITH_DEV_AUTOMATION_TESTS`), so the name never exists in a shipped registry, and no
	// shipped map or script spells it.
	struct FProbeRegistrar
	{
		FProbeRegistrar()
		{
			FElysiumClassRegistry::Get().Register(FName(GProbeClass), ElysiumAnimatingClassName(),
				&MakeProbe);
		}
	};
	static FProbeRegistrar GProbeRegistrar;

	FElysiumEntityDefs MakeProbeDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__animevent_test__");

		FElysiumEntityDef Bodied;
		Bodied.Classname = GProbeClass;
		Bodied.TargetName = TEXT("bodied");
		// A model, because the world's pass skips anything with no skeletal body — which is the
		// gate one of the cases below is about.
		Bodied.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Bodied));

		// The same class with no model: it spawns, it has no `Visual`, and the pass must not reach
		// it. Without this row "the pass reached only bodied entities" is unfalsifiable.
		FElysiumEntityDef Bodiless;
		Bodiless.Classname = GProbeClass;
		Bodiless.TargetName = TEXT("bodiless");
		Defs.Defs.Add(MoveTemp(Bodiless));

		// A plain non-animating entity, which carries no clip at all.
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter");
		Defs.Defs.Add(MoveTemp(Counter));
		return Defs;
	}

	FElysiumAnimEventProbe* FindProbe(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		return static_cast<FElysiumAnimEventProbe*>(World.FindByName(Name));
	}

	// --- the carrier acceptance's own corpus discovery (slice 3) -------------------------------
	//
	// Nothing in the shipped corpus is named here: the case walks the export until it finds a clip
	// that can answer the question, and abstains by name when none can. Naming one would tie a
	// runtime acceptance to which characters happen to be exported.

	// How many times an id reached the probe.
	int32 CountFires(const TArray<FString>& Handled, int32 Event)
	{
		const FString Prefix = FString::Printf(TEXT("%d:"), Event);
		int32 Count = 0;
		for (const FString& Line : Handled)
		{
			if (Line.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				++Count;
			}
		}
		return Count;
	}

	struct FCarrierPick
	{
		FString Stem;
		FString Owner;
		FString Label;
		USkeletalMesh* Mesh = nullptr;
		UAnimSequence* Clip = nullptr;
		float FadeSeconds = UElysiumBodyAnimInstance::DefaultBlendSeconds;
		// The clip's whole authored timeline, and the one record the case tracks through it.
		TArray<FElysiumAnimEvent> Records;
		int32 TargetEvent = 0;
		float TargetCycle = 0.f;
		// An id the clip's own timeline does not use, for the synthetic cycle-0 record.
		int32 FirstFrameEvent = 0;
	};

	// The frames a clip has to have. Short enough that walking it at 1/30 is a handful of ticks, and
	// long enough that a record inside it lands on a frame of its own rather than sharing one with
	// the arm.
	constexpr int32 GMinEventClipFrames = 4;
	constexpr int32 GMaxEventClipFrames = 16;
	// How late in the clip the tracked record may sit. The walk stops before the montage's own end —
	// a one-shot that finishes hands the base back to the blend stack, which is a different play —
	// so a record in the last quarter would need a walk that runs past the thing it is measuring.
	constexpr float GMaxEventCycle = 0.7f;

	// The first (stem, owner, label) in the export whose baked clip carries a walkable timeline.
	//
	// **The timeline belongs to the OWNER, not to the body.** A VtMB character's combat and
	// locomotion clips come out of shared banks through the include DAG, and the event table is
	// written beside the sequence in the bank's own sidecar — so this resolves each label's owner
	// through the body's vocabulary and then asks that owner's table, exactly as
	// `UElysiumEntityBodies::GetNpcEventTimeline` does. Walking a body's own blend sidecar alone
	// finds the seven models in the shipped corpus that author their own events and none of the
	// forty banks that do.
	//
	// Five things have to hold, and each excludes a real corpus case rather than a hypothetical one:
	// the label must name no blend grid (a grid resolves to a cell rather than to one sequence), the
	// clip must not be an additive, it must carry a record strictly inside `(0, GMaxEventCycle]` (a
	// record at 0 is the synthetic one's job and one at exactly 1 is unreachable by construction),
	// that record's id must be BELOW the server dispatch ceiling and unique on the timeline so the
	// probe is offered it and a fire count is unambiguous, and the clip must be on the mount.
	bool FindEventClip(FCarrierPick& Out, FString& OutWhy)
	{
		FElysiumNpcIndex Index;
		FString Error;
		if (!ElysiumNativeTest::Load(Index, Error) || !Index.IsValid())
		{
			OutWhy = TEXT("no native cast view (run: uv run elysium import characters)");
			return false;
		}

		// One parse per owner, cached: a shared bank owns clips for most of the cast, and the two
		// halves of the index are consulted because an owner may be a character or a bank.
		TMap<FString, TSharedPtr<FElysiumBlendTable>> Tables;
		const auto TableFor = [&Index, &Tables](const FString& Owner) -> const FElysiumBlendTable*
		{
			if (const TSharedPtr<FElysiumBlendTable>* Found = Tables.Find(Owner))
			{
				return Found->Get();
			}
			const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Owner);
			if (Entry == nullptr) { Entry = Index.Banks.Find(Owner); }
			TSharedPtr<FElysiumBlendTable> Table;
			if (Entry != nullptr && Entry->EventSequences > 0 && !Entry->Blends.IsEmpty())
			{
				Table = MakeShared<FElysiumBlendTable>();
				FString TableError;
				if (!ElysiumNativeTest::Load(*Table, Entry->Blends, TableError))
				{
					Table.Reset();
				}
			}
			Tables.Add(Owner, Table);
			return Table.Get();
		};

		TArray<FString> Stems;
		Index.Npcs.GenerateKeyArray(Stems);
		Stems.Sort();
		int32 Considered = 0;
		for (const FString& Stem : Stems)
		{
			FElysiumNpcClipSet Vocabulary;
			if (!ElysiumNativeTest::Load(Vocabulary, Stem, Error))
			{
				continue;
			}
			TArray<FString> Labels;
			Vocabulary.Clips.GetKeys(Labels);
			Labels.Sort([](const FString& A, const FString& B) { return A < B; });

			for (const FString& Label : Labels)
			{
				const FElysiumNpcClip& Clip = Vocabulary.Clips[Label];
				if (Clip.IsAdditive()
					|| Clip.Frames < GMinEventClipFrames || Clip.Frames > GMaxEventClipFrames)
				{
					continue;
				}
				const FString Owner = Clip.IsOwnedBy(Stem) ? Stem : Clip.Owner;
				const FElysiumBlendTable* Table = TableFor(Owner);
				const TArray<FElysiumAnimEvent>* Records =
					Table != nullptr ? Table->FindEvents(Label) : nullptr;
				if (Records == nullptr || Records->IsEmpty() || Table->Find(Label) != nullptr)
				{
					continue;
				}
				++Considered;

				int32 TargetIndex = INDEX_NONE;
				for (int32 i = 0; i < Records->Num() && TargetIndex == INDEX_NONE; ++i)
				{
					const FElysiumAnimEvent& Record = (*Records)[i];
					if (Record.Cycle <= 0.f || Record.Cycle > GMaxEventCycle
						|| Record.Event >= ElysiumAnimEvents::ServerDispatchCeiling)
					{
						continue;
					}
					int32 Sharing = 0;
					for (const FElysiumAnimEvent& Other : *Records)
					{
						Sharing += Other.Event == Record.Event ? 1 : 0;
					}
					TargetIndex = Sharing == 1 ? i : INDEX_NONE;
				}
				if (TargetIndex == INDEX_NONE)
				{
					continue;
				}

				USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
				UAnimSequence* Baked = Mesh != nullptr
					? ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, Label) : nullptr;
				if (Baked == nullptr || Baked->GetPlayLength() <= 0.f)
				{
					continue;
				}

				Out.Stem = Stem;
				Out.Owner = Owner;
				Out.Label = Label;
				Out.Mesh = Mesh;
				Out.Clip = Baked;
				Out.FadeSeconds = Clip.FadeSeconds();
				Out.Records = *Records;
				Out.TargetEvent = (*Records)[TargetIndex].Event;
				Out.TargetCycle = (*Records)[TargetIndex].Cycle;
				// Below the ceiling so the probe is actually offered it, and unused by this clip so
				// its fires can be counted apart from the authored record's.
				Out.FirstFrameEvent = ElysiumAnimEvents::ServerDispatchCeiling - 1;
				for (bool bTaken = true; bTaken && Out.FirstFrameEvent > 0; )
				{
					bTaken = false;
					for (const FElysiumAnimEvent& Record : *Records)
					{
						bTaken = bTaken || Record.Event == Out.FirstFrameEvent;
					}
					Out.FirstFrameEvent -= bTaken ? 1 : 0;
				}
				return true;
			}
		}

		OutWhy = FString::Printf(
			TEXT("no exported body carries a %d-%d frame non-grid clip whose baked timeline has a "
			     "uniquely-identified server-band record in (0, %.2f] — %d label(s) considered; "
			     "run: uv run elysium import characters"),
			GMinEventClipFrames, GMaxEventClipFrames, GMaxEventCycle, Considered);
		return false;
	}
}

// The one-shot visibility contract.

// A SIBLING of `AnimEventDispatch`, never a child of it: the automation tree treats a name that is
// also a prefix as a group, and the leaf under it stops being discoverable.
#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOneShotVisibilityTest,
	"Elysium.Arm.AnimEventVisibility", GElysiumTestFlags)
bool FElysiumOneShotVisibilityTest::RunTest(const FString&)
{
	// The rule itself, where it lives. Both the body factory's own tail and the recording double
	// branch on this one function, so asserting it here is asserting what the runtime does rather
	// than a copy of it.
	TestTrue(TEXT("the Slot route forces its body visible"),
		ElysiumAnimIntent::OneShotForcesVisibility(EElysiumOneShotRoute::Slot));
	TestFalse(TEXT("an involuntary Reaction does not"),
		ElysiumAnimIntent::OneShotForcesVisibility(EElysiumOneShotRoute::Reaction));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeProbeDefs());
	World.Activate(0.0);

	FElysiumAnimEventProbe* Probe = FindProbe(World, TEXT("bodied"));
	if (!TestNotNull(TEXT("the probe exists"), Probe)
		|| !TestNotNull(TEXT("...carrying a body"), Probe->Visual))
	{
		return false;
	}
	Services.bNpcOneShotsPlay = true;

	// A body taken off screen the way `IElysiumNpcMotor::SetEnabled(false)` takes one off screen:
	// that call hides as well as immobilises (`Source/ElysiumUE/CLAUDE.md` → Engine gotchas), which
	// is exactly the state a flinch must not undo.
	Probe->Visual->SetVisibility(false, true);

	FElysiumOneShotClipRequest Flinch;
	Flinch.OwnerStem = GOwner;
	Flinch.AnimationName = TEXT("hit_torso_left");
	Flinch.Label = TEXT("hit_torso");
	Flinch.Route = EElysiumOneShotRoute::Reaction;
	Flinch.Source = EElysiumAnimSource::Damage;
	Flinch.Priority = EElysiumAnimPriority::Reaction;
	TestTrue(TEXT("the reaction plays"),
		Services.PlayNpcOneShot(Probe->Visual, Flinch, nullptr));
	TestFalse(TEXT("a hidden body stays hidden through a flinch"), Probe->Visual->GetVisibleFlag());

	// The other half, so the assertion above is about the ROUTE and not about one-shots in general:
	// a Slot one-shot is armed by something that means the body to be seen, and still reveals it.
	FElysiumOneShotClipRequest Stance = Flinch;
	Stance.Route = EElysiumOneShotRoute::Slot;
	Stance.Source = EElysiumAnimSource::Npc;
	TestTrue(TEXT("the slot one-shot plays"),
		Services.PlayNpcOneShot(Probe->Visual, Stance, nullptr));
	TestTrue(TEXT("...and reveals the body, as the ordinary clip funnel does"),
		Probe->Visual->GetVisibleFlag());

	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS


// The carrier, end to end.

}   // namespace ElysiumAnimEventTests

#endif // WITH_DEV_AUTOMATION_TESTS
