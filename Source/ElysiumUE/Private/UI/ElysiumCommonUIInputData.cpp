#include "UI/ElysiumCommonUIInputData.h"

#include "Engine/DataTable.h"
#include "InputCoreTypes.h"

UElysiumCommonUIInputData::UElysiumCommonUIInputData()
{
	ActionTable = CreateDefaultSubobject<UDataTable>(TEXT("CommonUIActions"));
	ActionTable->RowStruct = FElysiumCommonInputActionData::StaticStruct();

	FElysiumCommonInputActionData Accept;
	Accept.DisplayName = NSLOCTEXT("ElysiumUI", "Accept", "Accept");
	Accept.SetKeys(EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom);
	ActionTable->AddRow(TEXT("Accept"), Accept);
	DefaultClickAction.DataTable = ActionTable;
	DefaultClickAction.RowName = TEXT("Accept");

	FElysiumCommonInputActionData Back;
	Back.DisplayName = NSLOCTEXT("ElysiumUI", "Back", "Back");
	Back.SetKeys(EKeys::Escape, EKeys::Gamepad_FaceButton_Right);
	ActionTable->AddRow(TEXT("Back"), Back);
	DefaultBackAction.DataTable = ActionTable;
	DefaultBackAction.RowName = TEXT("Back");

	FElysiumCommonInputActionData Use;
	Use.DisplayName = NSLOCTEXT("ElysiumUI", "Use", "Use");
	Use.SetKeys(EKeys::E, EKeys::Gamepad_RightShoulder);
	ActionTable->AddRow(TEXT("Use"), Use);
	UseAction.DataTable = ActionTable;
	UseAction.RowName = TEXT("Use");
}
