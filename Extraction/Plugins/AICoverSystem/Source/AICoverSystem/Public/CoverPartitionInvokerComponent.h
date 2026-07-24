//// Copyright, (c) Sami Kangasmaa 2023

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CoverPartitionInvokerComponent.generated.h"

/**
 * Component that registers to Cover System and acts as partition invoker, so partitions around the owner of this component are generated
 */
UCLASS(ClassGroup = ("Cover System"), meta = (BlueprintSpawnableComponent))
class AICOVERSYSTEM_API UCoverPartitionInvokerComponent : public UActorComponent
{
	GENERATED_BODY()

public:	

	UCoverPartitionInvokerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;

protected:

	// Should this automatically register as invoker on begin play?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover System")
	bool bAutoRegisterInvoker = true;

public:

	// Returns location of the invoker (owner actor by default)
	virtual FVector GetInvokerLocation() const;

	// Registers this invoker to cover system to enable it
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void RegisterInvoker();

	// Unregisters this invoker from cover system to disable it
	UFUNCTION(BlueprintCallable, Category = "Cover System")
	void UnregisterInvoker();

	// Is the invoker currently registered to cover system as invoker?
	UFUNCTION(BlueprintPure, Category = "Cover System")
	FORCEINLINE bool HasRegistedInvoker() const { return bHasRegisteredAsInvoker; }

private:

	bool bHasRegisteredAsInvoker = false;
};
