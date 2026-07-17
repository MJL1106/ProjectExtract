// Passive project-owned wrappers for first-person weapon and attachment art.

#include "Weapon/PlayerWeaponView.h"

#include "Components/SceneComponent.h"

namespace
{
	void ConfigurePassiveView(AActor& View)
	{
		View.PrimaryActorTick.bCanEverTick = false;
		View.SetReplicates(false);
		View.SetReplicateMovement(false);
		View.AutoReceiveInput = EAutoReceiveInput::Disabled;
		View.SetCanBeDamaged(false);
		View.SetActorEnableCollision(false);
	}
}

APlayerWeaponView::APlayerWeaponView()
{
	ConfigurePassiveView(*this);
	ViewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot"));
	SetRootComponent(ViewRoot);
}

APlayerWeaponAttachmentView::APlayerWeaponAttachmentView()
{
	ConfigurePassiveView(*this);
	ViewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot"));
	SetRootComponent(ViewRoot);
}
