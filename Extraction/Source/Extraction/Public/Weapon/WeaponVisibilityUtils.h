// Symmetric hide/show utilities for weapon actor trees.
// Captures per-descendant hidden state on hide; restores exactly that set on show,
// preventing force-show of descendants that were legitimately hidden by the owning BP
// (e.g. variable-driven attachment slots on kit weapons).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

namespace WeaponVisibilityUtils
{
	/**
	 * Hides RootActor and all attached descendants. Records which descendants were ALREADY
	 * hidden into OutSnapshot so a later RestoreActorTree call skips re-showing them.
	 * Additive: caller must Reset() the set before a batch of hide calls when multiple
	 * roots share one snapshot.
	 */
	inline void SnapshotAndHideActorTree(AActor* RootActor, TSet<TWeakObjectPtr<AActor>>& OutSnapshot)
	{
		if (!IsValid(RootActor)) return;

		TArray<AActor*> Descendants;
		RootActor->GetAttachedActors(Descendants, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);

		for (AActor* Desc : Descendants)
		{
			if (IsValid(Desc) && Desc->IsHidden())
				OutSnapshot.Add(Desc);
		}

		RootActor->SetActorHiddenInGame(true);
		for (AActor* Desc : Descendants)
		{
			if (IsValid(Desc))
				Desc->SetActorHiddenInGame(true);
		}
	}

	/**
	 * Shows RootActor and all attached descendants that are NOT in PreviouslyHidden.
	 * Descendants captured as already-hidden by a prior SnapshotAndHideActorTree stay hidden.
	 */
	inline void RestoreActorTree(AActor* RootActor, const TSet<TWeakObjectPtr<AActor>>& PreviouslyHidden)
	{
		if (!IsValid(RootActor)) return;

		RootActor->SetActorHiddenInGame(false);

		TArray<AActor*> Descendants;
		RootActor->GetAttachedActors(Descendants, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);

		for (AActor* Desc : Descendants)
		{
			if (IsValid(Desc) && !PreviouslyHidden.Contains(Desc))
				Desc->SetActorHiddenInGame(false);
		}
	}
}
