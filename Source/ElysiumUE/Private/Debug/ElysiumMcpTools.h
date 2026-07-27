#pragma once

#include "CoreMinimal.h"

struct IModelContextProtocolTool;

// P2.7 — the `elysium_*` MCP tool collection, owned by UElysiumMcpSubsystem.
//
// Split from the subsystem so the UCLASS header stays free of the engine's experimental MCP
// includes, and so the whole tool surface compiles away to an empty shell (below, under
// ELYSIUM_WITH_MCP 0) for a target that has no MCP plugin.
//
// Registration is process-lifetime: tools are added to the module's collection once, resolve the
// live world at call time, and re-add themselves when `ModelContextProtocol.RefreshTools` clears
// the collection.
class FElysiumMcpTools
{
public:
	FElysiumMcpTools();
	~FElysiumMcpTools();

	FElysiumMcpTools(const FElysiumMcpTools&) = delete;
	FElysiumMcpTools& operator=(const FElysiumMcpTools&) = delete;

	// Build every tool and add it to the MCP module's collection. Safe to call twice (a second
	// call is a no-op); no-op without the plugin. Returns the number of tools now registered.
	int32 Register();
	// Remove them again (subsystem teardown).
	void Unregister();

	int32 Num() const;
	// `name — description` per registered tool, in registration order.
	void Describe(TArray<FString>& Out) const;

private:
	// Complete only inside the .cpp, so this header needs no MCP include.
	TArray<TSharedRef<IModelContextProtocolTool>> Registered;
	FDelegateHandle RefreshHandle;
};
