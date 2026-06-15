// AITargetingStatics — shared sight-location helper for companion and enemy AI.

#include "AI/AITargetingStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"

namespace AITargeting
{
	static const FName HeadBoneName(TEXT("head"));

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
}
