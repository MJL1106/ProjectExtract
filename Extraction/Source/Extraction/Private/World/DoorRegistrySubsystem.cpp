// UDoorRegistrySubsystem — door registry for AI acoustic portal queries.

#include "World/DoorRegistrySubsystem.h"
#include "World/DoorBase.h"

void UDoorRegistrySubsystem::Register(ADoorBase* Door)
{
	if (!IsValid(Door)) return;
	Doors.AddUnique(Door);
}

void UDoorRegistrySubsystem::Unregister(ADoorBase* Door)
{
	Doors.RemoveAllSwap([Door](const TWeakObjectPtr<ADoorBase>& Entry)
		{ return !Entry.IsValid() || Entry.Get() == Door; });
}

void UDoorRegistrySubsystem::CollectPortalCandidates(const FVector& A, const FVector& B, int32 MaxCount, TArray<ADoorBase*>& OutDoors) const
{
	struct FScored
	{
		float PathLen;
		ADoorBase* Door;
	};

	TArray<FScored, TInlineAllocator<128>> Scored;
	Scored.Reserve(Doors.Num());

	for (const TWeakObjectPtr<ADoorBase>& Entry : Doors)
	{
		ADoorBase* Door = Entry.Get();
		if (!Door) continue;
		const FVector Portal = Door->GetAcousticPortalPoint();
		Scored.Add({ static_cast<float>(FVector::Dist(A, Portal) + FVector::Dist(Portal, B)), Door });
	}

	Scored.Sort([](const FScored& L, const FScored& R) { return L.PathLen < R.PathLen; });

	const int32 Count = FMath::Min(MaxCount, Scored.Num());
	OutDoors.Reset(Count);
	for (int32 i = 0; i < Count; ++i)
		OutDoors.Add(Scored[i].Door);
}
