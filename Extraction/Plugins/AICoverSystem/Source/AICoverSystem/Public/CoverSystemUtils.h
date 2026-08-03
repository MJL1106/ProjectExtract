//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AIController.h"
#include "CoverSystemPublicData.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTFunctionLibrary.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"

#include "AI/BlackboardKeyType_Cover.h"

#include "CoverSystemUtils.generated.h"

/**
 * Utility library for cover system
 */
UCLASS()
class AICOVERSYSTEM_API UCoverSystemUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	// Gets blackboard value as cover by selected key
	UFUNCTION(BlueprintPure, Category = "Cover", Meta = (HidePin = "NodeOwner", DefaultToSelf = "NodeOwner"))
	static FCover GetBlackboardValueAsCover(UBTNode* NodeOwner, const FBlackboardKeySelector& Key);

	// Sets blackboard value as cover by selected key
	UFUNCTION(BlueprintCallable, Category = "Cover", Meta = (HidePin = "NodeOwner", DefaultToSelf = "NodeOwner"))
	static void SetBlackboardValueAsCover(UBTNode* NodeOwner, const FBlackboardKeySelector& Key, FCover Value);

	// Checks if cover is valid
	UFUNCTION(BlueprintPure, Category = "Cover")
	static bool IsCoverValid(const FCover& Cover);

	// Checks if a given handle is valid
	UFUNCTION(BlueprintPure, Category = "Cover")
	static bool IsCoverHandleValid(const FCoverHandle& Handle);

	/**
	* Blueprint utility function to fetch result from EQS instance as covers.
	* Usage:
	* 1. RunEQSQuery -> returns query instance
	* 2. Bind event into OnQueryFinished
	* 3. If the query status was success, call this function to fetch all results
	* Note that run mode affects if single or multiple results are returned
	*/
	UFUNCTION(BlueprintCallable, Category = "Cover")
	static bool GetEnvironmentQueryResultsAsCovers(UEnvQueryInstanceBlueprintWrapper* QueryInstance, TArray<FCover>& ResultCovers);
};
