// AAmmoPickup — physical ammo drop collected by player overlap, but only when its category
// matches the player's current weapon (mismatches stay on the ground for a later weapon swap).
// Spawned by AEnemyCharacter death rolls; mesh is assigned in the Blueprint subclass.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemy/EnemyTypes.h"
#include "AmmoPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class EXTRACTION_API AAmmoPickup : public AActor
{
	GENERATED_BODY()

public:
	AAmmoPickup();

	/** Stamp category + amount after a deferred spawn (before FinishSpawning). */
	void InitPickup(EEnemyWeaponAnimType InCategory, int32 InAmount);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<USphereComponent> OverlapSphere;

	/** Visual — mesh assigned in the BP child, no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Ammo category this drop feeds. Editable for hand-placed pickups; InitPickup overrides on spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	EEnemyWeaponAnimType Category = EEnemyWeaponAnimType::Rifle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "0"))
	int32 Amount = 30;

	/** Seconds before an uncollected drop despawns. <= 0 = never despawns. */
	UPROPERTY(EditAnywhere, Category = "Pickup", meta = (ClampMin = "0.0"))
	float Lifespan = 60.f;

private:
	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ExpireLifespan();

	FTimerHandle LifespanTimerHandle;
};
