// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerHealthWidget.h"
#include "Components/ProgressBar.h"
#include "HealthComponent.h"
#include "ExtractionCharacter.h"

void UPlayerHealthWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!BindHealthComponent())
	{
		// Pawn not possessed yet — retry when it arrives
		if (APlayerController* PC = GetOwningPlayer())
			PC->OnPossessedPawnChanged.AddDynamic(this, &UPlayerHealthWidget::OnPawnChanged);
	}
}

void UPlayerHealthWidget::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UPlayerHealthWidget::OnPawnChanged);

	if (CachedHealthComponent.IsValid())
	{
		CachedHealthComponent->OnHealthChanged.RemoveDynamic(this, &UPlayerHealthWidget::OnHealthUpdated);
		CachedHealthComponent->OnShieldChanged.RemoveDynamic(this, &UPlayerHealthWidget::OnShieldUpdated);
	}

	Super::NativeDestruct();
}

bool UPlayerHealthWidget::BindHealthComponent()
{
	const APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return false;

	const AExtractionCharacter* Character = Cast<AExtractionCharacter>(PC->GetPawn());
	if (!IsValid(Character)) return false;

	UHealthComponent* HC = Character->GetHealthComponent();
	if (!IsValid(HC)) return false;

	CachedHealthComponent = HC;

	HC->OnHealthChanged.AddDynamic(this, &UPlayerHealthWidget::OnHealthUpdated);
	HC->OnShieldChanged.AddDynamic(this, &UPlayerHealthWidget::OnShieldUpdated);

	// Set initial values
	if (IsValid(HealthBar))
		HealthBar->SetPercent(HC->GetHealthPercent());
	if (IsValid(ShieldBar))
		ShieldBar->SetPercent(HC->GetShieldPercent());

	return true;
}

void UPlayerHealthWidget::OnPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (BindHealthComponent())
	{
		// Successfully bound — stop listening for pawn changes
		if (APlayerController* PC = GetOwningPlayer())
			PC->OnPossessedPawnChanged.RemoveDynamic(this, &UPlayerHealthWidget::OnPawnChanged);
	}
}

void UPlayerHealthWidget::OnHealthUpdated(float CurrentHealth, float MaxHealth)
{
	if (IsValid(HealthBar))
		HealthBar->SetPercent(MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f);
}

void UPlayerHealthWidget::OnShieldUpdated(float CurrentShield, float MaxShield)
{
	if (IsValid(ShieldBar))
		ShieldBar->SetPercent(MaxShield > 0.f ? CurrentShield / MaxShield : 0.f);
}
