#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CoverRegistrySubsystem.generated.h"

class AAICoverSlot;

DECLARE_LOG_CATEGORY_EXTERN(LogCoverRegistry, Log, All);

UCLASS()
class EXTRACTION_API UCoverRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterSlot(AAICoverSlot* Slot);
	void UnregisterSlot(AAICoverSlot* Slot);

	void GetSlotsInRadius(const FVector& Origin, float Radius, TArray<AAICoverSlot*>& OutSlots) const;

	AAICoverSlot* FindBestCoverFor(const FVector& QuerierLoc, AActor* Target, float MaxRadius) const;

private:
	TArray<TWeakObjectPtr<AAICoverSlot>> RegisteredSlots;

	bool bReserved = false;

	void PruneStaleSlots();
};
