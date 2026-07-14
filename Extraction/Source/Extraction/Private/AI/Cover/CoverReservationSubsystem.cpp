// CoverReservationSubsystem — post-vacate cooldown + intended-occupancy tracking.

#include "CoverReservationSubsystem.h"
#include "CoverSystem.h"
#include "Engine/World.h"

void UCoverReservationSubsystem::Deinitialize()
{
	VacateMap.Empty();
	IntendedMap.Empty();
	IntendedHandleRefCount.Empty();
	Super::Deinitialize();
}

// --- Post-vacate cooldown ---

void UCoverReservationSubsystem::MarkVacated(const FCoverHandle& Handle, AController* Controller)
{
	if (!Handle.IsValid() || !IsValid(Controller)) return;

	const UWorld* World = GetWorld();
	if (!World) return;

	FVacateStamp Stamp;
	Stamp.Vacater = Controller;
	Stamp.VacateTime = World->GetTimeSeconds();

	TArray<FVacateStamp>& Stamps = VacateMap.FindOrAdd(Handle);

	// Replace existing stamp from the same controller, or append.
	bool bFound = false;
	for (FVacateStamp& Existing : Stamps)
	{
		if (Existing.Vacater.Get() == Controller)
		{
			Existing.VacateTime = Stamp.VacateTime;
			bFound = true;
			break;
		}
	}
	if (!bFound) Stamps.Add(Stamp);

	PruneStaleVacateEntries();
}

bool UCoverReservationSubsystem::IsOnPostVacateCooldown(const FCoverHandle& Handle, const AController* Controller, float CooldownSeconds) const
{
	if (!Handle.IsValid() || !IsValid(Controller) || CooldownSeconds <= 0.f) return false;

	const TArray<FVacateStamp>* Stamps = VacateMap.Find(Handle);
	if (!Stamps) return false;

	const UWorld* World = GetWorld();
	if (!World) return false;

	const double Now = World->GetTimeSeconds();
	for (const FVacateStamp& Stamp : *Stamps)
	{
		if (Stamp.Vacater.Get() != Controller) continue;
		if ((Now - Stamp.VacateTime) < static_cast<double>(CooldownSeconds)) return true;
	}
	return false;
}

void UCoverReservationSubsystem::PruneStaleVacateEntries()
{
	const UWorld* World = GetWorld();
	if (!World) return;

	const double Now = World->GetTimeSeconds();

	// Throttle prune scans to avoid per-call map iteration
	if ((Now - LastPruneTime) < PruneThrottleInterval) return;
	LastPruneTime = Now;

	for (auto It = VacateMap.CreateIterator(); It; ++It)
	{
		TArray<FVacateStamp>& Stamps = It->Value;
		Stamps.RemoveAll([Now](const FVacateStamp& S)
		{
			return (Now - S.VacateTime) > MaxVacateStaleness || !S.Vacater.IsValid();
		});
		if (Stamps.Num() == 0) It.RemoveCurrent();
	}
}

// --- Intended occupancy ---

void UCoverReservationSubsystem::SetIntendedCover(AController* Controller, const FCoverHandle& Handle)
{
	if (!IsValid(Controller) || !Handle.IsValid()) return;

	// Fix 8: decrement reverse-index for the old handle if this controller already had one.
	if (const FCoverHandle* OldHandle = IntendedMap.Find(Controller))
	{
		if (OldHandle->IsValid())
		{
			int32& OldRef = IntendedHandleRefCount.FindOrAdd(*OldHandle, 0);
			--OldRef;
			if (OldRef <= 0) IntendedHandleRefCount.Remove(*OldHandle);
		}
	}

	IntendedMap.Add(Controller, Handle);

	// Fix 8: increment reverse-index for the new handle.
	++IntendedHandleRefCount.FindOrAdd(Handle, 0);

	// Opportunistically prune dead-controller entries (also keeps reverse index consistent).
	for (auto It = IntendedMap.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			// Fix 8: decrement reverse-index for the stale controller's handle.
			if (It->Value.IsValid())
			{
				int32& Ref = IntendedHandleRefCount.FindOrAdd(It->Value, 0);
				--Ref;
				if (Ref <= 0) IntendedHandleRefCount.Remove(It->Value);
			}
			It.RemoveCurrent();
		}
	}
}

void UCoverReservationSubsystem::ClearIntendedCover(AController* Controller)
{
	if (!IsValid(Controller)) return;

	// Fix 8: decrement reverse-index before removing.
	if (const FCoverHandle* OldHandle = IntendedMap.Find(Controller))
	{
		if (OldHandle->IsValid())
		{
			int32& Ref = IntendedHandleRefCount.FindOrAdd(*OldHandle, 0);
			--Ref;
			if (Ref <= 0) IntendedHandleRefCount.Remove(*OldHandle);
		}
	}

	IntendedMap.Remove(Controller);
}

bool UCoverReservationSubsystem::IsCoverIntendedByOther(const FCoverHandle& Handle, const AController* IgnoreController) const
{
	if (!Handle.IsValid()) return false;

	// Fix 8: O(1) via reverse-index instead of O(N) scan.
	const int32* RefCount = IntendedHandleRefCount.Find(Handle);
	if (!RefCount || *RefCount <= 0) return false;

	// If IgnoreController itself intends this handle, the ref-count includes it.
	// Check whether IgnoreController is the only intender.
	if (IgnoreController)
	{
		const FCoverHandle* IgnoredHandle = IntendedMap.Find(const_cast<AController*>(IgnoreController));
		if (IgnoredHandle && *IgnoredHandle == Handle)
			return *RefCount > 1;  // other controllers also intend it
	}

	return true;
}

bool UCoverReservationSubsystem::HasIntendedCover(const AController* Controller) const
{
	if (!IsValid(Controller)) return false;
	const FCoverHandle* Handle = IntendedMap.Find(const_cast<AController*>(Controller));
	return Handle && Handle->IsValid();
}

bool UCoverReservationSubsystem::GetIntendedCover(const AController* Controller, FCoverHandle& OutHandle) const
{
	if (!IsValid(Controller)) return false;
	const FCoverHandle* Handle = IntendedMap.Find(const_cast<AController*>(Controller));
	if (!Handle || !Handle->IsValid()) return false;
	OutHandle = *Handle;
	return true;
}

void UCoverReservationSubsystem::GetIntendedCovers(TArray<FCover>& Out, const AController* ExcludeController,
	const UClass* RequiredControllerClass)
{
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(GetWorld());
	if (!CoverSys) return;

	Out.Reserve(IntendedMap.Num());

	for (auto It = IntendedMap.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			// Fix 8: decrement reverse-index for pruned stale controller.
			if (It->Value.IsValid())
			{
				int32& Ref = IntendedHandleRefCount.FindOrAdd(It->Value, 0);
				--Ref;
				if (Ref <= 0) IntendedHandleRefCount.Remove(It->Value);
			}
			It.RemoveCurrent();
			continue;
		}

		const AController* Ctrl = It->Key.Get();
		if (Ctrl == ExcludeController) continue;
		if (RequiredControllerClass && !Ctrl->IsA(RequiredControllerClass)) continue;

		FCoverData Data;
		if (CoverSys->GetCoverData(It->Value, Data))
		{
			Out.Emplace(It->Value, Data);
		}
	}
}

void UCoverReservationSubsystem::GetIntendedCoverOwners(TArray<TPair<APawn*, FCover>>& Out,
	const AController* ExcludeController)
{
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(GetWorld());
	if (!CoverSys) return;

	Out.Reserve(IntendedMap.Num());

	for (auto It = IntendedMap.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			// Mirror GetIntendedCovers: prune stale controller + decrement reverse-index.
			if (It->Value.IsValid())
			{
				int32& Ref = IntendedHandleRefCount.FindOrAdd(It->Value, 0);
				--Ref;
				if (Ref <= 0) IntendedHandleRefCount.Remove(It->Value);
			}
			It.RemoveCurrent();
			continue;
		}

		AController* Ctrl = It->Key.Get();
		if (Ctrl == ExcludeController) continue;

		FCoverData Data;
		if (CoverSys->GetCoverData(It->Value, Data))
		{
			Out.Emplace(Ctrl->GetPawn(), FCover(It->Value, Data));
		}
	}
}
