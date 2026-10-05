#include "Substrate/ElysiumNpcPedestrian.h"

void FElysiumNpcPedestrian::CreateCorpse(const FVector& Force, void* InInfo)
{
	PedestrianCreateCorpse(Force, InInfo); // 0x103a38c0 slot301
}
