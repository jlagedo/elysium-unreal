// VtMB's sequence-event dispatcher — the pure interval rule, the entity chain that walks it, and
// the carrier that feeds it.
//
// Every rule asserted here is a fact from `docs/vtmb/animation_and_movers.md` → "Sequence events and
// native dispatch": the recovered comparison `last_cycle <= event.cycle < current_cycle`, the single
// wrapped visit a looping sequence makes after passing 1.0, file order for records sharing a cycle,
// and the 5000 server dispatch ceiling.
//
// The two `Elysium.Substrate.AnimEvent*` cases are content-free: a timeline is a fixture on the
// stack and the phase comes from the recording double, so what they prove is the RULE and the walk.
// `Elysium.Content.SequenceEventCarrier` at the bottom is the other half — a real baked clip on the
// real generated graph, with the pose layer publishing the phase — because the seam between a
// montage and the pass that reads it cannot be asserted without both ends of it.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
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

	FElysiumAnimEvent Ev(float Cycle, int32 Event, const TCHAR* Options = TEXT(""))
	{
		FElysiumAnimEvent Record;
		Record.Cycle = Cycle;
		Record.Event = Event;
		Record.Options = Options;
		return Record;
	}

	FElysiumClipPhase PhaseAt(float Cycle, bool bLooping = true, uint32 PlayId = 1)
	{
		FElysiumClipPhase Phase;
		Phase.OwnerStem = GOwner;
		Phase.Label = GLabel;
		Phase.Cycle = Cycle;
		Phase.Length = 1.0f;
		Phase.bLooping = bLooping;
		Phase.PlayId = PlayId;
		return Phase;
	}

	// The same phase from a producer that found its clip already running — the blend stack, or an
	// arm resuming after a higher one displaced it. `AnchorCycle` is where its timeline was last
	// dispatched from rather than the zero a started clip carries.
	FElysiumClipPhase PhaseFrom(float Anchor, float Cycle, bool bLooping = true, uint32 PlayId = 1)
	{
		FElysiumClipPhase Phase = PhaseAt(Cycle, bLooping, PlayId);
		Phase.AnchorCycle = Anchor;
		return Phase;
	}

	// The ids a walk answered with, in order — the only thing an ordering assertion can compare.
	FString IdsOf(const TArray<const FElysiumAnimEvent*>& Fired)
	{
		FString Out;
		for (const FElysiumAnimEvent* Record : Fired)
		{
			if (!Out.IsEmpty()) { Out += TEXT(","); }
			Out += FString::FromInt(Record->Event);
		}
		return Out;
	}

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
		if (!Index.Load(Error) || !Index.IsValid())
		{
			OutWhy = TEXT("no exported npc index (run: uv run elysium export grid)");
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
				if (!Table->Load(Entry->Blends, TableError))
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
			if (!Vocabulary.Load(Stem, Error))
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
			     "run: uv run elysium export characters"),
			GMinEventClipFrames, GMaxEventClipFrames, GMaxEventCycle, Considered);
		return false;
	}
}

// The pure rule: which records an interval contains.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimEventWindowTest,
	"Elysium.Substrate.AnimEventWindow", GElysiumTestFlags)
bool FElysiumAnimEventWindowTest::RunTest(const FString&)
{
	TArray<const FElysiumAnimEvent*> Fired;
	{
		TArray<FElysiumAnimEvent> Timeline{Ev(0.1f,2050)};
		FElysiumAnimEventCursor Cursor;
		auto Phase=PhaseAt(0.5f);
		Phase.OwnerRoot=TEXT("Bip01");
		ElysiumAnimEvents::Advance(&Timeline,Phase,Cursor,Fired);
		Phase.OwnerRoot=TEXT("bip01");
		ElysiumAnimEvents::Advance(&Timeline,Phase,Cursor,Fired);
		TestTrue(TEXT("case change is the same cinematic actor"),Fired.IsEmpty());
		Phase.OwnerRoot=TEXT("Bip02");
		ElysiumAnimEvents::Advance(&Timeline,Phase,Cursor,Fired);
		TestEqual(TEXT("another actor with identical owner/label/play id has its own timeline"),IdsOf(Fired),FString(TEXT("2050")));
	}

	// --- The half-open window, at both boundaries -------------------------------------------------
	{
		// One record at each end of the interval under test. The recovered comparison is
		// `last <= cycle < current`, so 0.25 belongs to a frame that STARTS on it and 0.5 does not
		// belong to a frame that ends on it — it belongs to the next one.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.25f, 2050));
		Timeline.Add(Ev(0.50f, 2051));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.25f), Cursor, Fired);
		TestEqual(TEXT("[0, 0.25) excludes the record sitting on the upper bound"),
			IdsOf(Fired), FString());

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.50f), Cursor, Fired);
		TestEqual(TEXT("[0.25, 0.5) INCLUDES the lower bound and excludes the upper"),
			IdsOf(Fired), FString(TEXT("2050")));

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.75f), Cursor, Fired);
		TestEqual(TEXT("...and the upper-bound record fires on the next frame"),
			IdsOf(Fired), FString(TEXT("2051")));
	}

	// --- A record at cycle 0 fires on the first advance --------------------------------------------
	{
		// A new play anchors at zero and its first frame is `[0, Cycle)`, so a record authored at the
		// very start of a sequence is reached rather than stepped over. Retail's own `+0x658` starts
		// at zero for the same reason.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.0f, 2040));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.05f), Cursor, Fired);
		TestEqual(TEXT("a cycle-0 record fires on the first advance"),
			IdsOf(Fired), FString(TEXT("2040")));

		// And exactly once: the second frame's interval starts above it.
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.10f), Cursor, Fired);
		TestEqual(TEXT("...and not again on the next frame"), IdsOf(Fired), FString());
	}

	// --- A record at exactly 1.0 is unreachable ----------------------------------------------------
	{
		// The parser permits it — the exporter writes what the model declares — and the `< current`
		// half of the comparison excludes it from every possible interval, because a phase is
		// normalized and the widest window any frame presents is `[x, 1)`. Retail never fires one
		// either; this asserts the two agree rather than that the record is illegal.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(1.0f, 2052));
		Timeline.Add(Ev(0.999f, 2053));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.9995f), Cursor, Fired);
		TestEqual(TEXT("a record at 0.999 fires; the one at exactly 1.0 does not"),
			IdsOf(Fired), FString(TEXT("2053")));

		// Not even by wrapping: the tail half of a wrap is `[last, 1)`, which is still open at 1.
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.1f), Cursor, Fired);
		TestEqual(TEXT("...and the wrap does not reach it either"), IdsOf(Fired), FString());
	}

	// --- The wrap, visited exactly once ------------------------------------------------------------
	{
		// One record in the tail of the lap that ended and one in the head of the lap that began.
		// Both fire, tail first, on the single frame that crosses 1.0 — and neither fires twice.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.10f, 1));
		Timeline.Add(Ev(0.95f, 2));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.90f), Cursor, Fired);
		TestEqual(TEXT("the first lap fires the head record only"), IdsOf(Fired), FString(TEXT("1")));

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.20f), Cursor, Fired);
		TestEqual(TEXT("the wrapping frame fires the tail then the head, once each"),
			IdsOf(Fired), FString(TEXT("2,1")));

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.30f), Cursor, Fired);
		TestEqual(TEXT("...and the frame after it fires nothing"), IdsOf(Fired), FString());
	}

	// --- Equal cycles fire in FILE order -----------------------------------------------------------
	{
		// The array order is the model's own declaration order and is never sorted: retail scans the
		// 76-byte records in place, so three events at one cycle reach the handler in the order the
		// file lists them. Descending ids make a sort visible if one ever crept in.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.5f, 3005));
		Timeline.Add(Ev(0.5f, 2050));
		Timeline.Add(Ev(0.5f, 1003));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.4f), Cursor, Fired);
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.6f), Cursor, Fired);
		TestEqual(TEXT("records sharing a cycle fire in declaration order"),
			IdsOf(Fired), FString(TEXT("3005,2050,1003")));
	}

	// --- A PlayId change re-arms at zero -----------------------------------------------------------
	{
		// The same clip armed again is a NEW play: its timeline runs from the top. Nothing else on
		// the record can say so — the owner and the label are identical and the phase went backwards,
		// which on a looping clip would otherwise read as a wrap.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.1f, 2050));
		Timeline.Add(Ev(0.9f, 2051));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.95f, true, 1), Cursor, Fired);
		TestEqual(TEXT("the first play walks its whole timeline"),
			IdsOf(Fired), FString(TEXT("2050,2051")));

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.5f, true, 2), Cursor, Fired);
		TestEqual(TEXT("a new PlayId re-arms at 0 and fires [0, 0.5) rather than wrapping"),
			IdsOf(Fired), FString(TEXT("2050")));
		TestEqual(TEXT("...and the cursor now names that play"), Cursor.PlayId, 2u);

		// A different clip is the same statement through the other half of the identity.
		FElysiumClipPhase Other = PhaseAt(0.5f, true, 2);
		Other.Label = TEXT("run");
		ElysiumAnimEvents::Advance(&Timeline, Other, Cursor, Fired);
		TestEqual(TEXT("a different label re-arms at 0 too"), IdsOf(Fired), FString(TEXT("2050")));
	}

	// --- A HELD repeat keeps its play, and its timeline is not disturbed ---------------------------
	{
		// The counter-case to the block above, and the reason the arm follows the pose rather than the
		// request (`Visual/ElysiumBipedAnimInstance.cpp` → `PlayClip`). The ordinary ideal route
		// re-requests a clip it is already holding — the controlled crouch trace does it 151 times
		// across 1.791 s (`docs/vtmb/animation_and_movers.md`) — and the pose refuses to restart. The
		// arm therefore keeps its `PlayId` and its anchor, and what the cursor sees is one continuous
		// play: no record fires twice, and none is skipped. Arming a new `PlayId` on a clip that did
		// not restart is what would put a footstep under a body that never took a step.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.0f, 2040));
		Timeline.Add(Ev(0.3f, 2050));
		Timeline.Add(Ev(0.7f, 2051));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.40f, true, 1), Cursor, Fired);
		TestEqual(TEXT("the held play walks up to 0.40"), IdsOf(Fired), FString(TEXT("2040,2050")));

		// The re-request. Nothing about the published phase changes except the clock the clip kept
		// advancing, because nothing about the play changed.
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.50f, true, 1), Cursor, Fired);
		TestEqual(TEXT("a repeat the pose refused re-fires nothing behind it"),
			IdsOf(Fired), FString());
		TestEqual(TEXT("...and the cursor still names the play it was already walking"),
			Cursor.PlayId, 1u);

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.80f, true, 1), Cursor, Fired);
		TestEqual(TEXT("...and the record ahead of it still fires, exactly once"),
			IdsOf(Fired), FString(TEXT("2051")));

		// And the restart route, on the same clip, is the visible difference: a new `PlayId` is the
		// only thing that can say a repeated request was a new play.
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.10f, true, 2), Cursor, Fired);
		TestEqual(TEXT("a restart-route repeat runs the timeline again from zero"),
			IdsOf(Fired), FString(TEXT("2040")));
	}

	// --- A zero-delta frame fires nothing ----------------------------------------------------------
	{
		// A paused clip, a fully faded one, or a second read in the same frame. The interval is
		// empty, and firing on it would double every event a stalled body sits on.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.4f, 2050));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.5f), Cursor, Fired);
		TestEqual(TEXT("the first frame reaches the record"), IdsOf(Fired), FString(TEXT("2050")));
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.5f), Cursor, Fired);
		TestEqual(TEXT("a zero-delta frame fires nothing"), IdsOf(Fired), FString());
	}

	// --- A backwards phase on a NON-looping clip re-anchors ----------------------------------------
	{
		// That is a seek, not a lap: a one-shot cannot have wrapped, so replaying `[last, 1)` would
		// fire a footstep for a step the body never took. The cursor simply moves.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.2f, 2050));
		Timeline.Add(Ev(0.8f, 2051));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.9f, /*bLooping=*/false), Cursor, Fired);
		TestEqual(TEXT("the one-shot walks forward normally"),
			IdsOf(Fired), FString(TEXT("2050,2051")));

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.5f, /*bLooping=*/false), Cursor, Fired);
		TestEqual(TEXT("a backwards phase on a one-shot fires nothing"), IdsOf(Fired), FString());
		TestEqual(TEXT("...and re-anchors the cursor where the seek landed"), Cursor.LastCycle, 0.5f);

		// Forward from the seek fires only what lies ahead of it.
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.9f, /*bLooping=*/false), Cursor, Fired);
		TestEqual(TEXT("...so the next frame fires only the record past the seek"),
			IdsOf(Fired), FString(TEXT("2051")));
	}

	// --- No timeline: advance silently -------------------------------------------------------------
	{
		// Most sequences declare none, which is an absence rather than a fault. The cursor still
		// moves, so a clip whose timeline is resolved a frame later cannot fire a backlog.
		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(nullptr, PhaseAt(0.6f), Cursor, Fired);
		TestEqual(TEXT("a null timeline fires nothing"), IdsOf(Fired), FString());
		TestTrue(TEXT("...but the cursor is armed"), Cursor.bArmed);
		TestEqual(TEXT("...and stands where the clip does"), Cursor.LastCycle, 0.6f);

		const TArray<FElysiumAnimEvent> Empty;
		ElysiumAnimEvents::Advance(&Empty, PhaseAt(0.7f), Cursor, Fired);
		TestEqual(TEXT("an empty timeline is the same absence"), IdsOf(Fired), FString());
		TestEqual(TEXT("...and advances the cursor with it"), Cursor.LastCycle, 0.7f);
	}

	// --- A phase naming no clip forgets where the cursor was ---------------------------------------
	{
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.5f, 2050));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.9f), Cursor, Fired);
		TestTrue(TEXT("the cursor is armed on a real clip"), Cursor.bArmed);

		ElysiumAnimEvents::Advance(&Timeline, FElysiumClipPhase(), Cursor, Fired);
		TestEqual(TEXT("a phase naming no clip fires nothing"), IdsOf(Fired), FString());
		TestFalse(TEXT("...and disarms the cursor, so the next clip cannot inherit its position"),
			Cursor.bArmed);
	}

	// --- A non-finite phase does not retire the timeline -------------------------------------------
	{
		// `FMath::Clamp` passes a NaN through both of its comparisons, so a cycle divided out of a
		// length-zero clip would otherwise be STORED on the cursor — where it fails `>` and `<` alike
		// and silently stops that clip firing for the rest of the play. The rule anchors it at zero
		// instead, so the very next honest frame walks a real interval.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.3f, 2050));

		// Built from its bit pattern rather than from an expression, so no compiler folds it away.
		float NotANumber = 0.0f;
		const uint32 QuietNanBits = 0x7FC00000u;
		FMemory::Memcpy(&NotANumber, &QuietNanBits, sizeof(NotANumber));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(NotANumber), Cursor, Fired);
		TestEqual(TEXT("a non-finite phase fires nothing"), IdsOf(Fired), FString());
		TestEqual(TEXT("...and anchors the cursor at zero rather than storing the NaN"),
			Cursor.LastCycle, 0.0f);

		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.5f), Cursor, Fired);
		TestEqual(TEXT("...so the next honest frame walks [0, 0.5) normally"),
			IdsOf(Fired), FString(TEXT("2050")));
	}

	// --- A play found MID-FLIGHT resumes from its anchor, not from zero ----------------------------
	{
		// The producer that cannot start anything: the locomotion blend stack is already advancing
		// its clip by the time anything looks at it, and an arm a higher-priority pose displaced has
		// been advancing the whole time it was off. Both present a play the cursor has never seen at
		// a cycle well past zero, and seeding `[0, cycle)` for them fires every record behind the
		// current phase in one burst — a phantom footstep, gunshot or commit on every one-shot that
		// ends over a moving body. 217 of the shipped locomotion and idle labels carry a timeline.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.10f, 2050));
		Timeline.Add(Ev(0.39f, 2051));
		Timeline.Add(Ev(0.875f, 2052));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseFrom(0.62f, 0.62f), Cursor, Fired);
		TestEqual(TEXT("a play met at 0.62 fires nothing behind it"), IdsOf(Fired), FString());
		TestEqual(TEXT("...and the cursor stands where it was met"), Cursor.LastCycle, 0.62f);

		// And it walks forward normally from there — the anchor moves the interval's lower bound, it
		// does not disable the rule.
		ElysiumAnimEvents::Advance(&Timeline, PhaseFrom(0.62f, 0.90f), Cursor, Fired);
		TestEqual(TEXT("...then fires only what it crossed going forward"),
			IdsOf(Fired), FString(TEXT("2052")));
	}

	// --- A one-shot ending over a locomotion clip does not replay that clip's head ------------------
	{
		// The reachable shape of the same defect, in the order it happens: a montage owns the channel,
		// it ends, and the blend stack underneath takes the channel back mid-clip as a play this
		// cursor has never seen. The early record must not fire — the body passed it while the
		// one-shot was posing, not now.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.10f, 2050));
		Timeline.Add(Ev(0.80f, 2051));

		FElysiumAnimEventCursor Cursor;
		// The montage's own play, on a different clip, walking normally.
		FElysiumClipPhase Montage = PhaseAt(0.5f, /*bLooping=*/false, /*PlayId=*/7);
		Montage.Label = TEXT("attack");
		ElysiumAnimEvents::Advance(&Timeline, Montage, Cursor, Fired);
		TestEqual(TEXT("the one-shot walks its own timeline"), IdsOf(Fired), FString(TEXT("2050")));

		// The montage ends; the stack's `walk` re-asserts at 0.62 as a play id 8.
		ElysiumAnimEvents::Advance(&Timeline, PhaseFrom(0.62f, 0.62f, true, 8), Cursor, Fired);
		TestEqual(TEXT("the locomotion arm taking the channel back fires nothing behind its phase"),
			IdsOf(Fired), FString());
		TestEqual(TEXT("...and now names that play"), Cursor.PlayId, 8u);
	}

	// --- A displaced arm that resumes walks the interval it really advanced through -----------------
	{
		// The other half, and it is not the same statement: an arm whose clock kept running while a
		// higher one posed the body has genuinely passed the records in between, and retail's server
		// timeline advances regardless of what is on screen. Its anchor is where the dispatcher last
		// saw it, so the resumption fires those records once rather than skipping or replaying them.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.30f, 2050));
		Timeline.Add(Ev(0.60f, 2051));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.20f, false, 3), Cursor, Fired);
		TestEqual(TEXT("the montage walks up to 0.20"), IdsOf(Fired), FString());

		// A reaction takes the channel for a while: a different clip, a different play.
		FElysiumClipPhase Flinch = PhaseAt(0.5f, false, 4);
		Flinch.Label = TEXT("hit_torso");
		ElysiumAnimEvents::Advance(&Timeline, Flinch, Cursor, Fired);

		// It ends, and the montage — still playing, still play 3 — is at 0.70 with its anchor frozen
		// where the dispatcher left it.
		ElysiumAnimEvents::Advance(&Timeline, PhaseFrom(0.20f, 0.70f, false, 3), Cursor, Fired);
		TestEqual(TEXT("the resuming arm fires the records it passed while displaced, once each"),
			IdsOf(Fired), FString(TEXT("2050,2051")));
		ElysiumAnimEvents::Advance(&Timeline, PhaseFrom(0.20f, 0.90f, false, 3), Cursor, Fired);
		TestEqual(TEXT("...and not again afterwards"), IdsOf(Fired), FString());
	}

	// --- A genuine restart still anchors at zero ----------------------------------------------------
	{
		// The default, and the whole reason the anchor is a field rather than a rule: a clip a play
		// seam STARTED begins at zero, so a record authored on its first frame is still reached. The
		// mid-flight cases above must not have cost that.
		TArray<FElysiumAnimEvent> Timeline;
		Timeline.Add(Ev(0.0f, 2040));
		Timeline.Add(Ev(0.5f, 2041));

		FElysiumAnimEventCursor Cursor;
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.90f, false, 1), Cursor, Fired);
		TestEqual(TEXT("the first play walks its whole timeline from zero"),
			IdsOf(Fired), FString(TEXT("2040,2041")));

		// The same clip re-armed by a seam: a new play, anchored at zero again.
		ElysiumAnimEvents::Advance(&Timeline, PhaseAt(0.05f, false, 2), Cursor, Fired);
		TestEqual(TEXT("a restarted play fires its cycle-0 record again"),
			IdsOf(Fired), FString(TEXT("2040")));
	}

	// --- The recovered server band -----------------------------------------------------------------
	TestEqual(TEXT("the server dispatch ceiling is the recovered 5000"),
		ElysiumAnimEvents::ServerDispatchCeiling, 5000);

	return true;
}

// The chain: the world pass, the handler hop, and the census.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimEventDispatchTest,
	"Elysium.Substrate.AnimEventDispatch", GElysiumTestFlags)
bool FElysiumAnimEventDispatchTest::RunTest(const FString&)
{
	// The census is process-wide by design (a work list across a session, not per-map state), so a
	// suite that reads row counts starts from a known state.
	ElysiumAnimEventCensus::Clear();

	auto Stand = [this](FElysiumRecordingServices& Services, FElysiumEntityWorld& World,
		FElysiumAnimEventProbe*& OutBodied, FElysiumAnimEventProbe*& OutBodiless) -> bool
	{
		World.Load(MakeProbeDefs());
		World.Activate(0.0);

		OutBodied = FindProbe(World, TEXT("bodied"));
		OutBodiless = FindProbe(World, TEXT("bodiless"));
		if (!TestNotNull(TEXT("the bodied probe exists"), OutBodied)
			|| !TestNotNull(TEXT("the bodiless probe exists"), OutBodiless))
		{
			return false;
		}
		if (!TestNotNull(TEXT("the bodied probe carries a body"), OutBodied->Visual))
		{
			return false;
		}
		TestNull(TEXT("the bodiless probe carries none"), OutBodiless->Visual);
		return true;
	};

	// --- The pass reaches only bodied entities, and dispatches in order ---------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumAnimEventProbe* Bodied = nullptr;
		FElysiumAnimEventProbe* Bodiless = nullptr;
		if (!Stand(Services, World, Bodied, Bodiless))
		{
			return false;
		}

		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GOwner, GLabel));
		Timeline.Add(Ev(0.25f, 2050, TEXT("left")));
		Timeline.Add(Ev(0.75f, 2051, TEXT("right")));

		Services.bBodyClipPhaseSet = true;
		Services.BodyClipPhase = PhaseAt(0.5f);

		World.Tick(0.1);
		TestEqual(TEXT("the first frame's interval hands the handler one record"),
			FString::Join(Bodied->Handled, TEXT(",")), FString(TEXT("2050:left")));

		Services.BodyClipPhase.Cycle = 0.9f;
		World.Tick(0.2);
		TestEqual(TEXT("...and the next frame the one after it, in order"),
			FString::Join(Bodied->Handled, TEXT(",")), FString(TEXT("2050:left,2051:right")));

		// The gate. A bodiless entity of the same class shares the timeline fixture and the seam, so
		// the only thing that can be keeping it silent is the body test in the world's own walk.
		TestEqual(TEXT("the pass never reached the bodiless entity"), Bodiless->Handled.Num(), 0);

		// A claimed id is not census work.
		TestEqual(TEXT("nothing a handler claimed reaches the census"),
			ElysiumAnimEventCensus::Num(), 0);
	}

	// --- Above the server band: never offered to a handler, always counted ------------------------
	{
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumAnimEventProbe* Bodied = nullptr;
		FElysiumAnimEventProbe* Bodiless = nullptr;
		if (!Stand(Services, World, Bodied, Bodiless))
		{
			return false;
		}

		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GOwner, GLabel));
		Timeline.Add(Ev(0.25f, 2050));
		// At the ceiling and above it. `DispatchAnimEvents` hands the handler ids strictly BELOW
		// 5000, so 5000 itself is outside — an off-by-one here would route a whole client-side family
		// into a server handler.
		Timeline.Add(Ev(0.30f, 5000, TEXT("attach")));
		Timeline.Add(Ev(0.35f, 5100));

		Services.bBodyClipPhaseSet = true;
		Services.BodyClipPhase = PhaseAt(0.5f);
		World.Tick(0.1);

		TestEqual(TEXT("only the id below the ceiling reached the handler"),
			FString::Join(Bodied->Handled, TEXT(",")), FString(TEXT("2050:")));

		TArray<ElysiumAnimEventCensus::FRow> Rows;
		ElysiumAnimEventCensus::Collect(Rows);
		TestEqual(TEXT("both ids at or above the ceiling landed in the census"), Rows.Num(), 2);
		if (Rows.Num() == 2)
		{
			// Sorted by count then id, and both fired once.
			TestEqual(TEXT("...the one at exactly 5000 among them"), Rows[0].Event, 5000);
			TestEqual(TEXT("...and the one above it"), Rows[1].Event, 5100);
			TestTrue(TEXT("...both marked as never having reached a handler"),
				Rows[0].bAboveServerBand && Rows[1].bAboveServerBand);
			TestEqual(TEXT("...carrying the record's own options"), Rows[0].Options,
				FString(TEXT("attach")));
			TestEqual(TEXT("...and naming the clip they fired from"), Rows[0].Label, FString(GLabel));
			TestEqual(TEXT("...and its owning bank"), Rows[0].OwnerStem, FString(GOwner));
		}
	}

	// --- A handler that refuses lands in the census, counted once per key --------------------------
	{
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumAnimEventProbe* Bodied = nullptr;
		FElysiumAnimEventProbe* Bodiless = nullptr;
		if (!Stand(Services, World, Bodied, Bodiless))
		{
			return false;
		}
		Bodied->Refuse.Add(2051);

		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GOwner, GLabel));
		Timeline.Add(Ev(0.50f, 2051));

		Services.bBodyClipPhaseSet = true;
		Services.BodyClipPhase = PhaseAt(0.9f);
		World.Tick(0.1);

		TestEqual(TEXT("the refused record never counts as handled"), Bodied->Handled.Num(), 0);
		TArray<ElysiumAnimEventCensus::FRow> Rows;
		ElysiumAnimEventCensus::Collect(Rows);
		TestEqual(TEXT("a handler returning false lands in the census"), Rows.Num(), 1);
		if (Rows.Num() == 1)
		{
			TestFalse(TEXT("...reported as refused rather than as out of band"),
				Rows[0].bAboveServerBand);
			TestEqual(TEXT("...counted once so far"), Rows[0].Count, 1);
		}

		// **The once-per-key gate the Verbose line rides on.** Ten more laps of the same clip fire
		// the same id ten more times; the row's count grows and the ROW COUNT does not, which is the
		// same fact the log line is emitted on. A per-occurrence line would be ten lines here and
		// dozens a second on a walking cast.
		for (int32 Lap = 0; Lap < 10; ++Lap)
		{
			Services.BodyClipPhase.Cycle = 0.1f;
			World.Tick(0.2 + Lap * 0.2);
			Services.BodyClipPhase.Cycle = 0.9f;
			World.Tick(0.3 + Lap * 0.2);
		}
		ElysiumAnimEventCensus::Collect(Rows);
		TestEqual(TEXT("eleven fires of one id are still ONE census row"), Rows.Num(), 1);
		if (Rows.Num() == 1)
		{
			TestEqual(TEXT("...whose count carries every occurrence"), Rows[0].Count, 11);
		}
	}

	// --- A body playing nothing is silent ----------------------------------------------------------
	{
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumAnimEventProbe* Bodied = nullptr;
		FElysiumAnimEventProbe* Bodiless = nullptr;
		if (!Stand(Services, World, Bodied, Bodiless))
		{
			return false;
		}

		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GOwner, GLabel));
		Timeline.Add(Ev(0.5f, 2050));

		// The production state this slice: nothing publishes a phase, so the seam answers false on
		// every body and the pass is inert.
		Services.bBodyClipPhaseSet = false;
		World.Tick(0.1);
		World.Tick(0.2);
		TestEqual(TEXT("a body whose pose layer reports no phase dispatches nothing"),
			Bodied->Handled.Num(), 0);
		TestEqual(TEXT("...and writes no census row"), ElysiumAnimEventCensus::Num(), 0);
	}

	ElysiumAnimEventCensus::Clear();
	return true;
}

// The one-shot visibility contract.

// A SIBLING of `AnimEventDispatch`, never a child of it: the automation tree treats a name that is
// also a prefix as a group, and the leaf under it stops being discoverable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOneShotVisibilityTest,
	"Elysium.Substrate.AnimEventVisibility", GElysiumTestFlags)
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


// The carrier, end to end.

// A real baked clip, armed on a real animation host, walked by the real world pass.
//
// Everything above proves a half: `AnimEventWindow` proves the interval rule with no body,
// `AnimEventDispatch` proves the world walk against a scripted phase, and `Elysium.Content.
// GraphMontageSlot` proves the montage poses. What none of them can prove is that the pose layer
// PUBLISHES a phase the pass can walk — the seam between them, which is the whole of this slice. The
// failure it guards is silent by construction: a phase that never arms, or one divided by the
// dynamic montage's own length instead of the clip's, leaves every log clean and every other tier
// green while no sequence event ever fires in the running game.
//
// Nothing here is a fixture except the world's plumbing and one synthetic record: the clip, its
// authored timeline and the cycle a record sits at all come off the export corpus.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSequenceEventCarrierTest,
	"Elysium.Content.SequenceEventCarrier", GElysiumTestFlags)
bool FElysiumSequenceEventCarrierTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	UClass* Graph = LoadClass<UAnimInstance>(nullptr,
		*FElysiumContentPaths::PlayerAnimBlueprintClass());
	if (Graph == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the player animation graph is not generated "
			"(run: uv run elysium export bundle policy)"));
		return true;
	}

	FCarrierPick Pick;
	FString Why;
	if (!FindEventClip(Pick, Why))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: %s"), *Why));
		return true;
	}
	Pick.Mesh->AddToRoot();
	Pick.Clip->AddToRoot();
	ON_SCOPE_EXIT
	{
		Pick.Clip->RemoveFromRoot();
		Pick.Mesh->RemoveFromRoot();
	};
	const float ClipSeconds = Pick.Clip->GetPlayLength();

	// --- the owner join, on the real vocabulary ---------------------------------------------------
	//
	// Two independent code paths compute the owner of one clip, and a weapon's stand-down joins them:
	// the resolver records `FElysiumAnimationSelection::OwnerStem` and the play seam publishes the
	// phase's. They must be one expression, or a swing asks about `bank/attack` while the channel
	// publishes `body/attack` and the commit is silently lost. Asserted on a real (stem, label) whose
	// timeline actually exists, because the case that matters is a BANK-owned clip.
	{
		FElysiumNpcClipSet Vocabulary;
		FString VocabularyError;
		if (TestTrue(TEXT("the discovered body's vocabulary loads"),
			Vocabulary.Load(Pick.Stem, VocabularyError)))
		{
			const FElysiumNpcClip* Row = Vocabulary.Find(Pick.Label);
			if (TestNotNull(TEXT("...and carries the discovered label"), Row))
			{
				// `Visual/ElysiumAnimationResolve.cpp` writes exactly this; so does
				// `UElysiumEntityBodies::PlayNpcClip`.
				const FString ResolverOwner = Row->IsOwnedBy(Pick.Stem) ? Pick.Stem : Row->Owner;
				TestEqual(TEXT("the play seam and the resolver name the same owner"),
					ResolverOwner, Pick.Owner);
				TestTrue(TEXT("...and the dispatcher's own comparison joins them"),
					ResolverOwner.Equals(Pick.Owner, ESearchCase::IgnoreCase));
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("'%s' plays '%s'@'%s' (%.3fs, fade %.2fs); its timeline carries %d record(s), and "
		     "event %d sits at cycle %.4f (%.3fs in)"),
		*Pick.Stem, *Pick.Label, *Pick.Owner, ClipSeconds, Pick.FadeSeconds,
		Pick.Records.Num(), Pick.TargetEvent, Pick.TargetCycle, Pick.TargetCycle * ClipSeconds));

	// --- the body, on the real graph -------------------------------------------------------------
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* UnrealWorld = TestWorld.GetTestWorld();
	AActor* Owner = UnrealWorld ? UnrealWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("body owner spawned"), Owner))
	{
		return false;
	}
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Pick.Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(Graph);
	Owner->SetRootComponent(Comp);
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Comp->GetAnimInstance());
	if (!TestNotNull(TEXT("the generated graph installs the biped host"), Inst))
	{
		return false;
	}
	if (!TestTrue(TEXT("...reporting a compiled graph, so the one-shot takes the slot montage"),
		Inst->HasCompiledGraph()))
	{
		return false;
	}

	// --- the substrate, over that same body ------------------------------------------------------
	//
	// The recording services hand the entity chain the body already standing above, and answer the
	// phase seam off its REAL host rather than off a scripted record. Everything between the montage
	// and the handler is then the shipping path.
	FElysiumRecordingServices Services;
	Services.PrebuiltNpcVisual = Comp;
	Services.bLiveClipPhase = true;
	// The clip's own authored timeline, filed where the seam looks for it, plus one synthetic record
	// at cycle 0. That one is the only fixture in the run, and it asserts a rule the corpus may not
	// happen to author: a new play anchors at zero, so its first frame is the interval `[0, Cycle)`
	// and a record sitting on the very first frame is reached rather than stepped over.
	TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
		FElysiumRecordingServices::EventTimelineKey(Pick.Owner, Pick.Label));
	Timeline.Add(Ev(0.0f, Pick.FirstFrameEvent, TEXT("first-frame")));
	Timeline.Append(Pick.Records);

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeProbeDefs());
	World.Activate(0.0);
	FElysiumAnimEventProbe* Probe = FindProbe(World, TEXT("bodied"));
	if (!TestNotNull(TEXT("the bodied probe exists"), Probe))
	{
		return false;
	}
	if (!TestTrue(TEXT("and it carries the body the graph was stood on"),
		Probe->Visual == Comp))
	{
		return false;
	}

	constexpr float FrameSeconds = 1.f / 30.f;
	double Now = 0.0;
	// One frame of the whole chain: advance the pose layer, evaluate it, then run the world pass that
	// reads it. That is the shipping order — `USkeletalMeshComponent::TickAnimation` runs the montage
	// and `NativeUpdateAnimation` under it, and the entity world's event pass polls afterwards.
	//
	// The evaluation is not decoration. `GetSlotMontageGlobalWeight` is filled by the slot node while
	// the graph is being evaluated, so a run that only ticked would read every slot at zero weight
	// forever — and the "it fired while still blending" assertion below would pass vacuously. A null
	// tick function keeps the evaluation on this thread.
	const auto Step = [Comp, &World, &Now, FrameSeconds]()
	{
		Comp->TickAnimation(FrameSeconds, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
		Now += static_cast<double>(FrameSeconds);
		World.Tick(Now);
	};
	const auto CycleNow = [Inst]()
	{
		FElysiumClipPhase Phase;
		return Inst->GetClipPhase(EElysiumAnimChannel::Base, Phase) ? Phase.Cycle : -1.f;
	};

	// A resolved locomotion selection first, which is the state every body in the running game is in
	// before anything arms a clip on it. It is what gives the montage something to blend FROM — a
	// one-shot over a stack holding no asset snaps in, and the still-blending half below would then
	// be unobservable. Its label is deliberately one no timeline is filed under: the locomotion arm
	// publishes that label whenever the montage is not playing, and a base standing on the SAME label
	// would fire the clip's records a second time from underneath.
	FElysiumAnimationSelection Standing;
	Standing.Generation = 1;
	Standing.GraphState = EElysiumGraphState::Idle;
	Standing.SequenceLabel = TEXT("__carrier_base__");
	Standing.OwnerStem = Pick.Owner;
	Standing.AnimationName = TEXT("__carrier_base__");
	Standing.AssetKind = EElysiumAnimAssetKind::Sequence;
	Standing.Outcome = EElysiumAnimOutcome::Resolved;
	FElysiumResolvedAnimation Assets;
	Assets.Sequence = Pick.Clip;
	Inst->PublishSelection(Standing, Assets);
	Step();
	Step();
	TestEqual(TEXT("a base standing on a clip with no timeline dispatches nothing"),
		Probe->Handled.Num(), 0);

	// --- the arm ---------------------------------------------------------------------------------
	//
	// The blend IN is long enough to still be running when the tracked record fires, and it is
	// **stated by the case rather than taken off the clip**. Every shipped sequence that authors an
	// early event is an attack or a knockback, and all of them set `flags & 0x2` — the authored hard
	// cut — so the corpus's own fade is zero and there is no blend to observe. The seam takes the two
	// fades as arguments precisely because the caller supplies them, and what is under test here is
	// that a record fires while the slot is still fading up, not what a particular clip authored.
	const float TargetSeconds = Pick.TargetCycle * ClipSeconds;
	const float BlendInSeconds = FMath::Max(Pick.FadeSeconds, TargetSeconds + 2.f * FrameSeconds);
	if (!TestTrue(TEXT("the clip is accepted by the slot"),
		Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip,
			/*bLoop=*/false, BlendInSeconds, Pick.FadeSeconds)))
	{
		return false;
	}
	// **Armed synchronously, not on the next tick.** The weapon transactions ask this question in the
	// same statement pair that played the clip (`Substrate/ElysiumWeaponClasses.cpp`), so a phase
	// that only appeared on the following update would name the clip it replaced.
	FElysiumClipPhase Armed;
	if (TestTrue(TEXT("and the base channel publishes its phase at the instant it was armed"),
		Inst->GetClipPhase(EElysiumAnimChannel::Base, Armed)))
	{
		TestEqual(TEXT("...naming the label the caller resolved it through"), Armed.Label,
			Pick.Label);
		TestEqual(TEXT("...and the bank that owns it"), Armed.OwnerStem, Pick.Owner);
		TestEqual(TEXT("...standing at cycle 0"), Armed.Cycle, 0.f);
		// The whole point of arming from the clip rather than the montage: a dynamic montage built
		// for a looping hold is a million segments long, and a cycle divided by that never leaves 0.
		TestTrue(TEXT("...and carrying the CLIP's own length, not the montage's"),
			FMath::IsNearlyEqual(Armed.Length, ClipSeconds, 0.001f));
	}

	// --- the walk ---------------------------------------------------------------------------------
	//
	// Ticked to just past the target record and no further. A non-looping montage that ENDS hands the
	// base back to the blend stack, which is a new play of a different clip — correct behaviour,
	// and not what this half is measuring.
	const int32 TargetFrame = FMath::CeilToInt(TargetSeconds / FrameSeconds) + 1;
	const int32 Frames = FMath::Min(TargetFrame + 2, FMath::FloorToInt(ClipSeconds / FrameSeconds));
	if (!TestTrue(TEXT("the walk reaches the tracked record before the montage's own end"),
		Frames >= TargetFrame))
	{
		return false;
	}
	int32 FiredOnFrame = INDEX_NONE;
	float CycleBefore = 0.f;
	float CycleAtFire = 0.f;
	float SlotWeightAtFire = -1.f;
	int32 FirstFrameFires = 0;
	for (int32 Frame = 1; Frame <= Frames; ++Frame)
	{
		const int32 Before = CountFires(Probe->Handled, Pick.TargetEvent);
		const float Prev = CycleNow();
		Step();
		if (Frame == 1)
		{
			FirstFrameFires = CountFires(Probe->Handled, Pick.FirstFrameEvent);
		}
		if (FiredOnFrame == INDEX_NONE && CountFires(Probe->Handled, Pick.TargetEvent) > Before)
		{
			FiredOnFrame = Frame;
			CycleBefore = Prev;
			CycleAtFire = CycleNow();
			SlotWeightAtFire = Inst->GetSlotMontageGlobalWeight(FAnimSlotGroup::DefaultSlotName);
		}
	}

	// A record authored at cycle 0 reaches the handler on the FIRST advance of the play. Nothing else
	// in the chain can produce that: the cursor has to anchor a new play at zero rather than at
	// wherever the previous clip stood.
	TestEqual(TEXT("a record at cycle 0 fires on the first advance of the play"),
		FirstFrameFires, 1);

	if (!TestTrue(TEXT("the clip's own authored record reached the handler"),
		FiredOnFrame != INDEX_NONE))
	{
		AddError(FString::Printf(
			TEXT("event %d at cycle %.4f never fired across %d frames of a %.3fs clip; the pose "
			     "layer stands at cycle %.4f"),
			Pick.TargetEvent, Pick.TargetCycle, Frames, ClipSeconds, CycleNow()));
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("event %d fired on frame %d, in the interval [%.4f, %.4f) — slot weight %.3f"),
		Pick.TargetEvent, FiredOnFrame, CycleBefore, CycleAtFire, SlotWeightAtFire));
	TestEqual(TEXT("...exactly once"), CountFires(Probe->Handled, Pick.TargetEvent), 1);
	// The recovered comparison, read off the frame that actually fired: `last <= event.cycle <
	// current` (`docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch").
	TestTrue(TEXT("...on the frame whose half-open interval contains its authored cycle"),
		CycleBefore <= Pick.TargetCycle && Pick.TargetCycle < CycleAtFire);

	// **No weight gate, and that is the design.** Events fire from cycle 0 while the slot is still
	// blending in, because a threshold here would rebuild the exact defect this carrier exists to
	// avoid — a `UAnimNotify` is evaluated on a weighted pose and drops on a body that is fading.
	if (SlotWeightAtFire >= 1.f)
	{
		AddWarning(FString::Printf(
			TEXT("event %d fires at cycle %.4f, %.3fs into a %.3fs clip, and the slot was already "
			     "at full weight by then despite a %.3fs blend — this pair cannot show the "
			     "still-blending half"),
			Pick.TargetEvent, Pick.TargetCycle, TargetSeconds, ClipSeconds, BlendInSeconds));
	}
	else
	{
		TestTrue(TEXT("and it fired while the slot montage was still blending in"),
			SlotWeightAtFire < 1.f);
	}

	// --- the re-arm -------------------------------------------------------------------------------
	//
	// The same clip played again is a NEW play, and its timeline runs from the top. Nothing else on
	// the record can say so: the owner, the label and the phase are identical, and a phase that went
	// backwards would otherwise read as a seek. `PlayId` is the whole discriminator, and it is what
	// makes a repeated attack commit a second time.
	if (!TestTrue(TEXT("the same clip re-arms"),
		Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip,
			/*bLoop=*/false, BlendInSeconds, Pick.FadeSeconds)))
	{
		return false;
	}
	for (int32 Frame = 1; Frame <= Frames; ++Frame)
	{
		Step();
	}
	TestEqual(TEXT("...and its timeline fires again from zero"),
		CountFires(Probe->Handled, Pick.TargetEvent), 2);
	TestEqual(TEXT("...the cycle-0 record with it"),
		CountFires(Probe->Handled, Pick.FirstFrameEvent), 2);

	// --- a reaction over the montage, and the montage taking its channel back ----------------------
	//
	// The four producers run CONCURRENTLY. A reaction replaces the locomotion pose without stopping
	// the montage under it, so a single shared phase record lets the newer arm erase a clip that is
	// still playing — and the displaced one can never take the channel back. In the running game that
	// is a swing whose commit id never fires because a flinch landed on the same body: the phase is
	// armed, read true by the weapon, and gone on the next update.
	{
		if (!Inst->HasCompiledReactionBranch())
		{
			AddWarning(TEXT("this generated graph carries no reaction branch, so the arm-precedence "
				"half cannot run against it"));
		}
		else
		{
			// Re-arm the montage so its clock is near the top of the clip and its later record is
			// still ahead of it.
			Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip,
				/*bLoop=*/false, BlendInSeconds, Pick.FadeSeconds);
			Step();
			FElysiumClipPhase BeforeHit;
			Inst->GetClipPhase(EElysiumAnimChannel::Base, BeforeHit);
			const uint32 MontagePlay = BeforeHit.PlayId;
			const float MontageCycle = BeforeHit.Cycle;
			TestEqual(TEXT("the montage owns the channel before the hit"), BeforeHit.Label,
				Pick.Label);

			// A flinch, on the same body, naming its own clip. It outranks the montage: it is what
			// poses the body and it holds the base-channel claim.
			FElysiumReactionPlay Flinch;
			Flinch.Sequence = Pick.Clip;
			Flinch.LengthSeconds = ClipSeconds;
			Flinch.OwnerStem = Pick.Owner;
			Flinch.Label = TEXT("__carrier_flinch__");
			if (TestTrue(TEXT("the reaction plays"), Inst->PlayReaction(Flinch)))
			{
				FElysiumClipPhase Reacting;
				if (TestTrue(TEXT("and it takes the base channel"),
					Inst->GetClipPhase(EElysiumAnimChannel::Base, Reacting)))
				{
					TestEqual(TEXT("...naming the reaction's own clip"), Reacting.Label,
						FString(TEXT("__carrier_flinch__")));
					TestNotEqual(TEXT("...as a play of its own"), Reacting.PlayId, MontagePlay);
				}
				Step();

				// The flinch ends. The montage never stopped, so it takes the channel back — as the
				// SAME play, with its clock where it actually got to.
				Inst->StopReaction();
				Step();
				FElysiumClipPhase Resumed;
				if (TestTrue(TEXT("the montage takes the channel back when the reaction ends"),
					Inst->GetClipPhase(EElysiumAnimChannel::Base, Resumed)))
				{
					TestEqual(TEXT("...naming the clip that never stopped"), Resumed.Label,
						Pick.Label);
					TestEqual(TEXT("...as the SAME play, so its timeline does not restart"),
						Resumed.PlayId, MontagePlay);
					// Its clock ran the whole time the flinch was posing, which is retail's own
					// behaviour: the server timeline advances regardless of what is on screen.
					TestTrue(TEXT("...with its own clock advanced, not rewound"),
						Resumed.Cycle > MontageCycle);
					// And the dispatcher resumes from where it last saw this arm rather than from
					// zero, so the interval it advanced through while displaced is walked once.
					TestTrue(TEXT("...resuming from where the dispatcher left it"),
						Resumed.AnchorCycle >= MontageCycle && Resumed.AnchorCycle <= Resumed.Cycle);
				}
			}
			Inst->StopOneShot(0.f);
			Inst->StopReaction();
		}
	}

	// --- the body with no compiled graph -----------------------------------------------------------
	//
	// `PlayOneShot` routes that body to the proxy's clip player instead of a slot montage, and it is
	// a real case rather than a degenerate one: a generated graph package missing from the mount
	// leaves every body on the plain native class, and skeletal props are installed on it
	// deliberately. Which host is posing a clip is not a property of the clip, so its timeline has to
	// be walkable either way — and the two routes arm through different code, so one can regress
	// while the other stays green.
	{
		AActor* NativeOwner = UnrealWorld->SpawnActor<AActor>();
		USkeletalMeshComponent* NativeComp = NewObject<USkeletalMeshComponent>(NativeOwner);
		NativeComp->SetMobility(EComponentMobility::Movable);
		NativeComp->SetSkeletalMeshAsset(Pick.Mesh);
		NativeComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		NativeComp->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
		NativeOwner->SetRootComponent(NativeComp);
		NativeComp->RegisterComponent();
		NativeComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		UElysiumBipedAnimInstance* Native =
			Cast<UElysiumBipedAnimInstance>(NativeComp->GetAnimInstance());
		if (TestNotNull(TEXT("the plain native host installs"), Native)
			&& TestFalse(TEXT("...and reports no compiled graph, so the clip player answers"),
				Native->HasCompiledGraph()))
		{
			TestTrue(TEXT("the clip-player fallback accepts the clip"),
				Native->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip,
					/*bLoop=*/false, 0.f, 0.f));
			FElysiumClipPhase NativePhase;
			if (TestTrue(TEXT("...and publishes the base channel's phase like any other body"),
				Native->GetClipPhase(EElysiumAnimChannel::Base, NativePhase)))
			{
				TestEqual(TEXT("...naming the same clip"), NativePhase.Label, Pick.Label);
				TestEqual(TEXT("...standing at cycle 0"), NativePhase.Cycle, 0.f);
			}
			// TWO frames, and the second one is the point: the clip player is a graph-side node that
			// advances on the worker, so `NativeUpdateAnimation` reads the position last frame's
			// update settled. The first tick therefore still reports cycle 0 — that is a frame of
			// lag on a monotonic clock, not a stalled phase, and the interval rule still fires each
			// record exactly once.
			NativeComp->TickAnimation(FrameSeconds, /*bNeedsValidRootMotion=*/false);
			NativeComp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
			NativeComp->TickAnimation(FrameSeconds, /*bNeedsValidRootMotion=*/false);
			NativeComp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
			FElysiumClipPhase Advanced;
			if (TestTrue(TEXT("...and advances it off the clip player's own position"),
				Native->GetClipPhase(EElysiumAnimChannel::Base, Advanced)))
			{
				TestTrue(TEXT("...to a cycle inside the clip"),
					Advanced.Cycle > 0.f && Advanced.Cycle < 1.f);
			}

			// A SECOND clip over the first, which is what a prop switching clips and a scene chaining
			// beats both do. `PlayDirect` swaps the sequence on the game thread and leaves the node's
			// time accumulator alone until the worker restarts it, so the position this update reads
			// still belongs to the clip that was replaced. Dividing it by the new clip's length would
			// publish a phase for a play nobody made — and a freshly armed clip anchors at zero, so
			// the cursor would fire every record below that phantom cycle in one burst.
			Native->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip,
				/*bLoop=*/false, 0.f, 0.f);
			NativeComp->TickAnimation(FrameSeconds, /*bNeedsValidRootMotion=*/false);
			NativeComp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
			FElysiumClipPhase Restarted;
			if (TestTrue(TEXT("a clip that replaced a running one still publishes"),
				Native->GetClipPhase(EElysiumAnimChannel::Base, Restarted)))
			{
				TestEqual(TEXT("...at its own anchor, not at the replaced clip's position"),
					Restarted.Cycle, 0.f);
			}
		}
	}

	return true;
}

}   // namespace ElysiumAnimEventTests

#endif // WITH_DEV_AUTOMATION_TESTS
