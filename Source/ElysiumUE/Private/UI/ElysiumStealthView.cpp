#include "ElysiumViewState.h"
#include "ElysiumPlayer.h"

FElysiumStealthView FElysiumStealthView::FromPlayer(const FElysiumPlayer* Player, bool bDucking)
{
	FElysiumStealthView View;
	View.bSneaking = bDucking;
	if (Player)
	{
		View.bConcealmentValid = Player->Stealth.bHasLightSample;
		View.LightRow = Player->Stealth.LightRow;
		View.bObserverValid = Player->Observer.IsSet();
		View.ObserverDistanceCm = Player->Observer.DistanceCm;
		View.Detection = !View.bObserverValid ? EElysiumDetection::Unaware
			: (Player->Observer.bDetected ? EElysiumDetection::Detected : EElysiumDetection::Searching);
	}
	return View;
}
