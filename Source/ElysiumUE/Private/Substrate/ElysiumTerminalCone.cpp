#include "Substrate/ElysiumTerminalCone.h"

float ElysiumTerminalCone::FacingDot(const FVector& ScreenCm, const FVector& ScreenAxisCm,
	const FVector& EyeCm)
{
	// `FUN_10218710` 10218742-10218782: the delta is taken in 3D and normalized in 3D
	// (`VectorNormalize` through `PTR_thunk_FUN_10137220_1057966c`). A zero-length axis leaves the
	// vector at zero, which the dot below turns into 0.
	FVector Forward = ScreenAxisCm - ScreenCm;
	if (!Forward.Normalize())
	{
		return 0.0f;
	}

	// `FUN_101d1120`: the eye-to-screen delta is flattened FIRST and normalized in 2D; the forward
	// vector is not re-normalized after the projection.
	const double DeltaX = EyeCm.X - ScreenCm.X;
	const double DeltaY = EyeCm.Y - ScreenCm.Y;
	const double Length = FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
	if (Length == 0.0)
	{
		return 0.0f;
	}
	const double Scale = 1.0 / Length;
	return static_cast<float>(DeltaX * Scale * Forward.X + DeltaY * Scale * Forward.Y);
}

bool ElysiumTerminalCone::Faces(const FVector& ScreenCm, const FVector& ScreenAxisCm,
	const FVector& EyeCm)
{
	return FacingDot(ScreenCm, ScreenAxisCm, EyeCm) > FacingCosine;
}
