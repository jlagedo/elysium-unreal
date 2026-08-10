#pragma once

// The registration form for a recovered datamap input with no system behind it yet.
//
// A pending input resolves through the registry's chain walk and names itself through the same
// stub surface an unregistered classname's wires reach (`FElysiumCombatCharacter::PendingInput` ->
// `ElysiumStub::Fired`), so the gap is a reported work-list row rather than an unknown-input
// diagnostic. It performs nothing: the recovered inventory in `docs/vtmb/script_api.md` carries
// the name, the argument type and the retail handler address, but not the semantics.
//
// An input thunk is a captureless function pointer (`FElysiumInputThunk`), so the input's name and
// its owning task have to be baked into the lambda's body — hence a macro rather than a table. `D`
// is the `FElysiumClassDesc&` the surrounding registration callback holds.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumPlayer.h"

// The common case: an input CBaseCombatCharacter declares.
#define ELYSIUM_PENDING_INPUT(Class, Name, OwnerText)                                  \
	D.Input(TEXT(#Name), [](FElysiumEntity& E, const FElysiumInputArgs& A)             \
		{ static_cast<Class&>(E).PendingInput(TEXT(#Name), TEXT(OwnerText), A); })

// The same, for an input a different level of the character chain declares. The reported key names
// that level rather than the receiving classname, so one work-list row covers every classname that
// inherits the input.
#define ELYSIUM_PENDING_INPUT_ON(DeclaringClass, Class, Name, OwnerText)               \
	D.Input(TEXT(#Name), [](FElysiumEntity& E, const FElysiumInputArgs& A)             \
		{ static_cast<Class&>(E).PendingInput(TEXT(#Name), TEXT(OwnerText), A,         \
			TEXT(DeclaringClass)); })
