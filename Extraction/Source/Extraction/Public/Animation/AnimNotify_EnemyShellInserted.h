// AnimNotify_EnemyShellInserted — place on the Loop section of the enemy body reload montage
// at the shell-seat frame. Resolves the owning AEnemyCharacter → current weapon →
// HandleShellInserted(), which ticks ammo +1 and decides whether to loop or end.
// Place this notify on the BODY montage only — the gun montage carries no notify to avoid double-counting.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemyShellInserted.generated.h"

UCLASS(meta = (DisplayName = "Enemy Shell Inserted"))
class EXTRACTION_API UAnimNotify_EnemyShellInserted : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Enemy Shell Inserted"); }
};
