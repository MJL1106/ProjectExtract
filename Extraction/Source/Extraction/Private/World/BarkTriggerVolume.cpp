#include "World/BarkTriggerVolume.h"
#include "Companion/CompanionCharacter.h"
#include "Components/BoxComponent.h"
#include "Enemy/BarkSubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	/** Durations beyond this are treated as a looping/procedural sound misreporting length —
	 *  the chain advances after the fallback beat instead of stalling for minutes. */
	constexpr float MaxTrustedLineDuration = 60.f;
	constexpr float FallbackLineDuration = 4.f;

	/** Beat between a line with no (or unimported) VO and the next, so text-only sequencing
	 *  still paces like speech. */
	constexpr float NoSoundLineDuration = 0.5f;
}

ABarkTriggerVolume::ABarkTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);
	TriggerBox->SetBoxExtent(FVector(200.f, 200.f, 100.f));
	TriggerBox->SetCollisionProfileName(TEXT("OverlapOnlyPawn"));
	TriggerBox->SetGenerateOverlapEvents(true);
}

void ABarkTriggerVolume::BeginPlay()
{
	Super::BeginPlay();
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ABarkTriggerVolume::HandleOverlapBegin);
}

void ABarkTriggerVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LineTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void ABarkTriggerVolume::HandleOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn) || !Pawn->IsPlayerControlled()) return;

	UWorld* World = GetWorld();
	if (!IsValid(World) || Lines.Num() == 0) return;

	const float Now = World->GetTimeSeconds();
	if (bTriggered && (bTriggerOnce || Now - LastTriggerTime < RetriggerCooldownSeconds)) return;

	// Scripted story lines belong to the primary companion's voice only.
	ACompanionCharacter* Companion = ACompanionCharacter::GetPrimaryCompanion(World);
	if (!IsValid(Companion) || Companion->GetIsCompanionDBNO()) return;

	// Mid-fight or mid-another-exchange, skip WITHOUT consuming the trigger — flavor dialogue
	// interleaving with combat telegraphs (or another box's sequence) reads as broken VO. The box
	// fires cleanly the next time the player crosses it.
	if (Companion->GetPosture() == ECompanionPosture::Combat) return;
	if (const UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>())
		if (Barks->IsScriptedLineActive()) return;

	Speaker = Companion;
	bTriggered = true;
	LastTriggerTime = Now;
	ScheduleLine(0);
}

void ABarkTriggerVolume::ScheduleLine(int32 LineIndex)
{
	if (!Lines.IsValidIndex(LineIndex)) return;

	const float Delay = Lines[LineIndex].PreDelaySeconds;
	if (Delay <= 0.f)
	{
		PlayLine(LineIndex);
		return;
	}

	GetWorldTimerManager().SetTimer(LineTimerHandle,
		FTimerDelegate::CreateUObject(this, &ABarkTriggerVolume::PlayLine, LineIndex), Delay, false);
}

void ABarkTriggerVolume::PlayLine(int32 LineIndex)
{
	if (!Lines.IsValidIndex(LineIndex)) return;

	ACompanionCharacter* Companion = Speaker.Get();
	if (!IsValid(Companion) || Companion->GetIsCompanionDBNO()) return;

	// A fight that starts mid-exchange wins: stop the chain rather than have flavor VO fade out
	// a priority combat telegraph (RequestScriptedLine unconditionally interrupts the live voice).
	if (Companion->GetPosture() == ECompanionPosture::Combat) return;

	const FScriptedDialogueLine& Line = Lines[LineIndex];
	float Duration = NoSoundLineDuration;
	if (IsValid(Line.Sound))
	{
		Companion->SpeakScriptedLine(Line.Sound);
		const float SoundDuration = Line.Sound->GetDuration();
		Duration = (SoundDuration > 0.f && SoundDuration < MaxTrustedLineDuration)
			? SoundDuration : FallbackLineDuration;
	}

	if (!Lines.IsValidIndex(LineIndex + 1)) return;

	GetWorldTimerManager().SetTimer(LineTimerHandle,
		FTimerDelegate::CreateUObject(this, &ABarkTriggerVolume::ScheduleLine, LineIndex + 1), Duration, false);
}
