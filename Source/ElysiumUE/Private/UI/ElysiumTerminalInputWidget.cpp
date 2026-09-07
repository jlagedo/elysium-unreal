#include "UI/ElysiumTerminalInputWidget.h"

#include "UI/SElysiumTerminalInput.h"

void UElysiumTerminalInputWidget::SetSession(const FElysiumTerminalView& View)
{
	PendingSession = View;
	if (Input.IsValid())
	{
		Input->SetSession(View);
	}
}

FString UElysiumTerminalInputWidget::GetDraft() const
{
	return Input.IsValid() ? Input->Draft() : FString();
}

void UElysiumTerminalInputWidget::ClearDraft()
{
	if (Input.IsValid())
	{
		Input->ClearDraft();
	}
}

TSharedRef<SWidget> UElysiumTerminalInputWidget::RebuildWidget()
{
	Input = SNew(SElysiumTerminalInput);
	Input->OnSubmitLine = OnSubmitLine;
	Input->OnSubmitCharacter = OnSubmitCharacter;
	Input->OnAcknowledge = OnAcknowledge;
	Input->OnQuit = OnQuit;
	Input->OnBreak = OnBreak;
	Input->OnDraftChanged = OnDraftChanged;
	Input->SetSession(PendingSession);
	return Input.ToSharedRef();
}

void UElysiumTerminalInputWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Input.Reset();
}
