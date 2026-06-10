# Class Templates

Complete file templates for each class type. Claude should adapt these to the specific class being generated.

## Actor Template

### Header
```cpp
// Brief purpose (1 line)
// Used by: SystemA, SystemB

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyActor.generated.h"

class UBoxComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogMySystem, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMyEvent, float, Value);

UCLASS(Blueprintable)
class MYPROJECT_API AMyActor : public AActor
{
    GENERATED_BODY()

public:
    AMyActor();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // --- Components ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UBoxComponent> BoxComponent;

    // --- Config ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config", meta = (ClampMin = "0.0"))
    float MaxValue = 100.f;

    // --- State ---
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "State")
    bool bIsActive = false;

    // --- Events ---
    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnMyEvent OnMyEvent;

private:
    FTimerHandle MyTimerHandle;
};
```

### Source
```cpp
#include "MyActor.h"
#include "Components/BoxComponent.h"

DEFINE_LOG_CATEGORY(LogMySystem);

AMyActor::AMyActor()
{
    PrimaryActorTick.bCanEverTick = false;

    BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxComponent"));
    RootComponent = BoxComponent;
}

void AMyActor::BeginPlay()
{
    Super::BeginPlay();
}

void AMyActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(MyTimerHandle);
    Super::EndPlay(EndPlayReason);
}
```

## ActorComponent Template

### Header
```cpp
// Brief purpose (1 line)
// Used by: ActorA, ActorB

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MyComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMyComponent, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeath);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MYPROJECT_API UMyComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMyComponent();

    // --- Public API ---
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ApplyDamage(float DamageAmount);

    UFUNCTION(BlueprintCallable, Category = "Combat")
    void Heal(float HealAmount);

    UFUNCTION(BlueprintPure, Category = "Combat")
    float GetHealthPercent() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // --- Config ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config", meta = (ClampMin = "0.0"))
    float MaxHealth = 100.f;

    // --- State ---
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "State")
    float CurrentHealth = 0.f;

    // --- Events ---
    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnHealthChanged OnHealthChanged;

    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnDeath OnDeath;
};
```

### Source
```cpp
#include "MyComponent.h"

DEFINE_LOG_CATEGORY(LogMyComponent);

UMyComponent::UMyComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UMyComponent::BeginPlay()
{
    Super::BeginPlay();
    CurrentHealth = MaxHealth;
}

void UMyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // No timers or delegate bindings to clean up in this example
    Super::EndPlay(EndPlayReason);
}

void UMyComponent::ApplyDamage(const float DamageAmount)
{
    if (DamageAmount <= 0.f) return;

    const float OldHealth = CurrentHealth;
    CurrentHealth = FMath::Clamp(CurrentHealth - DamageAmount, 0.f, MaxHealth);
    const float Delta = CurrentHealth - OldHealth;

    OnHealthChanged.Broadcast(CurrentHealth, Delta);

    if (CurrentHealth <= 0.f)
    {
        OnDeath.Broadcast();
    }
}

void UMyComponent::Heal(const float HealAmount)
{
    if (HealAmount <= 0.f) return;

    const float OldHealth = CurrentHealth;
    CurrentHealth = FMath::Clamp(CurrentHealth + HealAmount, 0.f, MaxHealth);
    const float Delta = CurrentHealth - OldHealth;

    if (Delta > 0.f)
    {
        OnHealthChanged.Broadcast(CurrentHealth, Delta);
    }
}

float UMyComponent::GetHealthPercent() const
{
    return MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f;
}
```

## Character with Enhanced Input Template

### Header
```cpp
// Player-controlled FPS character
// Used by: PlayerController, HUD, GameMode

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MyCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UCameraComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogMyCharacter, Log, All);

UCLASS(Blueprintable)
class MYPROJECT_API AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMyCharacter();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // --- Components ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    // --- Input Config ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> FireAction;

private:
    void HandleMove(const FInputActionValue& Value);
    void HandleLook(const FInputActionValue& Value);
    void HandleFire(const FInputActionValue& Value);
};
```

### Source
```cpp
#include "MyCharacter.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

DEFINE_LOG_CATEGORY(LogMyCharacter);

AMyCharacter::AMyCharacter()
{
    PrimaryActorTick.bCanEverTick = false;

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetMesh(), FName("head"));
    FirstPersonCamera->bUsePawnControlRotation = true;
}

void AMyCharacter::BeginPlay()
{
    Super::BeginPlay();

    // Add Input Mapping Context
    if (const auto* PC = Cast<APlayerController>(GetController()))
    {
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
        else
        {
            UE_LOG(LogMyCharacter, Warning, TEXT("%s: Failed to get EnhancedInput subsystem"), *GetName());
        }
    }
}

void AMyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
}

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (auto* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (IsValid(MoveAction))
        {
            EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMyCharacter::HandleMove);
        }
        if (IsValid(LookAction))
        {
            EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMyCharacter::HandleLook);
        }
        if (IsValid(FireAction))
        {
            EnhancedInput->BindAction(FireAction, ETriggerEvent::Triggered, this, &AMyCharacter::HandleFire);
        }
    }
    else
    {
        UE_LOG(LogMyCharacter, Error, TEXT("%s: PlayerInputComponent is not UEnhancedInputComponent"), *GetName());
    }
}

void AMyCharacter::HandleMove(const FInputActionValue& Value)
{
    const FVector2D Input = Value.Get<FVector2D>();
    const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);

    AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Input.Y);
    AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Input.X);
}

void AMyCharacter::HandleLook(const FInputActionValue& Value)
{
    const FVector2D Input = Value.Get<FVector2D>();
    AddControllerYawInput(Input.X);
    AddControllerPitchInput(Input.Y);
}

void AMyCharacter::HandleFire(const FInputActionValue& Value)
{
    // Implement weapon firing
}
```

## AI Controller Template

### Header
```cpp
// Controls enemy AI behavior
// Used by: EnemyCharacter, GameMode (spawning)

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MyAIController.generated.h"

class UBehaviorTree;
class UBlackboardData;

DECLARE_LOG_CATEGORY_EXTERN(LogMyAI, Log, All);

UCLASS()
class MYPROJECT_API AMyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AMyAIController();

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    // --- Config ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    TObjectPtr<UBlackboardData> BlackboardAsset;
};
```

### Source
```cpp
#include "MyAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

DEFINE_LOG_CATEGORY(LogMyAI);

AMyAIController::AMyAIController()
{
}

void AMyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (!IsValid(BehaviorTreeAsset))
    {
        UE_LOG(LogMyAI, Error, TEXT("%s: BehaviorTreeAsset is null"), *GetName());
        return;
    }

    if (IsValid(BlackboardAsset))
    {
        UseBlackboard(BlackboardAsset, Blackboard);
    }
    else
    {
        UE_LOG(LogMyAI, Error, TEXT("%s: BlackboardAsset is null"), *GetName());
        return;
    }

    RunBehaviorTree(BehaviorTreeAsset);
}

void AMyAIController::OnUnPossess()
{
    Super::OnUnPossess();

    if (auto* BTComp = GetBrainComponent())
    {
        BTComp->StopLogic(TEXT("Unpossessed"));
    }
}
```

## Widget Template

### Header
```cpp
// HUD overlay for player health and ammo
// Used by: PlayerController, HUD

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MyWidget.generated.h"

class UTextBlock;
class UProgressBar;

UCLASS(Blueprintable)
class MYPROJECT_API UMyWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // --- Bound Widgets (must exist in Blueprint) ---
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> HealthBar;

    // --- Optional Bound Widgets ---
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> AmmoText;

public:
    UFUNCTION(BlueprintCallable, Category = "UI")
    void UpdateHealth(float HealthPercent);

    UFUNCTION(BlueprintCallable, Category = "UI")
    void UpdateAmmo(int32 Current, int32 Max);
};
```

### Source
```cpp
#include "MyWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

void UMyWidget::NativeConstruct()
{
    Super::NativeConstruct();
}

void UMyWidget::UpdateHealth(const float HealthPercent)
{
    if (IsValid(HealthBar))
    {
        HealthBar->SetPercent(HealthPercent);
    }
}

void UMyWidget::UpdateAmmo(const int32 Current, const int32 Max)
{
    if (IsValid(AmmoText))
    {
        AmmoText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Current, Max)));
    }
}
```
