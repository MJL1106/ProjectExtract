// AnimNotify_EnemyGrenadeRelease — place at the hand-open frame of each grenade throw montage.
// When the notify fires, it resolves the owning AEnemyCharacter and calls ReleaseGrenade(),
// which spawns the projectile from the GrenadeSocket. The fallback telegraph timer is also cleared.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemyGrenadeRelease.generated.h"

UCLASS(meta = (DisplayName = "Enemy Grenade Release"))
class EXTRACTION_API UAnimNotify_EnemyGrenadeRelease : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Enemy Grenade Release"); }
};
