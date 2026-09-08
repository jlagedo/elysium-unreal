#pragma once

#include "ElysiumAnimationIntent.h"

struct FElysiumStealthPairClips
{
	int32 Position = INDEX_NONE;
	FElysiumActivityClip Attacker;
	FElysiumActivityClip Victim;
	FString AttackerActivity;
	FString VictimActivity;
};
