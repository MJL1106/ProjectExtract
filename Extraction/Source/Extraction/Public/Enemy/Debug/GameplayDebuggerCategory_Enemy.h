// Gameplay Debugger category for enemy AI — displays awareness, suppression, morale, squad, and archetype state.

#pragma once

#if WITH_GAMEPLAY_DEBUGGER

#include "CoreMinimal.h"
#include "GameplayDebuggerCategory.h"

class FGameplayDebuggerCategory_Enemy : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_Enemy();

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) override;

private:

	struct FRepData
	{
		// Identity
		FString PawnName;
		FString ArchetypeName;

		// Awareness
		FString AwarenessState;
		float Suspicion = 0.f;
		bool bInCombat = false;
		bool bAnyHostileSighted = false;
		FString CombatTargetName;
		FVector LastKnownLocation = FVector::ZeroVector;

		// Blackboard (LOS/InRange only valid when bInCombat — written by combat-only BTService)
		bool bHasLineOfSight = false;
		bool bTargetInRange = false;
		bool bHasCover = false;
		FString CoverSlotName;
		FString MoraleStateBB;
		FString ManeuverRole;

		// BT
		FString ActiveTaskName;

		// Suppression
		float SuppressionValue = 0.f;
		bool bIsSuppressed = false;

		// Morale
		FString MoraleState;
		float Morale01 = 1.f;

		// Squad
		FString SquadId;
		int32 SquadAlive = 0;

		// Archetype-specific
		FString ArchetypeExtra;

		// Validity
		bool bHasValidEnemy = false;

		void Serialize(FArchive& Ar);
	};

	FRepData DataPack;
};

#endif // WITH_GAMEPLAY_DEBUGGER
