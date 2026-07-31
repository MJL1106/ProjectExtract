// AITargetingStatics — shared sight-location helper for companion and enemy AI.

#include "AI/AITargetingStatics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Enemy/EnemyCharacter.h"
#include "Enemy/EnemyArchetypeData.h"
#include "Character/ExtractionPlayerInterface.h"
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

	// Cap on the pawn-skip loop in HasClearLineIgnoringPawns. A double takedown has at most a
	// handful of bodies in the way (victim, second enemy, player) — this is headroom, not a budget
	// meant to be hit; exceeding it means a pathological stack of pawns and is treated as blocked.
	constexpr int32 MaxPawnStepIterations = 6;

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

	bool GetVisibleBodyPoint(const AActor* Target, const FVector& ObserverEye, const AActor* IgnoreActor, FVector& OutPoint, bool bIncludeHead)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_VisibleBodyPoint);

		if (!IsValid(Target)) return false;

		UWorld* World = Target->GetWorld();
		if (!World) return false;

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AIBodyPointLoS), false);
		QueryParams.AddIgnoredActor(Target);
		if (IsValid(IgnoreActor)) QueryParams.AddIgnoredActor(IgnoreActor);

		// Build the low→high candidate ladder (head excluded by default —
		// only a head-only peek with neck still occluded stays undetected; neck and below register).
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

			// Head as last-resort candidate — only when the caller explicitly opts in.
			// Appended LAST so the existing low→high "return lowest visible" order is unchanged;
			// the head only fires when pelvis, chest, and neck are all blocked.
			if (bIncludeHead && Mesh->DoesSocketExist(HeadBoneName))
				Candidates.Add(Mesh->GetSocketLocation(HeadBoneName));
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

	bool ShouldIncludeHeadForObserver(const AActor* Observer, const AActor* Target)
	{
		if (!IsValid(Observer) || !IsValid(Target)) return false;

		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Observer);
		if (!Enemy) return false;

		const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
		if (!IsValid(DA) || !DA->bIsSniper) return false;

		const ACharacter* TargetChar = Cast<ACharacter>(Target);
		if (!TargetChar || TargetChar->bIsCrouched) return false;

		const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(Target);
		if (PlayerIface && PlayerIface->GetIsProne()) return false;

		return true;
	}

	bool HasClearLineIgnoringPawns(const UWorld* World, const FVector& Start, const FVector& End,
		FCollisionQueryParams QueryParams, AActor** OutBlocker)
	{
		if (!World) return false;

		for (int32 Iteration = 0; Iteration < MaxPawnStepIterations; ++Iteration)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams))
				return true;

			APawn* HitPawn = Cast<APawn>(Hit.GetActor());
			if (!HitPawn)
			{
				if (OutBlocker) *OutBlocker = Hit.GetActor();
				return false;
			}

			// Stepped over — join the local ignore set and re-trace from the same Start.
			QueryParams.AddIgnoredActor(HitPawn);
		}

		// Exceeded the pawn-skip cap: a pathological stack of bodies, not a real clear line.
		if (OutBlocker) *OutBlocker = nullptr;
		return false;
	}
}
