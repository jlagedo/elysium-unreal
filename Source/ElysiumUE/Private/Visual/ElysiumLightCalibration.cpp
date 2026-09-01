#include "ElysiumLightCalibration.h"

const FElysiumLightCalibrationRow* UElysiumLightCalibration::FindRow(int32 SourceIndex) const
{
	return Rows.FindByPredicate([SourceIndex](const FElysiumLightCalibrationRow& Row)
	{
		return Row.SourceIndex == SourceIndex;
	});
}
