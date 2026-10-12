#include "ElysiumRetailConVars.h"
#include "HAL/IConsoleManager.h"

bool ElysiumRetailConVars::DeveloperDiagnosticsEnabled()
{
	IConsoleManager& Manager = IConsoleManager::Get();
	IConsoleObject* Object = Manager.FindConsoleObject(TEXT("developer"));
	if (!Object)
	{
		Object = Manager.RegisterConsoleVariable(TEXT("developer"), 0,
			TEXT("Retail developer diagnostics level."), ECVF_Default);
	}
	// Preserve the object predicate before the value read: a command is not a ConVar.
	const IConsoleVariable* Variable = Object ? Object->AsVariable() : nullptr;
	return Variable && Variable->GetInt() > 0;
}
