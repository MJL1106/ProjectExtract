//// Copyright, (c) Sami Kangasmaa 2023


#include "CoverPartitionInvokerComponent.h"
#include "CoverSystem.h"

UCoverPartitionInvokerComponent::UCoverPartitionInvokerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCoverPartitionInvokerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoRegisterInvoker)
	{
		RegisterInvoker();
	}
}

void UCoverPartitionInvokerComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (bHasRegisteredAsInvoker)
	{
		UnregisterInvoker();
	}
	Super::EndPlay(Reason);
}

FVector UCoverPartitionInvokerComponent::GetInvokerLocation() const
{
	if (GetOwner())
	{
		return GetOwner()->GetActorLocation();
	}
	return FVector::ZeroVector;
}

void UCoverPartitionInvokerComponent::RegisterInvoker()
{
	if (ACoverSystem* CoverSystem = ACoverSystem::GetCoverSystem(this))
	{
		if (!bHasRegisteredAsInvoker)
		{
			CoverSystem->RegisterPartitionInvoker(this);
			bHasRegisteredAsInvoker = true;
		}
	}
}

void UCoverPartitionInvokerComponent::UnregisterInvoker()
{
	if (ACoverSystem* CoverSystem = ACoverSystem::GetCoverSystem(this))
	{
		if (bHasRegisteredAsInvoker)
		{
			CoverSystem->UnregisterPartitionInvoker(this);
			bHasRegisteredAsInvoker = false;
		}
	}
}