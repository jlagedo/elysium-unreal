#include "UI/ElysiumInputGlyphControllerData.h"

#include "ElysiumInputAssets.h"

#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumInputGlyphs, Log, All);

namespace
{
	const FVector2D GlyphSize(48.0, 48.0);

	FString TexturePath(const TCHAR* Family, const TCHAR* SourceStem)
	{
		return FString::Printf(TEXT("/Game/Input/Glyphs/Kenney/%s/T_Kenney_%s.T_Kenney_%s"),
			Family, SourceStem, SourceStem);
	}

	void AddStandardGamepadGlyphs(
		UElysiumInputGlyphControllerData& Data, const TCHAR* Family,
		const TCHAR* FaceBottom, const TCHAR* FaceRight, const TCHAR* FaceLeft, const TCHAR* FaceTop,
		const TCHAR* LeftShoulder, const TCHAR* RightShoulder,
		const TCHAR* LeftTrigger, const TCHAR* RightTrigger,
		const TCHAR* LeftThumb, const TCHAR* RightThumb,
		const TCHAR* LeftStick, const TCHAR* RightStick,
		const TCHAR* DPadUp, const TCHAR* DPadDown, const TCHAR* DPadLeft, const TCHAR* DPadRight,
		const TCHAR* SpecialLeft, const TCHAR* SpecialRight)
	{
		Data.AddGlyph(EKeys::Gamepad_FaceButton_Bottom, *TexturePath(Family, FaceBottom));
		Data.AddGlyph(EKeys::Gamepad_FaceButton_Right, *TexturePath(Family, FaceRight));
		Data.AddGlyph(EKeys::Gamepad_FaceButton_Left, *TexturePath(Family, FaceLeft));
		Data.AddGlyph(EKeys::Gamepad_FaceButton_Top, *TexturePath(Family, FaceTop));
		Data.AddGlyph(EKeys::Gamepad_LeftShoulder, *TexturePath(Family, LeftShoulder));
		Data.AddGlyph(EKeys::Gamepad_RightShoulder, *TexturePath(Family, RightShoulder));
		Data.AddGlyph(EKeys::Gamepad_LeftTriggerAxis, *TexturePath(Family, LeftTrigger));
		Data.AddGlyph(EKeys::Gamepad_RightTriggerAxis, *TexturePath(Family, RightTrigger));
		Data.AddGlyph(EKeys::Gamepad_LeftThumbstick, *TexturePath(Family, LeftThumb));
		Data.AddGlyph(EKeys::Gamepad_RightThumbstick, *TexturePath(Family, RightThumb));
		Data.AddGlyph(EKeys::Gamepad_Left2D, *TexturePath(Family, LeftStick));
		Data.AddGlyph(EKeys::Gamepad_Right2D, *TexturePath(Family, RightStick));
		Data.AddGlyph(EKeys::Gamepad_DPad_Up, *TexturePath(Family, DPadUp));
		Data.AddGlyph(EKeys::Gamepad_DPad_Down, *TexturePath(Family, DPadDown));
		Data.AddGlyph(EKeys::Gamepad_DPad_Left, *TexturePath(Family, DPadLeft));
		Data.AddGlyph(EKeys::Gamepad_DPad_Right, *TexturePath(Family, DPadRight));
		Data.AddGlyph(EKeys::Gamepad_Special_Left, *TexturePath(Family, SpecialLeft));
		Data.AddGlyph(EKeys::Gamepad_Special_Right, *TexturePath(Family, SpecialRight));
	}
}

bool UElysiumInputGlyphControllerData::TryGetInputBrush(
	FSlateBrush& OutBrush, const FKey& Key) const
{
	if (Super::TryGetInputBrush(OutBrush, Key))
	{
		return true;
	}

	const TSoftObjectPtr<UTexture2D>* Glyph = GlyphTextures.Find(Key.GetFName());
	if (!Glyph)
	{
		return false;
	}

	UTexture2D* Texture = Glyph->LoadSynchronous();
	if (!Texture)
	{
		if (!FailedGlyphLoads.Contains(Key.GetFName()))
		{
			UE_LOG(LogElysiumInputGlyphs, Warning,
				TEXT("CommonInput glyph texture failed to load for %s from %s; prompt will use text"),
				*Key.ToString(), *Glyph->ToSoftObjectPath().ToString());
			FailedGlyphLoads.Add(Key.GetFName());
		}
		return false;
	}

	FCommonInputKeyBrushConfiguration Entry;
	Entry.Key = Key;
	Entry.KeyBrush.SetResourceObject(Texture);
	Entry.KeyBrush.DrawAs = ESlateBrushDrawType::Image;
	Entry.KeyBrush.ImageSize = GlyphSize;
	UElysiumInputGlyphControllerData* MutableThis =
		const_cast<UElysiumInputGlyphControllerData*>(this);
	MutableThis->InputBrushDataMap.Add(MoveTemp(Entry));
	return Super::TryGetInputBrush(OutBrush, Key);
}

bool UElysiumInputGlyphControllerData::TryGetInputBrush(
	FSlateBrush& OutBrush, const TArray<FKey>& Keys) const
{
	return Keys.Num() == 1
		? TryGetInputBrush(OutBrush, Keys[0])
		: Super::TryGetInputBrush(OutBrush, Keys);
}

void UElysiumInputGlyphControllerData::AddGlyph(const FKey& Key, const TCHAR* TexturePathValue)
{
	GlyphTextures.Add(Key.GetFName(), TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TexturePathValue)));
}

void UElysiumInputGlyphControllerData::AddHardwareId(
	const TCHAR* InputDeviceName, const TCHAR* HardwareDeviceIdentifier)
{
	FInputDeviceIdentifierPair& Pair = GamepadHardwareIdMapping.AddDefaulted_GetRef();
	Pair.InputDeviceName = InputDeviceName;
	Pair.HardwareDeviceIdentifier = HardwareDeviceIdentifier;
}

UElysiumKeyboardControllerData::UElysiumKeyboardControllerData()
{
	InputType = ECommonInputType::MouseAndKeyboard;
	AddGlyph(EKeys::E, TEXT("/Game/Input/Glyphs/Kenney/Keyboard/T_Kenney_keyboard_e.T_Kenney_keyboard_e"));
	AddGlyph(EKeys::Enter,
		TEXT("/Game/Input/Glyphs/Kenney/Keyboard/T_Kenney_keyboard_enter.T_Kenney_keyboard_enter"));
	AddGlyph(EKeys::Escape,
		TEXT("/Game/Input/Glyphs/Kenney/Keyboard/T_Kenney_keyboard_escape.T_Kenney_keyboard_escape"));
}

UElysiumXboxControllerData::UElysiumXboxControllerData()
{
	InputType = ECommonInputType::Gamepad;
	GamepadName = TEXT("Xbox");
	GamepadDisplayName = NSLOCTEXT("ElysiumInput", "XboxController", "Xbox Controller");
	AddHardwareId(TEXT("GameInput"), TEXT("XboxOne"));
	AddHardwareId(TEXT("GameInput"), TEXT("Xbox360"));
	AddHardwareId(TEXT("XInputInterface"), TEXT("XInputController"));
	AddStandardGamepadGlyphs(*this, TEXT("Xbox"),
		TEXT("xbox_button_a"), TEXT("xbox_button_b"),
		TEXT("xbox_button_x"), TEXT("xbox_button_y"),
		TEXT("xbox_lb"), TEXT("xbox_rb"), TEXT("xbox_lt"), TEXT("xbox_rt"),
		TEXT("xbox_ls"), TEXT("xbox_rs"), TEXT("xbox_stick_l"), TEXT("xbox_stick_r"),
		TEXT("xbox_dpad_up"), TEXT("xbox_dpad_down"),
		TEXT("xbox_dpad_left"), TEXT("xbox_dpad_right"),
		TEXT("xbox_button_view"), TEXT("xbox_button_menu"));
}

UElysiumDualSenseControllerData::UElysiumDualSenseControllerData()
{
	InputType = ECommonInputType::Gamepad;
	GamepadName = TEXT("DualSense");
	GamepadDisplayName = NSLOCTEXT("ElysiumInput", "DualSenseController", "DualSense Controller");
	AddHardwareId(TEXT("GameInput"), TEXT("DualSense"));
	AddStandardGamepadGlyphs(*this, TEXT("PlayStation"),
		TEXT("playstation_button_cross"), TEXT("playstation_button_circle"),
		TEXT("playstation_button_square"), TEXT("playstation_button_triangle"),
		TEXT("playstation_trigger_l1"), TEXT("playstation_trigger_r1"),
		TEXT("playstation_trigger_l2"), TEXT("playstation_trigger_r2"),
		TEXT("playstation_button_l3"), TEXT("playstation_button_r3"),
		TEXT("playstation_stick_l"), TEXT("playstation_stick_r"),
		TEXT("playstation_dpad_up"), TEXT("playstation_dpad_down"),
		TEXT("playstation_dpad_left"), TEXT("playstation_dpad_right"),
		TEXT("playstation5_touchpad_press"), TEXT("playstation5_button_options"));
	AddGlyph(FKey(ElysiumInputAssets::DualSenseCreateKey),
		TEXT("/Game/Input/Glyphs/Kenney/PlayStation/T_Kenney_playstation5_button_create."
			"T_Kenney_playstation5_button_create"));
	AddGlyph(FKey(ElysiumInputAssets::DualSenseMuteKey),
		TEXT("/Game/Input/Glyphs/Kenney/PlayStation/T_Kenney_playstation5_button_mute."
			"T_Kenney_playstation5_button_mute"));
}
