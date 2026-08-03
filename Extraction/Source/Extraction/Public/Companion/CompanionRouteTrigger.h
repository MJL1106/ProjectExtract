// Overlap trigger that starts a pre-authored companion route when the player enters.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Companion/CompanionRouteTypes.h"
#include "CompanionRouteTrigger.generated.h"

class UBoxComponent;
class UBillboardComponent;
class ACompanionRoute;

UCLASS(Blueprintable)
class EXTRACTION_API ACompanionRouteTrigger : public AActor
{
	GENERATED_BODY()

public:
	ACompanionRouteTrigger();

	/** What this trigger does when the player enters it. Execute Route is the original behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Trigger")
	ECompanionRouteTriggerMode Mode = ECompanionRouteTriggerMode::ExecuteRoute;

	/** The route to act on when the player enters the trigger.
	 *  Walked in Execute Route mode, faced along in Set Facing Reference mode, unused by Clear. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Route Trigger",
		meta = (EditCondition = "Mode != ECompanionRouteTriggerMode::ClearFacingReference"))
	TObjectPtr<ACompanionRoute> Route;

	/** If true, the trigger fires only once and then disables itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Trigger")
	bool bOneShot = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Route Trigger")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(VisibleAnywhere, Category = "Route Trigger")
	TObjectPtr<UBillboardComponent> Billboard;

	/** Set after firing once when bOneShot is true. */
	bool bHasFired = false;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
