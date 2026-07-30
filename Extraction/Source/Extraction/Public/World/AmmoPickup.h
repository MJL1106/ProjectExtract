// AAmmoPickup — physical ammo drop, collected by walking over it (bAutoCollectOnOverlap) or through
// the kit interaction system (BP children add the interaction area + prompt widget and call
// TryCollect on interact). Player and companion both trip the walk-over, but the ammo always lands
// in the PLAYER's reserve — the companion has no inventory.
// Spawned by AEnemyCharacter death rolls; mesh is assigned in the Blueprint subclass.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemy/EnemyTypes.h"
#include "AmmoPickup.generated.h"

class APawn;
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

	/** Grants the ammo to the collector and destroys the pickup. Returns false (and stays in
	 *  the world) when the grant was refused — e.g. no carried weapon uses this category. */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	bool TryCollect(APawn* Collector);

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

	/** Walk-over collection. Clear it on a hand-placed pickup that should need a deliberate interact.
	 *  Never applies to an item seated in a weapon case: BeginPlay leaves the overlap unbound there,
	 *  so brushing past a full case can't vacuum it. */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	bool bAutoCollectOnOverlap = true;

private:
	void ExpireLifespan();

	/** Binds the overlap handlers, then sweeps for a pawn already standing on the drop. */
	void EnableAutoCollect();

	/** Walk-over collection for one overlapping pawn. Friendly pawns only, and the grant is always
	 *  routed to the player. Silent on refusal for every collector including the player — the
	 *  "no compatible weapon" toast belongs to the deliberate TryCollect interact. */
	void TryAutoCollect(APawn* OverlappingPawn);

	/** Team attitude against the player pawn — enemies never collect. Takes the already-resolved
	 *  player pawn so one overlap event resolves it once. */
	bool IsFriendlyCollector(const APawn* OverlappingPawn, const APawn* PlayerPawn) const;

	/** True when the player carries a weapon that takes this drop's ammo category. */
	bool PlayerHasCompatibleWeapon(const APawn* PlayerPawn) const;

	/** Adds a pawn to RefusedCollectors, dropping stale entries on the way in. */
	void MarkCollectorRefused(APawn* OverlappingPawn);

	UFUNCTION()
	void OnSphereBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereEndOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	FTimerHandle LifespanTimerHandle;

	/** Pawns whose walk-over attempt was refused, so a pawn parked in the sphere retries at most
	 *  once per overlap. Cleared for that pawn on end-overlap, so stepping off and back on (after a
	 *  reload, or after picking up a matching gun) tries again. Bounded at the two or three allies
	 *  that can stand on one drop — no cleanup timer, stale entries are pruned on insert. */
	TSet<TWeakObjectPtr<APawn>> RefusedCollectors;
};
