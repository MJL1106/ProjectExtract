// ABarkTriggerVolume — level-placed dialogue trigger. The player walking through the box makes
// the companion speak a designer-authored sequence of VO lines through the one-voice bark
// channel, so scripted dialogue never talks over (or gets talked over by) systemic barks.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BarkTriggerVolume.generated.h"

class UBoxComponent;
class USoundBase;
class ACompanionCharacter;

USTRUCT(BlueprintType)
struct FScriptedDialogueLine
{
	GENERATED_BODY()

	/** VO for this step, played 3D at the companion. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TObjectPtr<USoundBase> Sound;

	/** Subtitle/debug text. No subtitle UI exists yet — stored for when one does. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	FText Subtitle;

	/** Silence before this line starts (after the previous line ends; for the first line, after
	 *  the player enters the box). */
	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (ClampMin = "0.0"))
	float PreDelaySeconds = 0.f;
};

UCLASS()
class EXTRACTION_API ABarkTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	ABarkTriggerVolume();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Waits out the line's PreDelay, then speaks it. */
	void ScheduleLine(int32 LineIndex);

	/** Speaks the line and chains the next one after this line's estimated duration. */
	void PlayLine(int32 LineIndex);

	UPROPERTY(VisibleAnywhere, Category = "Dialogue")
	TObjectPtr<UBoxComponent> TriggerBox;

	/** Lines spoken in order — a multi-entry array plays as one exchange. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TArray<FScriptedDialogueLine> Lines;

	/** Fire once per level load (default). Off = re-triggerable after RetriggerCooldownSeconds. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	bool bTriggerOnce = true;

	UPROPERTY(EditAnywhere, Category = "Dialogue", meta = (EditCondition = "!bTriggerOnce", ClampMin = "0.0"))
	float RetriggerCooldownSeconds = 60.f;

	bool bTriggered = false;
	float LastTriggerTime = 0.f;
	FTimerHandle LineTimerHandle;
	TWeakObjectPtr<ACompanionCharacter> Speaker;
};
