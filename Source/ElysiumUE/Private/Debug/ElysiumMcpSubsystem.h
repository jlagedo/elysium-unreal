#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Templates/PimplPtr.h"
#include "ElysiumMcpSubsystem.generated.h"

class FElysiumMcpTools;
class IConsoleObject;

// The agent-facing surface (`docs/architecture/debug-tooling.md` Layer 3): the `elysium_*` MCP tools and the
// server that serves them. Every tool is a thin structured wrapper over the same runtime state the
// Cog windows render and the `elysium.*` verbs flip; nothing here is a second dispatch mechanism,
// and entity injection still goes through `FElysiumEntityWorld::EnqueueInput` like everything else.
//
// Engine-scoped, not game-instance-scoped, deliberately: tools register once for the process and
// resolve the live world at CALL time, so a connected agent keeps one stable tool list across map
// travel, PIE start/stop, and an idle editor (a tool called with no game running reports that
// rather than vanishing from the list).
//
// The server is ON BY DEFAULT in any build that carries the plugin (editor/dev only — the dep is
// gated by ELYSIUM_WITH_MCP, defined 1 only for the Editor target, so it never self-starts in
// Shipping/Test). Commandlets and unattended automation do not auto-start the HTTP listener; `-NoElysiumMcp` opts out in an
// interactive process; `-ElysiumMcp=<port>` pins a port; `elysium.mcp.start`/`stop` toggle it live.
// It binds loopback-only with no authentication (the engine plugin's posture), so it is a local
// dev tool and nothing else.
UCLASS()
class UElysiumMcpSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Start the MCP HTTP server on Port (0 = the plugin's configured default). Returns false when
	// the engine's ModelContextProtocol plugin is unavailable (a non-Editor target) or already up.
	bool StartServer(int32 Port);
	void StopServer();
	bool IsServerRunning() const;

	// Tools registered by this subsystem (0 when built without the plugin).
	int32 NumTools() const;
	// `name — description` for each registered tool, for `elysium.mcp.tools`.
	void DescribeTools(TArray<FString>& Out) const;

	// The port the server was last started on (0 = never started / the plugin default).
	int32 ServerPort() const { return LastPort; }

private:
	void RegisterConsoleCommands();

	// The tool collection. Type-erased so this header needs no MCP plugin include, and so the whole
	// implementation compiles away to an empty shell when ELYSIUM_WITH_MCP is 0.
	TPimplPtr<FElysiumMcpTools> Tools;

	TArray<IConsoleObject*> ConsoleObjects;
	int32 LastPort = 0;
};
