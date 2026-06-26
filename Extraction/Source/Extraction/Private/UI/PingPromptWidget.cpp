// UPingPromptWidget implementation.

#include "UI/PingPromptWidget.h"
#include "Components/CompanionCommandComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"

void UPingPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Populate hint text from the designer-editable FText fields.
	if (IsValid(BreachHintText))  BreachHintText->SetText(BreachHint);
	if (IsValid(KnifeHintText))   KnifeHintText->SetText(KnifeHint);
	if (IsValid(ShootHintText))   ShootHintText->SetText(ShootHint);

	// Resolve the command component on the owning player pawn.
	APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return;

	UCompanionCommandComponent* Comp = Pawn->FindComponentByClass<UCompanionCommandComponent>();
	if (!IsValid(Comp)) return;

	CachedCommandComp = Comp;
	if (!Comp->OnPingChanged.IsAlreadyBound(this, &UPingPromptWidget::HandlePingChanged))
	{
		Comp->OnPingChanged.AddDynamic(this, &UPingPromptWidget::HandlePingChanged);
	}

	// Initialise to current state (widget may construct after a ping already fired).
	HandlePingChanged(Comp->GetPendingCommand(), Comp->GetPingedTarget());
}

void UPingPromptWidget::NativeDestruct()
{
	if (CachedCommandComp.IsValid())
	{
		CachedCommandComp->OnPingChanged.RemoveDynamic(this, &UPingPromptWidget::HandlePingChanged);
	}

	Super::NativeDestruct();
}

void UPingPromptWidget::HandlePingChanged(ECompanionCommand PendingCommand, AActor* PingedTarget)
{
	const bool bBreach    = (PendingCommand == ECompanionCommand::Breach);
	const bool bTakedown  = (PendingCommand == ECompanionCommand::Takedown);

	if (IsValid(BreachContainer))
	{
		BreachContainer->SetVisibility(bBreach ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (IsValid(TakedownContainer))
	{
		TakedownContainer->SetVisibility(bTakedown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// Hide the whole widget when no command is pending.
	SetVisibility((bBreach || bTakedown) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}
