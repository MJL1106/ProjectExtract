// AExtracteeCompanion -- armed extraction VIP; see header.

#include "Extractee/ExtracteeCompanion.h"

#include "CompanionAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AIPerceptionSystem.h"
#include "TimerManager.h"
#include "WeaponBase.h"
#include "World/InteractionEventSubsystem.h"

AExtracteeCompanion::AExtracteeCompanion()
{
	bIsPrimaryCompanion = false;

	// Captive until rescued -- CompleteRescue spawns the AI controller. The BP child must keep
	// this Disabled too (BP CDO wins over the C++ default); BeginPlay heals a misconfiguration.
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AExtracteeCompanion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtracteeCompanion, bCaptive);
	DOREPLIFETIME(AExtracteeCompanion, bArmed);
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
	{
		World->GetTimerManager().ClearTimer(ArmTimerHandle);
		World->GetTimerManager().ClearTimer(ReplyLineTimerHandle);
		World->GetTimerManager().ClearTimer(WaveTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

float AExtracteeCompanion::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// Story prop until rescued, and still untouchable through the unarmed handoff window: the
	// team flips to player-side the instant he is freed, so without this the whole squad gets to
	// shoot an unarmed man who can't fight back for the length of the handoff line. Expressed as
	// the same predicate the enemy perception cloak reads, so immunity and cloak are one window by
	// construction and cannot silently split.
	if (IsAlwaysSightCloaked()) return 0.f;
	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

void AExtracteeCompanion::SetMode(ECompanionMode NewMode)
{
	// Not player-commandable -- pinned to Normal.
	if (NewMode != ECompanionMode::Normal) return;
	Super::SetMode(NewMode);
}

void AExtracteeCompanion::StartWeaponFire()
{
	if (!bArmed) return;
	Super::StartWeaponFire();
}

bool AExtracteeCompanion::CanFire() const
{
	return bArmed && Super::CanFire();
}

bool AExtracteeCompanion::IsCombatReady() const
{
	return bArmed && Super::IsCombatReady();
}

bool AExtracteeCompanion::IsAlwaysSightCloaked() const
{
	// Matches TakeDamage's immunity window exactly -- captive through the unarmed handoff. Enemies
	// must never lock onto a man who cannot fight back: zeroing the damage stopped him dying, but
	// the squad still picked the hostage as a target and emptied magazines into him.
	return !bArmed;
}

// ------------------------------------------------------------------
// Rescue
// ------------------------------------------------------------------

bool AExtracteeCompanion::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	return bCaptive && bRescueEnabled;
}

void AExtracteeCompanion::WorldInteract_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !bCaptive || !bRescueEnabled) return;
	CompleteRescue(/*bCeremony=*/true, Interactor);
}

FText AExtracteeCompanion::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return RescuePrompt;
}

float AExtracteeCompanion::GetWorldInteractHoldSeconds_Implementation(AActor* /*Interactor*/) const
{
	return RescueHoldSeconds;
}

void AExtracteeCompanion::OnWorldInteractHoldStarted_Implementation(AActor* /*Interactor*/)
{
	if (!HasAuthority() || !bCaptive || !bRescueEnabled) return;
	if (bRescueStartLineSpoken || !CompanionRescueStartLine) return;

	// Cutting the ropes takes RescueHoldSeconds; the primary companion talks the player through
	// it. A downed primary stays quiet — the hold still runs its full length.
	const ACompanionCharacter* Primary = GetPrimaryCompanion(GetWorld());
	if (!IsValid(Primary) || Primary->GetIsCompanionDBNO()) return;

	bRescueStartLineSpoken = true;
	Primary->SpeakScriptedLine(CompanionRescueStartLine);
}

void AExtracteeCompanion::ForceRescue()
{
	// Checkpoint fast-forward: no VO, no OnRescued -- the flow restarts the wave itself.
	CompleteRescue(/*bCeremony=*/false);
}

void AExtracteeCompanion::CompleteRescue(bool bCeremony, AActor* Interactor)
{
	if (!bCaptive) return;
	bCaptive = false;

	// Sight perception bakes listener-target affiliation at registration and never re-evaluates a
	// runtime team change. This pawn just flipped from NoTeam (captive) to team 0 (player side) —
	// evict and re-register so perceivers re-pair with the new affiliation.
	if (UWorld* World = GetWorld())
		if (UAIPerceptionSystem* PerceptionSys = UAIPerceptionSystem::GetCurrent(World))
		{
			PerceptionSys->UnregisterSource(*this, UAISense_Sight::StaticClass());
			PerceptionSys->RegisterSourceForSenseClass(UAISense_Sight::StaticClass(), *this);
		}

	// Only a real rescue is an interaction. Raised from this success branch rather than blanket
	// from the player's commit, so a refused hold (rescue gated off) ticks nothing off.
	if (bCeremony)
		if (UInteractionEventSubsystem* Events = GetWorld() ? GetWorld()->GetSubsystem<UInteractionEventSubsystem>() : nullptr)
			Events->NotifyWorldInteract(this, Interactor);

	// AI on -- the companion controller possesses and starts the shared behaviour tree. He stands
	// and follows from here, but UNARMED: the weapon stays hidden and the ABP's unarmed branch
	// runs until ArmWithPistol flips bArmed.
	if (!GetController())
		SpawnDefaultController();

	if (HealthWidgetComponent)
		HealthWidgetComponent->SetVisibility(HealthWidgetClass != nullptr);

	UE_LOG(LogCompanion, Log, TEXT("%s freed (%s) -- unarmed, awaiting pistol handoff"),
		*GetName(), bCeremony ? TEXT("interact") : TEXT("checkpoint fast-forward"));

	// Checkpoint resume lands mid-mission: no ceremony, no delay, straight to armed.
	if (!bCeremony)
	{
		ArmWithPistol(/*bCeremony=*/false);
		return;
	}

	// The primary companion offers the pistol; it changes hands as the line lands. A bleeding-out
	// primary doesn't cheerfully hand over hardware, and an unassigned line has no "ended" signal
	// to wait on — both arm immediately rather than stalling the wave forever.
	float HandoffDuration = 0.f;
	if (CompanionHandoffLine)
		if (const ACompanionCharacter* Primary = GetPrimaryCompanion(GetWorld()))
			if (!Primary->GetIsCompanionDBNO())
				HandoffDuration = Primary->SpeakScriptedLine(CompanionHandoffLine);

	if (HandoffDuration <= 0.f)
	{
		ArmWithPistol(/*bCeremony=*/true);
		return;
	}

	GetWorldTimerManager().SetTimer(ArmTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { ArmWithPistol(/*bCeremony=*/true); }),
		HandoffDuration, /*bLoop=*/false);
}

void AExtracteeCompanion::ArmWithPistol(bool bCeremony)
{
	if (bArmed) return;
	bArmed = true;

	if (AWeaponBase* Weapon = GetCurrentWeapon())
		Weapon->SetWeaponHidden(false);

	// Same weapon actor as at BeginPlay; re-baseline so ABP-specific cover align values take effect.
	RebaselineWeaponAlign();

	UE_LOG(LogCompanion, Log, TEXT("%s armed -- second companion active"), *GetName());

	if (!bCeremony) return;

	if (RescueReplyLine)
	{
		// ReplyLineDelay is an optional extra beat now that the reply chains off the handoff
		// line ending rather than racing it on a fixed clock.
		if (ReplyLineDelay > 0.f)
			GetWorldTimerManager().SetTimer(ReplyLineTimerHandle,
				FTimerDelegate::CreateWeakLambda(this, [this]() { SpeakScriptedLine(RescueReplyLine); }),
				ReplyLineDelay, /*bLoop=*/false);
		else
			SpeakScriptedLine(RescueReplyLine);
	}

	// OnRescued is the wave trigger — it fires off the pistol changing hands, not off the rescue.
	if (WaveDelayAfterArmed > 0.f)
		GetWorldTimerManager().SetTimer(WaveTimerHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { OnRescued.Broadcast(); }),
			WaveDelayAfterArmed, /*bLoop=*/false);
	else
		OnRescued.Broadcast();
}

void AExtracteeCompanion::RebaselineWeaponAlign()
{
	const USkeletalMeshComponent* BodyMesh = GetMesh();
	UCompanionAnimInstance* CompAnim = IsValid(BodyMesh)
		? Cast<UCompanionAnimInstance>(BodyMesh->GetAnimInstance())
		: nullptr;
	if (!IsValid(CompAnim))
	{
		// The exact designer mis-wire that reproduces the pistol drift (ABP not a
		// UCompanionAnimInstance) would otherwise fail invisibly here.
		UE_LOG(LogCompanion, Warning,
			TEXT("%s: RebaselineWeaponAlign -- ABP is not a UCompanionAnimInstance (class '%s'); "
				 "weapon align will not re-bake and the pistol drift this fixes will reproduce"),
			*GetName(), BodyMesh ? *GetNameSafe(BodyMesh->GetAnimInstance()) : TEXT("null"));
		return;
	}

	CompAnim->RequestWeaponAlignRebake();
}

FText AExtracteeCompanion::GetBleedoutFailReason() const
{
	return NSLOCTEXT("Extraction", "ExtracteeBledOut", "The VIP bled out.");
}
