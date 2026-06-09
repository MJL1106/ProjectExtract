// UBarkFeedWidget — subscribes the subtitle feed to the bark subsystem.

#include "BarkFeedWidget.h"
#include "BarkSubsystem.h"
#include "Engine/World.h"

void UBarkFeedWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
		if (UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>())
			Barks->OnBarkRequested.AddUniqueDynamic(this, &UBarkFeedWidget::HandleBarkRequested);
}

void UBarkFeedWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
		if (UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>())
			Barks->OnBarkRequested.RemoveDynamic(this, &UBarkFeedWidget::HandleBarkRequested);

	Super::NativeDestruct();
}

void UBarkFeedWidget::HandleBarkRequested(FText SpeakerName, FText Line)
{
	OnBarkReceived(SpeakerName, Line);
}
