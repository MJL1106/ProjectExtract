// AExtracteeCompanion -- armed extraction VIP; see header.

#include "Extractee/ExtracteeCompanion.h"

#include "Components/WidgetComponent.h"
#include "TimerManager.h"
#include "WeaponBase.h"

AExtracteeCompanion::AExtracteeCompanion()
{
	bIsPrimaryCompanion = false;

	// Captive until rescued -- CompleteRescue spawns the AI controller. The BP child must keep
	// this Disabled too (BP CDO wins over the C++ default); BeginPlay heals a misconfiguration.
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AExtracteeCompanion::BeginPlay()
{
	Super::BeginPlay();

	// Never show a command-mode ring over a companion the player can't command — regardless of
	// captive state (a BP child duplicated from BP_Companion inherits its ModeWidgetClass).
	if (ModeWidgetComponent)
		ModeWidgetComponent->SetVisibility(false);

	if (!bCaptive) return;

	if (GetController())
	{
		UE_LOG(LogCompanion, Warning,
			TEXT("%s possessed while captive (keep AutoPossessAI = Disabled on the BP child) -- detaching"),
			*GetName());
		DetachFromControllerPendingDestroy();
	}

	// The base BeginPlay spawned and attached the weapon -- keep it out of sight until the handoff.
	if (AWeaponBase* Weapon = GetCurrentWeapon())
		Weapon->SetWeaponHidden(true);

	// No health bar over a hostage; CompleteRescue restores it.
	if (HealthWidgetComponent)
		HealthWidgetComponent->SetVisibility(false);
}

void AExtracteeCompanion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ReplyLineTimerHandle);

	Super::EndPlay(EndPlayReason);
}

float AExtracteeCompanion::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// Story prop until rescued -- no damage, no suppression/attacker stamps.
	if (bCaptive) return 0.f;
	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

void AExtracteeCompanion::SetMode(ECompanionMode NewMode)
{
	// Not player-commandable -- pinned to Normal.
	if (NewMode != ECompanionMode::Normal) return;
	Super::SetMode(NewMode);
}

// ------------------------------------------------------------------
// Rescue
// ------------------------------------------------------------------

bool AExtracteeCompanion::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	return bCaptive && bRescueEnabled;
}

void AExtracteeCompanion::WorldInteract_Implementation(AActor* /*Interactor*/)
{
	if (!HasAuthority() || !bCaptive || !bRescueEnabled) return;
	CompleteRescue(/*bCeremony=*/true);
}

FText AExtracteeCompanion::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return RescuePrompt;
}

void AExtracteeCompanion::ForceRescue()
{
	// Checkpoint fast-forward: no VO, no OnRescued -- the flow restarts the wave itself.
	CompleteRescue(/*bCeremony=*/false);
}

void AExtracteeCompanion::CompleteRescue(bool bCeremony)
{
	if (!bCaptive) return;
	bCaptive = false;

	// AI on -- the companion controller possesses and starts the shared behaviour tree.
	if (!GetController())
		SpawnDefaultController();

	if (AWeaponBase* Weapon = GetCurrentWeapon())
		Weapon->SetWeaponHidden(false);

	if (HealthWidgetComponent)
		HealthWidgetComponent->SetVisibility(HealthWidgetClass != nullptr);

	UE_LOG(LogCompanion, Log, TEXT("%s rescued (%s) -- second companion active"),
		*GetName(), bCeremony ? TEXT("interact") : TEXT("checkpoint fast-forward"));

	if (!bCeremony) return;

	// Handoff exchange: the primary companion offers the pistol, the VIP replies after a beat.
	// A bleeding-out primary doesn't cheerfully hand over hardware — skip its line, keep the reply.
	if (CompanionHandoffLine)
		if (const ACompanionCharacter* Primary = GetPrimaryCompanion(GetWorld()))
			if (!Primary->GetIsCompanionDBNO())
				Primary->SpeakScriptedLine(CompanionHandoffLine);

	if (RescueReplyLine)
	{
		GetWorldTimerManager().SetTimer(ReplyLineTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				SpeakScriptedLine(RescueReplyLine);
			}),
			FMath::Max(0.1f, ReplyLineDelay), /*bLoop=*/false);
	}

	OnRescued.Broadcast();
}

FText AExtracteeCompanion::GetBleedoutFailReason() const
{
	return NSLOCTEXT("Extraction", "ExtracteeBledOut", "The VIP bled out.");
}
