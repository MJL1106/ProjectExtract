//// Copyright, (c) Sami Kangasmaa 2022


#include "CoverSystemUtils.h"
#include "AI/EnvQueryItemType_CoverBase.h"

FCover UCoverSystemUtils::GetBlackboardValueAsCover(UBTNode* NodeOwner, const FBlackboardKeySelector& Key)
{
	UBlackboardComponent* BlackboardComp = UBTFunctionLibrary::GetOwnersBlackboard(NodeOwner);
	return BlackboardComp ? BlackboardComp->GetValue<UBlackboardKeyType_Cover>(Key.SelectedKeyName) : FCover();
}

void UCoverSystemUtils::SetBlackboardValueAsCover(UBTNode* NodeOwner, const FBlackboardKeySelector& Key, FCover Value)
{
	if (UBlackboardComponent* BlackboardComp = UBTFunctionLibrary::GetOwnersBlackboard(NodeOwner))
	{
		BlackboardComp->SetValue<UBlackboardKeyType_Cover>(Key.SelectedKeyName, Value);
	}
}

bool UCoverSystemUtils::IsCoverValid(const FCover& Cover)
{
	return Cover.IsValid();
}

bool UCoverSystemUtils::IsCoverHandleValid(const FCoverHandle& Handle)
{
	return Handle.IsValid();
}

bool UCoverSystemUtils::GetEnvironmentQueryResultsAsCovers(UEnvQueryInstanceBlueprintWrapper* QueryInstance, TArray<FCover>& ResultCovers)
{
	if (!QueryInstance)
	{
		return false;
	}

	const FEnvQueryInstance* NativeQueryInstance = QueryInstance->GetQueryInstance();
	if (!NativeQueryInstance || NativeQueryInstance->GetRawStatus() != EEnvQueryStatus::Success)
	{
		return false;
	}

	const FEnvQueryResult* QueryResult = QueryInstance->GetQueryResult();
	if (!QueryResult)
	{
		return false;
	}

	if(QueryResult->GetRawStatus() == EEnvQueryStatus::Success &&
	(QueryResult->ItemType.Get() != nullptr) &&
	QueryResult->ItemType->IsChildOf(UEnvQueryItemType_CoverBase::StaticClass()))
	{
		if (QueryResult->Items.Num() > 0)
		{
			if (QueryInstance->GetRunMode() != EEnvQueryRunMode::AllMatching)
			{
				ResultCovers.Add(UEnvQueryItemType_CoverBase::GetValue(QueryResult->RawData.GetData() + QueryResult->Items[0].DataOffset));
			}
			else
			{
				ResultCovers.Reserve(ResultCovers.Num() + QueryResult->Items.Num());
				for (const FEnvQueryItem& Item : QueryResult->Items)
				{
					ResultCovers.Add(UEnvQueryItemType_CoverBase::GetValue(QueryResult->RawData.GetData() + Item.DataOffset));
				}
			}

			return (ResultCovers.Num() > 0);
		}
	}

	return false;
}