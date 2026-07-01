// AEnemySpawnZone — designer-placed box that the director pulls spawn points from.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyTypes.h"
#include "EnemySpawnZone.generated.h"

class UBoxComponent;
class UBillboardComponent;
class UDirectorConfigData;

UCLASS(Blueprintable)
class EXTRACTION_API AEnemySpawnZone : public AActor
{
	GENERATED_BODY()

public:
	AEnemySpawnZone();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// --- Designer config ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnZone", meta = (ToolTip = "Optional designer label for this zone or area. Current director selection does not require it; use it for organization, logs, and future tuning."))
	FName AreaTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnZone", meta = (ToolTip = "Mission phases allowed to use this zone. Empty means all phases. Use this to make route-, room-, or floor-specific spawn rooms."))
	TArray<EMissionPhase> ActivePhases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnZone", meta = (ToolTip = "Optional config asset pushed to the world director when this zone registers and no explicit config has been set. Prefer using the same config for all zones in a level."))
	TObjectPtr<UDirectorConfigData> DirectorConfig;

	// --- API for the director ---

	/** True if this zone should produce spawns during the given phase. Empty ActivePhases = all phases. */
	bool IsActiveForPhase(EMissionPhase Phase) const;

	/** Deterministic spread inside the box. Index-based so a squad gets separated points. Z at box base. */
	FTransform GetSpawnTransform(int32 Index) const;

	/** World-space centre of the box volume. */
	FVector GetZoneOrigin() const;

	/** Optional config pushed to the director on registration (first wins). */
	UFUNCTION(BlueprintPure, Category = "SpawnZone")
	UDirectorConfigData* GetDirectorConfig() const { return DirectorConfig; }

private:
	UPROPERTY(VisibleAnywhere, Category = "SpawnZone")
	TObjectPtr<UBoxComponent> ZoneBox;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "SpawnZone")
	TObjectPtr<UBillboardComponent> Billboard;
#endif
};
