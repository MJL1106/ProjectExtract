// AnimNotify_EnemyGrenadeRelease

#include "AnimNotify_EnemyGrenadeRelease.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnemyCharacter.h"

void UAnimNotify_EnemyGrenadeRelease::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp)) return;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(MeshComp->GetOwner());
	if (!IsValid(Enemy)) return;

	Enemy->ReleaseGrenade();
}
