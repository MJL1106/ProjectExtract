// Passive project-owned wrappers for first-person weapon and attachment art.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlayerWeaponView.generated.h"

class USceneComponent;

UCLASS(Blueprintable)
class EXTRACTION_API APlayerWeaponView : public AActor
{
	GENERATED_BODY()

public:

	APlayerWeaponView();

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation")
	TObjectPtr<USceneComponent> ViewRoot;
};

UCLASS(Blueprintable)
class EXTRACTION_API APlayerWeaponAttachmentView : public AActor
{
	GENERATED_BODY()

public:

	APlayerWeaponAttachmentView();

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation")
	TObjectPtr<USceneComponent> ViewRoot;
};
