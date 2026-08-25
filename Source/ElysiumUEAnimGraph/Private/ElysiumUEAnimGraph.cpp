#include "Modules/ModuleManager.h"

// The module exists to REGISTER a class, not to run anything: loading it is what puts
// `UAnimGraphNode_ElysiumPostAdditive` in the object hierarchy, so the generator's
// `FindObject<UClass>` and the T3D importer's class resolution can both reach it by path. A
// module with no implementation still needs this: without it the loader reports "could not be
// successfully initialized" and the process exits before any graph is built.
IMPLEMENT_GAME_MODULE(FDefaultModuleImpl, ElysiumUEAnimGraph);
