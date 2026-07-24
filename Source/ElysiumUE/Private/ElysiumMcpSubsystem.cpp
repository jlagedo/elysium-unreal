#include "ElysiumMcpSubsystem.h"

#include "ElysiumLogTap.h"
#include "ElysiumMcpTools.h"

#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if ELYSIUM_WITH_MCP
#include "IModelContextProtocolModule.h"
#include "ModelContextProtocolServer.h"
#include "ModelContextProtocolSettings.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMcp, Log, All);

namespace
{
	// `-NoElysiumMcp` fully suppresses the auto-start (the one opt-out). The server is otherwise on
	// by default in any build that carries the plugin (editor/dev only — see ELYSIUM_WITH_MCP).
	bool ElysiumMcpDisabledOnCommandLine()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("NoElysiumMcp"));
	}

	// `-ElysiumMcp=8123` pins a port; a bare `-ElysiumMcp` or its absence leaves OutPort 0, meaning
	// "the plugin's configured default". Returns true only when an explicit port was given.
	bool ElysiumMcpPortOverride(int32& OutPort)
	{
		OutPort = 0;
		FString Value;
		if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumMcp="), Value))
		{
			OutPort = FCString::Atoi(*Value);
			return true;
		}
		return false;
	}
}

void UElysiumMcpSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The tap runs whether or not MCP is ever started: it costs one ring-buffer write per log line
	// and it is the only way `elysium_log_tail` / `elysium_console_exec` can report what happened
	// before the agent connected.
	ElysiumLogTapGet().Install();

	Tools = MakePimpl<FElysiumMcpTools>();
	const int32 Registered = Tools->Register();

	RegisterConsoleCommands();

	// On by default in any build that carries the plugin — which is editor/dev only (ELYSIUM_WITH_MCP
	// is defined 1 only for the Editor target), so this never self-starts a server in Shipping/Test.
	// `-NoElysiumMcp` is the opt-out; `-ElysiumMcp=<port>` pins a port.
#if ELYSIUM_WITH_MCP
	if (ElysiumMcpDisabledOnCommandLine())
	{
		UE_LOG(LogElysiumMcp, Log,
			TEXT("Elysium MCP auto-start suppressed (-NoElysiumMcp); %d tools registered, use `elysium.mcp.start` to run."),
			Registered);
	}
	else
	{
		int32 Port = 0;
		ElysiumMcpPortOverride(Port);
		StartServer(Port);
	}
#else
	(void)Registered;
#endif
}

void UElysiumMcpSubsystem::Deinitialize()
{
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();

	if (Tools)
	{
		Tools->Unregister();
	}
	Tools.Reset();

	ElysiumLogTapGet().Remove();

	Super::Deinitialize();
}

int32 UElysiumMcpSubsystem::NumTools() const
{
	return Tools ? Tools->Num() : 0;
}

void UElysiumMcpSubsystem::DescribeTools(TArray<FString>& Out) const
{
	if (Tools)
	{
		Tools->Describe(Out);
	}
}

bool UElysiumMcpSubsystem::IsServerRunning() const
{
#if ELYSIUM_WITH_MCP
	IModelContextProtocolModule* Module = IModelContextProtocolModule::Get();
	return Module && Module->GetServer() != nullptr;
#else
	return false;
#endif
}

bool UElysiumMcpSubsystem::StartServer(int32 Port)
{
#if ELYSIUM_WITH_MCP
	IModelContextProtocolModule* Module = IModelContextProtocolModule::Get();
	if (!Module)
	{
		UE_LOG(LogElysiumMcp, Warning, TEXT("ModelContextProtocol module unavailable; cannot start the MCP server."));
		return false;
	}
	if (Module->GetServer() != nullptr)
	{
		UE_LOG(LogElysiumMcp, Log, TEXT("MCP server already running."));
		return false;
	}

	// Re-register if `ModelContextProtocol.RefreshTools` (or an earlier StopServer) emptied the
	// collection, so `elysium.mcp.start` always brings up a server that actually serves our tools.
	if (Tools)
	{
		Tools->Register();
	}

	const uint32 EffectivePort = (Port > 0)
		? static_cast<uint32>(Port)
		: UE::ModelContextProtocol::GetServerPortNumber();
	Module->StartServer(EffectivePort, UE::ModelContextProtocol::GetServerUrlPath());
	LastPort = static_cast<int32>(EffectivePort);

	UE_LOG(LogElysiumMcp, Display,
		TEXT("MCP server listening on http://127.0.0.1:%d%s — %d Elysium tools. Loopback only, no auth."),
		LastPort, *UE::ModelContextProtocol::GetServerUrlPath(), NumTools());
	return true;
#else
	UE_LOG(LogElysiumMcp, Warning,
		TEXT("Built without the ModelContextProtocol plugin (non-Editor target); MCP is unavailable."));
	return false;
#endif
}

void UElysiumMcpSubsystem::StopServer()
{
#if ELYSIUM_WITH_MCP
	if (IModelContextProtocolModule* Module = IModelContextProtocolModule::Get())
	{
		Module->StopServer();
		UE_LOG(LogElysiumMcp, Display, TEXT("MCP server stopped."));
	}
#endif
}

void UElysiumMcpSubsystem::RegisterConsoleCommands()
{
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.mcp.start"),
		TEXT("Start the Elysium MCP server (loopback only). Optional: elysium.mcp.start <port>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			StartServer(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		}),
		ECVF_Default));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.mcp.stop"),
		TEXT("Stop the Elysium MCP server."),
		FConsoleCommandDelegate::CreateLambda([this]() { StopServer(); }),
		ECVF_Default));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.mcp.status"),
		TEXT("Report whether the Elysium MCP server is running, on which port, with how many tools."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			if (IsServerRunning())
			{
				UE_LOG(LogElysiumMcp, Display, TEXT("running — http://127.0.0.1:%d, %d tools"),
					LastPort, NumTools());
			}
			else
			{
				UE_LOG(LogElysiumMcp, Display, TEXT("stopped — %d tools registered"), NumTools());
			}
		}),
		ECVF_Default));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.mcp.tools"),
		TEXT("List the registered Elysium MCP tools."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			TArray<FString> Lines;
			DescribeTools(Lines);
			UE_LOG(LogElysiumMcp, Display, TEXT("%d Elysium MCP tools:"), Lines.Num());
			for (const FString& Line : Lines)
			{
				UE_LOG(LogElysiumMcp, Display, TEXT("  %s"), *Line);
			}
		}),
		ECVF_Default));
}
