# UE5 Conventions — ProjectExtract

## Naming Conventions
- **Classes:** `AExtractionCharacter`, `UExtractionAnimInstance` — always prefixed with `Extraction`
- **Enums:** `EWeaponType` — PascalCase with `E` prefix
- **Booleans:** `bIsInAir`, `bIsFalling` — `b` prefix
- **Log categories:** `LogExtraction`, `LogExtractionAnim` — `Log` prefix + system name
- **DataAssets:** `DA_Anim_Unarmed`, `DA_Anim_Pistol` — `DA_` prefix recommended
- **Blendspaces:** `BS_Locomotion_Unarmed` — `BS_` prefix recommended
- **AnimMontages:** `AM_Fire_Rifle` — `AM_` prefix recommended
- **AnimBlueprint:** `ABP_ExtractionCharacter` — `ABP_` prefix

## UPROPERTY Patterns
```cpp
// Editable in class defaults only (DataAssets, config)
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Config")

// Readable by Blueprint (AnimBP reads these)
UPROPERTY(BlueprintReadOnly, Category = "Animation|Locomotion")

// Private cached refs (GC-safe but hidden)
UPROPERTY()
TObjectPtr<AExtractionCharacter> OwningCharacter;

// Component created in constructor
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
```

## UFUNCTION Patterns
```cpp
// Pure getter (no side effects, usable in ABP)
UFUNCTION(BlueprintPure, Category = "Animation|Data")

// Callable action (has side effects)
UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
```

## Category Convention
Use pipe-separated hierarchy: `"Animation|Locomotion"`, `"Animation|Actions"`, `"Input|Touch Controls"`

## Pointer Rules (from CLAUDE.md Hard Rules)
- All UObject pointers: `UPROPERTY()` — prevents GC crashes
- Prefer `TObjectPtr<>` for UObject members
- Use `IsValid()` not `!= nullptr` for UObject checks
- Null-check every `Cast<>`, every raw pointer, every `GetComponent` result
- Use `TWeakObjectPtr<>` for non-owning refs that may go stale

## AnimInstance Pattern
- Override `NativeInitializeAnimation()` to cache character + movement component
- Override `NativeUpdateAnimation(float DeltaSeconds)` for per-frame reads
- **Zero allocations in NativeUpdateAnimation** — no new, no string concat, no TArray copies
- All state exposed as `BlueprintReadOnly` UPROPERTY for ABP to read
- Montage playback via `Montage_Play()`, return duration for timing

## Include Paths
Build.cs adds subfolder paths so includes can be flat:
```cpp
#include "ExtractionTypes.h"      // not "Core/ExtractionTypes.h"
#include "ExtractionCharacter.h"  // not "Character/ExtractionCharacter.h"
```

## Module Dependencies
Current: Core, CoreUObject, Engine, InputCore, EnhancedInput, AnimGraphRuntime, UMG, Slate

## Build.cs Pattern
```csharp
PublicIncludePaths.AddRange(new string[] {
    "Extraction/Public/Core",
    "Extraction/Public/Character",
    "Extraction/Public/Animation",
    "Extraction/Public/Game"
});
```
When adding a new subfolder, add it to both Public and Private include paths.
