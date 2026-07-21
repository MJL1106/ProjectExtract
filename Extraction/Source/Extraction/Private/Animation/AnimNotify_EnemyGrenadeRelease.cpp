// AnimNotify_EnemyGrenadeRelease

#include "AnimNotify_EnemyGrenadeRelease.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnemyCharacter.h"
#include "EnemyGrenadierComponent.h"

void UAnimNotify_EnemyGrenadeRelease::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp)) return;

	AActor* Owner = MeshComp->GetOwner();
	if (!IsValid(Owner)) return;

	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Owner))
	{
		Enemy->ReleaseGrenade();
		return;
	}

	// Non-enemy thrower (companion) — release directly on the shared grenadier component.
	if (UEnemyGrenadierComponent* Grenadier = Owner->FindComponentByClass<UEnemyGrenadierComponent>())
		Grenadier->ReleaseGrenade();
}
