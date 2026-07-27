#include "Debug/ElysiumCogStyle.h"

#if ENABLE_COG

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarCogTheme(
		TEXT("elysium.CogTheme"),
		1,
		TEXT("Debug-UI skin. 1 = the VtMB blood-and-bone theme, 0 = stock ImGui dark."),
		ECVF_Cheat);

	// What the last EnsureApplied installed: -1 nothing yet, 0 stock dark, 1 the VtMB theme. Paired
	// with the DPI scale, because the size half of the style has to be re-scaled when that changes.
	int32 GAppliedTheme = -1;
	float GAppliedDpiScale = 0.0f;

	ImVec4 Alpha(const ImVec4& InColor, float InAlpha)
	{
		return ImVec4(InColor.x, InColor.y, InColor.z, InAlpha);
	}

	void ApplyColors(ImGuiStyle& Style)
	{
		using namespace ElysiumCogStyle;
		ImVec4* C = Style.Colors;

		C[ImGuiCol_Text] = Bone;
		C[ImGuiCol_TextDisabled] = BoneDim;
		C[ImGuiCol_WindowBg] = Ink;
		C[ImGuiCol_ChildBg] = InkChild;
		C[ImGuiCol_PopupBg] = InkPopup;
		C[ImGuiCol_Border] = Border;
		C[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

		C[ImGuiCol_FrameBg] = Frame;
		C[ImGuiCol_FrameBgHovered] = FrameHovered;
		C[ImGuiCol_FrameBgActive] = FrameActive;

		C[ImGuiCol_TitleBg] = InkTitle;
		C[ImGuiCol_TitleBgActive] = BloodDim;
		C[ImGuiCol_TitleBgCollapsed] = Alpha(InkTitle, 0.75f);
		C[ImGuiCol_MenuBarBg] = ImVec4(0.086f, 0.067f, 0.071f, 1.0f);

		C[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.031f, 0.031f, 0.60f);
		C[ImGuiCol_ScrollbarGrab] = ImVec4(0.235f, 0.161f, 0.169f, 1.0f);
		C[ImGuiCol_ScrollbarGrabHovered] = Blood;
		C[ImGuiCol_ScrollbarGrabActive] = BloodHi;

		C[ImGuiCol_CheckMark] = BloodBright;
		C[ImGuiCol_SliderGrab] = Blood;
		C[ImGuiCol_SliderGrabActive] = BloodBright;

		C[ImGuiCol_Button] = ImVec4(0.208f, 0.106f, 0.118f, 1.0f);
		C[ImGuiCol_ButtonHovered] = Blood;
		C[ImGuiCol_ButtonActive] = BloodHi;

		C[ImGuiCol_Header] = Alpha(BloodDim, 0.85f);
		C[ImGuiCol_HeaderHovered] = Alpha(Blood, 0.80f);
		C[ImGuiCol_HeaderActive] = BloodHi;

		C[ImGuiCol_Separator] = Border;
		C[ImGuiCol_SeparatorHovered] = Blood;
		C[ImGuiCol_SeparatorActive] = BloodHi;

		C[ImGuiCol_ResizeGrip] = Alpha(Blood, 0.25f);
		C[ImGuiCol_ResizeGripHovered] = Alpha(Blood, 0.70f);
		C[ImGuiCol_ResizeGripActive] = BloodHi;

		C[ImGuiCol_TabHovered] = Alpha(Blood, 0.85f);
		C[ImGuiCol_Tab] = InkTitle;
		C[ImGuiCol_TabSelected] = BloodDim;
		C[ImGuiCol_TabSelectedOverline] = BloodBright;
		C[ImGuiCol_TabDimmed] = Alpha(Ink, 1.0f);
		C[ImGuiCol_TabDimmedSelected] = ImVec4(0.200f, 0.090f, 0.102f, 1.0f);
		C[ImGuiCol_TabDimmedSelectedOverline] = BloodDim;

		C[ImGuiCol_DockingPreview] = Alpha(Blood, 0.70f);
		C[ImGuiCol_DockingEmptyBg] = ImVec4(0.055f, 0.047f, 0.051f, 1.0f);

		C[ImGuiCol_PlotLines] = Gold;
		C[ImGuiCol_PlotLinesHovered] = BloodBright;
		C[ImGuiCol_PlotHistogram] = Blood;
		C[ImGuiCol_PlotHistogramHovered] = BloodBright;

		C[ImGuiCol_TableHeaderBg] = ImVec4(0.153f, 0.098f, 0.106f, 1.0f);
		C[ImGuiCol_TableBorderStrong] = ImVec4(0.278f, 0.176f, 0.188f, 1.0f);
		C[ImGuiCol_TableBorderLight] = ImVec4(0.184f, 0.129f, 0.137f, 1.0f);
		C[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		C[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);

		C[ImGuiCol_TextLink] = Gold;
		C[ImGuiCol_TextSelectedBg] = Alpha(Blood, 0.45f);
		C[ImGuiCol_DragDropTarget] = Candle;
		C[ImGuiCol_NavCursor] = BloodBright;
		C[ImGuiCol_NavWindowingHighlight] = Alpha(Bone, 0.70f);
		C[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
		C[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
	}

	void ApplySizes(ImGuiStyle& Style)
	{
		// Tight but not cramped: these windows are dense tables of small numbers, so the win is in
		// row height and cell padding rather than in generous margins. Corners stay nearly square —
		// the game's panels are cut edges and hard rules, not rounded chrome.
		Style.WindowPadding = ImVec2(8.0f, 6.0f);
		Style.FramePadding = ImVec2(5.0f, 3.0f);
		Style.CellPadding = ImVec2(5.0f, 3.0f);
		Style.ItemSpacing = ImVec2(7.0f, 5.0f);
		Style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
		Style.IndentSpacing = 18.0f;
		Style.ScrollbarSize = 12.0f;
		Style.GrabMinSize = 10.0f;

		Style.WindowBorderSize = 1.0f;
		Style.ChildBorderSize = 1.0f;
		Style.PopupBorderSize = 1.0f;
		Style.FrameBorderSize = 0.0f;
		Style.TabBorderSize = 0.0f;

		Style.WindowRounding = 2.0f;
		Style.ChildRounding = 2.0f;
		Style.FrameRounding = 2.0f;
		Style.PopupRounding = 2.0f;
		Style.ScrollbarRounding = 2.0f;
		Style.GrabRounding = 2.0f;
		Style.TabRounding = 2.0f;

		Style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		Style.SeparatorTextBorderSize = 2.0f;
		Style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
		Style.SeparatorTextPadding = ImVec2(16.0f, 4.0f);
	}
}

void ElysiumCogStyle::EnsureApplied(float InDpiScale)
{
	const int32 Wanted = CVarCogTheme.GetValueOnAnyThread() != 0 ? 1 : 0;
	ImGuiStyle& Style = ImGui::GetStyle();

	// Cog rebuilds the style from ImGui's defaults on a DPI change, which silently drops the theme.
	// One colour is enough to detect that: nothing else writes this slot.
	const ImVec4& Sentinel = Style.Colors[ImGuiCol_TitleBgActive];
	const bool bIsTheme = Sentinel.x == BloodDim.x && Sentinel.y == BloodDim.y && Sentinel.z == BloodDim.z;
	const bool bStyleMatchesWanted = (Wanted == 1) == bIsTheme;

	if (GAppliedTheme == Wanted && GAppliedDpiScale == InDpiScale && bStyleMatchesWanted)
	{
		return;
	}

	Style = ImGuiStyle();   // from the defaults, so a re-apply never compounds ScaleAllSizes
	if (Wanted == 1)
	{
		ApplyColors(Style);
		ApplySizes(Style);
	}
	Style.ScaleAllSizes(InDpiScale > 0.0f ? InDpiScale : 1.0f);

	GAppliedTheme = Wanted;
	GAppliedDpiScale = InDpiScale;
}

void ElysiumCogStyle::LabelValue(const char* InLabel, const char* InValue, float InValueColumn,
	const ImVec4* InValueColor)
{
	ImGui::TextUnformatted(InLabel);
	ImGui::SameLine(0.0f, 0.0f);
	const float AfterLabel = ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetCursorPosX(FMath::Max(AfterLabel, InValueColumn));

	if (InValueColor != nullptr)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, *InValueColor);
	}
	ImGui::TextUnformatted(InValue);
	if (InValueColor != nullptr)
	{
		ImGui::PopStyleColor();
	}
}

#endif // ENABLE_COG
