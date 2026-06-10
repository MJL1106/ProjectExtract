# Character Movement Component Networking

## How CMC Prediction Works

1. Client executes movement locally (instant feel)
2. Client sends `FSavedMove` to server (input + timestamp)
3. Server replays the move and validates
4. If server result differs significantly → sends correction
5. Client receives correction, snaps to corrected position, replays all unacknowledged moves

## Key Settings

```cpp
UCharacterMovementComponent* CMC = GetCharacterMovement();

// Smoothing for simulated proxies (other players on your screen)
CMC->NetworkSimulatedSmoothLocationTime = 0.1f; // Seconds to smooth position corrections
CMC->NetworkSimulatedSmoothRotationTime = 0.05f;

// How far off before a correction is sent
CMC->NetworkMaxSmoothUpdateDistance = 256.f; // Beyond this, teleport instead of smooth
CMC->NetworkNoSmoothUpdateDistance = 384.f;

// For FPS — tighter correction tolerance
CMC->NetworkMinTimeBetweenClientAdjustments = 0.1f;
```

## Custom Movement Modes (Wall Run, Slide, Grapple)

Adding a custom movement mode requires hooking into the CMC's networking pipeline so the server can validate and clients can predict.

### Step 1: Define the Movement Mode

```cpp
// In your Character header
UENUM(BlueprintType)
enum class ECustomMovementMode : uint8
{
    None = 0,
    WallRun = 1,
    Slide = 2,
    Grapple = 3
};
```

### Step 2: Subclass UCharacterMovementComponent

```cpp
// Header
UCLASS()
class MYPROJECT_API UMyCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    // Custom movement mode
    UPROPERTY(BlueprintReadOnly, Category = "Movement")
    ECustomMovementMode CustomMode = ECustomMovementMode::None;

    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
    virtual bool IsMovingOnGround() const override;
    virtual float GetMaxSpeed() const override;

    // --- Networking ---
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};
```

### Step 3: Custom FSavedMove

This is what gets sent to the server for validation:

```cpp
class FMySavedMove : public FSavedMove_Character
{
public:
    uint8 bSavedWantsToSlide : 1;
    uint8 bSavedWantsToWallRun : 1;

    virtual void Clear() override;
    virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
    virtual void PrepMoveFor(ACharacter* Character) override;
    virtual uint8 GetCompressedFlags() const override;
};

class FMyNetworkPredictionData : public FNetworkPredictionData_Client_Character
{
public:
    FMyNetworkPredictionData(const UCharacterMovementComponent& ClientMovement);
    virtual FSavedMovePtr AllocateNewMove() override;
};
```

### Step 4: Implementation

```cpp
// Allocate custom saved move
FSavedMovePtr FMyNetworkPredictionData::AllocateNewMove()
{
    return FSavedMovePtr(new FMySavedMove());
}

FNetworkPredictionData_Client* UMyCharacterMovement::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
    {
        UMyCharacterMovement* MutableThis = const_cast<UMyCharacterMovement*>(this);
        MutableThis->ClientPredictionData = new FMyNetworkPredictionData(*this);
    }
    return ClientPredictionData;
}

// Save input flags for network transmission
void FMySavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
    FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

    if (const auto* CMC = Cast<UMyCharacterMovement>(Character->GetCharacterMovement()))
    {
        bSavedWantsToSlide = CMC->bWantsToSlide;
        bSavedWantsToWallRun = CMC->bWantsToWallRun;
    }
}

// Restore input flags when replaying moves after correction
void FMySavedMove::PrepMoveFor(ACharacter* Character)
{
    FSavedMove_Character::PrepMoveFor(Character);

    if (auto* CMC = Cast<UMyCharacterMovement>(Character->GetCharacterMovement()))
    {
        CMC->bWantsToSlide = bSavedWantsToSlide;
        CMC->bWantsToWallRun = bSavedWantsToWallRun;
    }
}

// Can we combine two moves to reduce bandwidth?
bool FMySavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
    const auto* OtherMove = static_cast<FMySavedMove*>(NewMove.Get());

    if (bSavedWantsToSlide != OtherMove->bSavedWantsToSlide) return false;
    if (bSavedWantsToWallRun != OtherMove->bSavedWantsToWallRun) return false;

    return FSavedMove_Character::CanCombineWith(NewMove, InCharacter, MaxDelta);
}
```

## Common CMC Networking Issues

| Issue | Fix |
|-------|-----|
| Custom movement desyncs | Must implement FSavedMove — client prediction requires it |
| Jittery movement on other players | Tune NetworkSimulatedSmoothLocationTime |
| Rubber-banding on fast movement | Increase NetworkMaxSmoothUpdateDistance or reduce correction threshold |
| Movement ability (dash, teleport) doesn't work in multiplayer | Must go through CMC pipeline, not raw SetActorLocation |
| Sprint speed only works for host | Speed changes must be in CMC so both client and server agree on max speed |
