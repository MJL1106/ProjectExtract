// BT service — periodically probes for traversal opportunities ahead of the companion.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_TraversalProbe.generated.h"

UCLASS()
class EXTRACTION_API UBTService_TraversalProbe : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_TraversalProbe();

	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** 2D speed below this skips the probe (companion standing still). */
	UPROPERTY(EditAnywhere, Category = "Probe")
	float MinSpeed2DToProbe = 50.f;

	/** Squared 2D speed threshold — when below this while wanting to move, companion is considered stuck. */
	UPROPERTY(EditAnywhere, Category = "Probe")
	float StuckVelocityThreshold = 100.f;

	/** When true, only probe when the companion appears blocked (low velocity while accelerating). */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bRequireBlocked = false;

	/** Draw a bright sphere + forward arrow at the companion every probe tick. */
	UPROPERTY(EditAnywhere, Category = "Probe|Debug")
	bool bDrawDebug = false;
};
