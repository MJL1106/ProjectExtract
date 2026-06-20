// AnimNotify_EnemyShellInserted

#include "AnimNotify_EnemyShellInserted.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnemyCharacter.h"
#include "Weapon/WeaponBase.h"

void UAnimNotify_EnemyShellInserted::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp)) return;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(MeshComp->GetOwner());
	if (!IsValid(Enemy)) return;

	if (AWeaponBase* Weapon = Enemy->GetCurrentWeapon())
		Weapon->HandleShellInserted();
}
