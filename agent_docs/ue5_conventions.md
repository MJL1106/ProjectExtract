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

## Tooltip Convention
Any UPROPERTY that is designer-tweakable and **not self-explanatory** MUST include a `ToolTip` in the `meta` specifier. This appears when hovering the property in the Details panel.

**When to add a ToolTip:**
- The value's effect is non-linear or curve-based (exponents, easing, blend factors)
- The property interacts with other properties in non-obvious ways
- Valid ranges aren't obvious from the name alone
- The property uses units that aren't in the name (degrees/s, cm/s, etc.)
- A few example values with descriptions help the designer understand the feel

**When a ToolTip is NOT needed:**
- The property name + comment fully explains it (e.g., `WalkSpeed` in cm/s)
- Simple on/off booleans

**Format:**
```cpp
UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
    meta = (ClampMin = "0.5", ClampMax = "5.0",
        ToolTip = "Controls how the speed drops off over the slide duration.\n1.0 = Linear\n2.0 = Holds speed then drops\n3.0+ = More hang time"))
float SlideDecelerationExponent = 2.0f;
```

Use `\n` for line breaks within tooltips. Keep tooltips concise but include example values where useful for tuning.

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
