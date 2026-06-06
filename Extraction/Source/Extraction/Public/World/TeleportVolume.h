// Designer-placed volume that teleports the player and companion to configured landing
// spots and restores both to full health on entry. Server-authoritative; overlap replicates
// through CMC / HealthComponent as normal.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleportVolume.generated.h"

class UBoxComponent;
class UArrowComponent;

UCLASS(Blueprintable)
class EXTRACTION_API ATeleportVolume : public AActor
{
	GENERATED_BODY()

public:
	ATeleportVolume();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport|Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport|Components")
	TObjectPtr<UArrowComponent> PlayerSpawnPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport|Components")
	TObjectPtr<UArrowComponent> CompanionSpawnPoint;

	// --- Designer Config ---

	/** If set, teleports the player to this actor's transform instead of the PlayerSpawnPoint arrow. */
	UPROPERTY(EditAnywhere, Category = "Teleport")
	TObjectPtr<AActor> PlayerSpawnOverride;

	/** If set, teleports the companion to this actor's transform instead of the CompanionSpawnPoint arrow. */
	UPROPERTY(EditAnywhere, Category = "Teleport")
	TObjectPtr<AActor> CompanionSpawnOverride;

	/** Seconds before the volume can fire again after a successful teleport. */
	UPROPERTY(EditAnywhere, Category = "Teleport", meta = (ClampMin = "0.0"))
	float RetriggerCooldown = 1.0f;

	/** If true, rotates the player controller to face the player spawn marker's yaw on arrival. */
	UPROPERTY(EditAnywhere, Category = "Teleport")
	bool bFacePlayerToMarker = true;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	void TeleportAndHealPlayer(APawn* PlayerPawn, const FTransform& SpawnTransform);
	void TeleportAndHealCompanion(const FTransform& SpawnTransform);

	FTransform GetPlayerSpawnTransform() const;
	FTransform GetCompanionSpawnTransform() const;

	/** Guards against re-entry within RetriggerCooldown seconds. */
	bool bCooldownActive = false;
	FTimerHandle CooldownTimer;
};
