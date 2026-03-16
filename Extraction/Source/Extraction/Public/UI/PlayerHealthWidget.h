// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHealthWidget.generated.h"

class UProgressBar;
class UHealthComponent;

/**
 * Simple HUD widget displaying health (red) and shield (blue) bars.
 * Requires a Widget Blueprint with ProgressBars named "HealthBar" and "ShieldBar".
 */
UCLASS()
class EXTRACTION_API UPlayerHealthWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> ShieldBar;

private:

	bool BindHealthComponent();

	UFUNCTION()
	void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void OnHealthUpdated(float CurrentHealth, float MaxHealth);

	UFUNCTION()
	void OnShieldUpdated(float CurrentShield, float MaxShield);

	TWeakObjectPtr<UHealthComponent> CachedHealthComponent;
};
