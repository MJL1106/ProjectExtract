// Companion bark identity — kept separate from the enemy EBarkType so shared-name calls
// (grenade, flanking, suppressing) never dedup against the enemy's in the bark subsystem.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/BarkSetData.h"
#include "CompanionBarkTypes.generated.h"

UENUM(BlueprintType)
enum class ECompanionBarkType : uint8
{
	// Group 1 — contact & combat callouts
	ContactCombat		UMETA(DisplayName = "Contact (Combat)"),
	EnemyDirection		UMETA(DisplayName = "Enemy Direction"),
	EnemyArchetype		UMETA(DisplayName = "Enemy Archetype"),
	TargetDown			UMETA(DisplayName = "Target Down"),
	MultipleContacts	UMETA(DisplayName = "Multiple Contacts"),
	LostContact			UMETA(DisplayName = "Lost Contact"),

	// Group 2 — companion's own actions
	Reloading			UMETA(DisplayName = "Reloading"),
	LowAmmo				UMETA(DisplayName = "Low Ammo"),
	Repositioning		UMETA(DisplayName = "Repositioning"),
	TakingCover			UMETA(DisplayName = "Taking Cover"),
	ThrowingGrenade		UMETA(DisplayName = "Throwing Grenade"),
	Suppressing			UMETA(DisplayName = "Suppressing"),
	Flanking			UMETA(DisplayName = "Flanking"),
	PushingUp			UMETA(DisplayName = "Pushing Up"),

	// Group 3 — support & relationship
	PlayerDownReaction	UMETA(DisplayName = "Player Down Reaction"),
	RevivingPlayer		UMETA(DisplayName = "Reviving Player"),
	PlayerRevived		UMETA(DisplayName = "Player Revived"),
	CompanionDown		UMETA(DisplayName = "Companion Down"),
	CompanionCallForHelp UMETA(DisplayName = "Companion Call For Help"),
	CompanionRevived	UMETA(DisplayName = "Companion Revived"),
	CompanionHurt		UMETA(DisplayName = "Companion Hurt"),
	Reassurance			UMETA(DisplayName = "Reassurance"),

	// Group 4 — stealth
	GoingStealth		UMETA(DisplayName = "Going Stealth"),
	StealthSpotEnemy	UMETA(DisplayName = "Stealth Spot Enemy"),
	TakedownConfirm		UMETA(DisplayName = "Takedown Confirm"),
	HoldForPatrol		UMETA(DisplayName = "Hold For Patrol"),
	StealthBroken		UMETA(DisplayName = "Stealth Broken"),

	// Group 5 — command acknowledgments
	AckBreach			UMETA(DisplayName = "Ack Breach"),
	Breaching			UMETA(DisplayName = "Breaching"),
	AckTakedown			UMETA(DisplayName = "Ack Takedown"),
	AckLoot				UMETA(DisplayName = "Ack Loot"),
	AckExplore			UMETA(DisplayName = "Ack Explore"),
	AckGeneric			UMETA(DisplayName = "Ack Generic"),

	// Group 6 — navigation & follow
	FallingBehind		UMETA(DisplayName = "Falling Behind"),
	Blocked				UMETA(DisplayName = "Blocked"),
	Regroup				UMETA(DisplayName = "Regroup"),
	Following			UMETA(DisplayName = "Following"),

	// Groups 7/8 — world & personality
	ObjectiveSpotted	UMETA(DisplayName = "Objective Spotted"),
	ExtractionCallout	UMETA(DisplayName = "Extraction Callout"),
	HeatRising			UMETA(DisplayName = "Heat Rising"),
	AreaClear			UMETA(DisplayName = "Area Clear"),
	PostFightBanter		UMETA(DisplayName = "Post Fight Banter"),
	ApprovePlayerKill	UMETA(DisplayName = "Approve Player Kill"),
	PlayerHurtWarning	UMETA(DisplayName = "Player Hurt Warning"),
	IdleAmbient			UMETA(DisplayName = "Idle Ambient"),
};

UCLASS(BlueprintType)
class EXTRACTION_API UCompanionBarkSetData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Barks")
	TMap<ECompanionBarkType, FBarkDefinition> Barks;

	/** Distance attenuation for the companion voice — gentler than the enemy falloff (he is
	 *  almost always near the player). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Barks")
	TObjectPtr<USoundAttenuation> Attenuation;
};
