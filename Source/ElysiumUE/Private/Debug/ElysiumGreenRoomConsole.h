#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Containers/Ticker.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"

class FElysiumArenaScenarioRunner;
class FElysiumEntityWorld;
class FElysiumGreenRoomRun;
class IConsoleObject;
class UElysiumMapSubsystem;
class UWorld;

/**
 * The green room's `elysium.gr_*` verb set — the whole lab reachable from a console line.
 *
 * The Cog window is the control surface a human uses. This is the same lab driven from the console,
 * which is what the MCP bridge speaks: it cannot click ImGui, so without these verbs the lab is
 * reachable by hand only, and anything it proves has to be proven by a person sitting at the
 * machine. Every verb here calls exactly the method the window's own widget calls, so there is no
 * second path into the lab and nothing the console can do that the window cannot.
 *
 * Non-Shipping, like the lab itself. Owned by `UElysiumMapSubsystem`, which owns the lab.
 */
class FElysiumGreenRoomConsole
{
public:
	explicit FElysiumGreenRoomConsole(UElysiumMapSubsystem* InOwner);
	~FElysiumGreenRoomConsole();

	FElysiumGreenRoomConsole(const FElysiumGreenRoomConsole&) = delete;
	FElysiumGreenRoomConsole& operator=(const FElysiumGreenRoomConsole&) = delete;

private:
	// The armed lab, or null with a reason already logged. Every verb starts here, so "no green room
	// is standing" is one message in one place rather than one per verb.
	FElysiumGreenRoomRun* Lab(const TCHAR* Verb) const;

	// The stage world's entity world and engine world, or null with a reason logged. The arena
	// scenario verbs (`gr_scenario`, `gr_hints`, `gr_los`) are console-only staging — they compose the
	// builder's, the cast's and the lab's own doors and add no path the game does not already use.
	FElysiumEntityWorld* EntityWorldFor(const TCHAR* Verb) const;
	UWorld* World() const;

	void Register(const TCHAR* Name, const TCHAR* Help,
		TFunction<void(FElysiumGreenRoomRun&, const TArray<FString>&)> Body);

	// `elysium.gr_scenario <name>`'s run, ticked off the core ticker until it ends, then its trace
	// written and its verdict logged. One at a time: a second call stops the first.
	bool TickLabScenario(float DeltaSeconds);
	void StopLabScenario(const TCHAR* Reason);
	void FinishLabScenario();

	TWeakObjectPtr<UElysiumMapSubsystem> Owner;
	TArray<IConsoleObject*> Commands;
	TUniquePtr<FElysiumArenaScenarioRunner> LabScenario;
	FTSTicker::FDelegateHandle LabScenarioTicker;
};

#endif // !UE_BUILD_SHIPPING
