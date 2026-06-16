// AITargetingStatics — shared sight-location helper for companion and enemy AI.

#include "AI/AITargetingStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace AITargeting
{
	static const FName HeadBoneName(TEXT("head"));
	static const FName PelvisBoneName(TEXT("pelvis"));
	static const FName ChestBoneName(TEXT("spine_03"));
	static const FName NeckBoneName(TEXT("neck_01"));

	FVector GetSightLocation(const AActor* Target)
	{
		if (!IsValid(Target)) return FVector::ZeroVector;

		// Prefer the head bone — first point to clear cover when an enemy stands up.
		// Skip when physics simulation is active (ragdoll death frame) so the point
		// doesn't chase a flailing corpse bone.
		if (const ACharacter* Char = Cast<ACharacter>(Target))
		{
			if (const USkeletalMeshComponent* Mesh = Char->GetMesh())
			{
				if (!Mesh->IsSimulatingPhysics() && Mesh->DoesSocketExist(HeadBoneName))
					return Mesh->GetSocketLocation(HeadBoneName);
			}
		}

		// Pawn eye height — mannequins without a "head" socket still beat actor centre.
		if (const APawn* Pawn = Cast<APawn>(Target))
			return Pawn->GetPawnViewLocation();

		return Target->GetActorLocation();
	}

	bool GetVisibleBodyPoint(const AActor* Target, const FVector& ObserverEye, const AActor* IgnoreActor, FVector& OutPoint)
	{
		if (!IsValid(Target)) return false;

		UWorld* World = Target->GetWorld();
		if (!World) return false;

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AIBodyPointLoS), false);
		QueryParams.AddIgnoredActor(Target);
		if (IsValid(IgnoreActor)) QueryParams.AddIgnoredActor(IgnoreActor);

		// Build the low→high candidate ladder (head intentionally excluded).
		// Actor centre is the guaranteed fallback when pelvis socket is missing.
		TArray<FVector, TInlineAllocator<4>> Candidates;
		Candidates.Reserve(4);

		const ACharacter* Char = Cast<ACharacter>(Target);
		const USkeletalMeshComponent* Mesh = Char ? Char->GetMesh() : nullptr;
		const bool bMeshValid = IsValid(Mesh) && !Mesh->IsSimulatingPhysics();

		// Always add actor centre as the base fallback (covers ragdolls and non-Character actors).
		Candidates.Add(Target->GetActorLocation());

		if (bMeshValid)
		{
			// Insert in low→high order so we return the lowest visible point.
			// Actor centre already covers the pelvis-missing case; only add socket locations on top of it.
			if (Mesh->DoesSocketExist(PelvisBoneName))
				Candidates[0] = Mesh->GetSocketLocation(PelvisBoneName);

			if (Mesh->DoesSocketExist(ChestBoneName))
				Candidates.Add(Mesh->GetSocketLocation(ChestBoneName));

			if (Mesh->DoesSocketExist(NeckBoneName))
				Candidates.Add(Mesh->GetSocketLocation(NeckBoneName));
		}

		for (const FVector& Candidate : Candidates)
		{
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, ObserverEye, Candidate, ECC_Visibility, QueryParams);
			if (!bBlocked || Hit.GetActor() == Target)
			{
				OutPoint = Candidate;
				return true;
			}
		}

		// Nothing visible — write a defined fallback so callers never read an uninitialised out-param.
		OutPoint = Target->GetActorLocation();
		return false;
	}
}
