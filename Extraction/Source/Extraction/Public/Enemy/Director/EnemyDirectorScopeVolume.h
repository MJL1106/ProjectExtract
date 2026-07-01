// AEnemyDirectorScopeVolume - optional box that limits director counting and spawn-zone selection.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyDirectorScopeVolume.generated.h"

class UBillboardComponent;
class UBoxComponent;

UCLASS(Blueprintable)
class EXTRACTION_API AEnemyDirectorScopeVolume : public AActor
{
	GENERATED_BODY()

public:
	AEnemyDirectorScopeVolume();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Director Scope")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Director Scope")
	FName ScopeTag;

	UFUNCTION(BlueprintPure, Category = "Director Scope")
	bool IsScopeEnabled() const { return bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Director Scope")
	FVector GetScopeOrigin() const;

	bool ContainsActor(const AActor* Actor) const;
	bool ContainsPoint(const FVector& Point) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Director Scope")
	TObjectPtr<UBoxComponent> ScopeBox;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Director Scope")
	TObjectPtr<UBillboardComponent> Billboard;
#endif
};
