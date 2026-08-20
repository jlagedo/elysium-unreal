// Content-free Substrate automation: VtMB's sequence-event dispatcher (LIFE5 slice 1) — the pure
// interval rule, and the entity chain that walks it.
//
// Every rule asserted here is a fact from `docs/vtmb/animation_and_movers.md` → "Sequence events and
// native dispatch": the recovered comparison `last_cycle <= event.cycle < current_cycle`, the single
// wrapped visit a looping sequence makes after passing 1.0, file order for records sharing a cycle,
// and the 5000 server dispatch ceiling. Nothing loads a model, a bake or an export — a timeline is a
// fixture on the stack, and the phase comes from the recording double.
//
// **Nothing is routed yet.** No entity claims an id this slice, so every fired record lands in the
// census. That is the point being asserted: the walk exists, it fires the right records in the right
// order, and each unclaimed id is counted once instead of warned about per occurrence.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Tests/ElysiumTestServices.h"

#include "Components/SkeletalMeshComponent.h"

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
}

// =====================================================================================
// The pure rule: which records an interval contains
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimEventWindowTest,
	"Elysium.Substrate.AnimEventWindow", GElysiumTestFlags)
bool FElysiumAnimEventWindowTest::RunTest(const FString&)
{
	TArray<const FElysiumAnimEvent*> Fired;

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

	// --- The recovered server band -----------------------------------------------------------------
	TestEqual(TEXT("the server dispatch ceiling is the recovered 5000"),
		ElysiumAnimEvents::ServerDispatchCeiling, 5000);

	return true;
}

// =====================================================================================
// The chain: the world pass, the handler hop, and the census
// =====================================================================================

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

// =====================================================================================
// The one-shot visibility contract (LIFE5 rider)
// =====================================================================================

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

}   // namespace ElysiumAnimEventTests

#endif // WITH_DEV_AUTOMATION_TESTS
