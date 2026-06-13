# EQS Patterns — Cover and Positioning Queries

## Build.cs Requirement

```csharp
"EnvironmentQuery"
```

## Companion Cover Query — C++ Implementation

This is the EQS query the companion uses to find cover positions during combat.

### Query Asset (Created in Editor or C++)

Most EQS queries are created in the editor as `UEnvironmentQuery` assets. However, the generators and tests can be implemented in C++ for custom behavior.

**Note:** UE5 provides `UBTTask_RunEQSQuery` out of the box, which handles the async pattern, blackboard writing, and BT integration automatically. The custom implementation below is useful when you need additional logic (e.g., custom scoring, fallback behavior) that the built-in task doesn't support.

### Running EQS from a BT Task (Custom Implementation)

```cpp
// Header
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "BTTask_FindCoverEQS.generated.h"

UCLASS()
class MYPROJECT_API UBTTask_FindCoverEQS : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FindCoverEQS();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
    UPROPERTY(EditAnywhere, Category = "EQS")
    TObjectPtr<UEnvQuery> CoverQuery;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_CoverLocation;

private:
    // Use TWeakObjectPtr — the BT component could be destroyed while the async query runs
    void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result, TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp);
};

// Source
#include "BTTask_FindCoverEQS.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EnvironmentQuery/EnvQueryManager.h"

UBTTask_FindCoverEQS::UBTTask_FindCoverEQS()
{
    NodeName = TEXT("Find Cover (EQS)");
    bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTTask_FindCoverEQS::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* Controller = OwnerComp.GetAIOwner();
    if (!IsValid(Controller)) return EBTNodeResult::Failed;

    if (!IsValid(CoverQuery))
    {
        UE_LOG(LogTemp, Error, TEXT("FindCoverEQS: CoverQuery is null"));
        return EBTNodeResult::Failed;
    }

    APawn* Pawn = Controller->GetPawn();
    if (!IsValid(Pawn)) return EBTNodeResult::Failed;

    // Capture weak pointer for async callback safety
    TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp(&OwnerComp);

    FEnvQueryRequest QueryRequest(CoverQuery, Pawn);
    QueryRequest.Execute(
        EEnvQueryRunMode::SingleResult,
        FQueryFinishedSignature::CreateUObject(this, &UBTTask_FindCoverEQS::OnQueryFinished, WeakOwnerComp)
    );

    return EBTNodeResult::InProgress; // Async — wait for callback
}

void UBTTask_FindCoverEQS::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result, TWeakObjectPtr<UBehaviorTreeComponent> WeakOwnerComp)
{
    // Validate the BT component still exists — it could have been destroyed during the async query
    UBehaviorTreeComponent* OwnerComp = WeakOwnerComp.Get();
    if (!IsValid(OwnerComp)) return;

    if (!Result.IsValid() || !Result->IsSuccessful())
    {
        FinishLatentTask(*OwnerComp, EBTNodeResult::Failed);
        return;
    }

    const FVector BestLocation = Result->GetItemAsLocation(0);

    UBlackboardComponent* BB = OwnerComp->GetBlackboardComponent();
    if (IsValid(BB))
    {
        BB->SetValueAsVector(BB_CoverLocation.SelectedKeyName, BestLocation);
        FinishLatentTask(*OwnerComp, EBTNodeResult::Succeeded);
    }
    else
    {
        FinishLatentTask(*OwnerComp, EBTNodeResult::Failed);
    }
}
```

## Cover Query Design (Editor Setup)

### Generator
- **Type:** Points: Grid
- **Grid Half Size:** 1500
- **Space Between:** 200
- **Generate Around:** Querier
- **Project Down:** Navigation (ensures points are on nav mesh)

### Tests (in order of importance)

**1. Trace to Enemy (Is This Cover?)**
- Type: Trace
- Context: BB_CombatTarget
- Purpose: Check if line from test point to enemy is **blocked**
- Scoring: **Prefer blocked** (Score = 1.0 if blocked, 0.0 if clear LOS)
- This is the core cover test — if enemy can see this point, it's not cover

**2. Distance to Enemy**
- Type: Distance
- Context: BB_CombatTarget
- Scoring curve: **Prefer range 800-1200** (bell curve, peak at 1000)
- Filter: Minimum 400 (too close = dangerous), Maximum 2000 (too far = useless)
- Ensures companion takes cover at useful engagement distance

**3. Distance to Player**
- Type: Distance
- Context: BB_FollowTarget (the player)
- Scoring: **Prefer closer** (inverse linear, weight 0.7)
- Prevents companion from wandering too far from player while seeking cover

**4. Path Existence**
- Type: PathFinding
- Context: Querier
- Filter only: **Must have valid path** (score doesn't matter)
- Eliminates unreachable points

**5. Dot Product — Between Enemy and Player (Optional)**
- Type: Dot
- Line A: TestPoint → BB_CombatTarget
- Line B: TestPoint → BB_FollowTarget
- Prefer: Negative dot (companion is between cover and can see both enemy and player)
- Weight: 0.3 (minor preference, not required)

### Test Weights
| Test | Weight | Purpose |
|------|--------|---------|
| Trace (is cover) | 1.0 | Core requirement |
| Distance to enemy | 0.8 | Engagement range |
| Distance to player | 0.7 | Stay near player |
| Path existence | Filter | Must be reachable |
| Dot product | 0.3 | Positioning preference |

## Sniper Nest Query Design (Enemy Sniper)

### Generator
- **Type:** Points: Grid
- **Grid Half Size:** 2000
- **Space Between:** 300
- **Generate Around:** Querier
- **Project Down:** Navigation

### Tests

**1. Trace to Player (Has Sightline)**
- Must have clear LOS to player's last known position
- Filter: **Must pass** (no point without sightline)

**2. Distance to Player**
- Prefer: **Far** (minimum 1500, prefer 2000-3000)
- Snipers want distance

**3. Height Advantage**
- Type: Distance (Z-axis only, or use `EnvQueryTest_Project` with Z comparison)
- Prefer: Higher elevation than target
- Weight: 0.6
- Note: Compare Z coordinates of test point vs BB_CombatTarget. There is no built-in "trace to sky" EQS test — use Z comparison or a custom `UEnvQueryTest` if needed

**4. Distance from Current Position**
- Prefer: Closer to current position (don't reposition across the map)
- Weight: 0.4

**5. Path Existence**
- Filter: Must have valid path

## Flanking Query Design (Future — Enemy Rusher/Grunt)

### Generator
- **Type:** Points: Ring
- **Ring Radius:** 800
- **Arc Angle:** 270 (behind and to sides of player, not in front)
- **Generate Around:** BB_CombatTarget (the player)

### Tests

**1. Trace FROM Player to Test Point**
- Prefer: **Blocked** (player can't see the flanking position)
- Weight: 1.0

**2. Trace from Test Point to Player**
- Prefer: **Clear** (flanker CAN see player from destination)
- Weight: 0.8

**3. Distance from Allies**
- Prefer: Far from other enemies (spread out, don't cluster)
- Weight: 0.5

**4. Path Length**
- Prefer: Shorter path (don't take a long detour)
- Weight: 0.6

**5. Path Existence**
- Filter: Must be reachable

## Performance Notes

- EQS queries are **async** — never block waiting for results
- Grid generator with 1500 radius and 200 spacing = ~176 points per query. Acceptable.
- Don't run EQS every frame — use BT service intervals (every 3-5 seconds in combat)
- If many AI run EQS simultaneously, stagger with random deviation on the BT service interval
- Consider reducing grid density for less critical queries (flanking can use 300 spacing)
- Profile with `stat EQS` in-editor to catch expensive queries
- Use `TWeakObjectPtr` for any UObject passed to EQS async callbacks — the object may be destroyed before the query completes
