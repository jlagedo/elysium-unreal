#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VBrujah` (primary vtable `0x104ab6cc`), built by `npc_VBrujah` factory `0x103677c0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcBrujah : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VBrujah", FElysiumNpcVampire)

};
