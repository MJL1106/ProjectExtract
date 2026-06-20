// AnimNotify_EnemyMeleeHit — drop at the weapon-contact frame of the enemy melee montage.
// When the notify fires, it resolves the owning AEnemyCharacter and calls ApplyMeleeDamage(),
// which delivers the committed strike's damage. No range re-check: pure timing.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemyMeleeHit.generated.h"

UCLASS(meta = (DisplayName = "Enemy Melee Hit"))
class EXTRACTION_API UAnimNotify_EnemyMeleeHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Enemy Melee Hit"); }
};
