#include "ElysiumScriptHost.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScript, Log, All);

FElysiumVariant FElysiumNullScriptHost::Eval(const FString& Source, const FElysiumScriptContext& /*Ctx*/)
{
	// No evaluator yet (M4/B6). Make the call observable and return Void so the caller's
	// truthiness test reads false (error-to-false, RE3), exactly as a failed retail eval would.
	UE_LOG(LogElysiumScript, Verbose, TEXT("[null] eval __main__.%s"), *Source);
	return FElysiumVariant::Void();
}
