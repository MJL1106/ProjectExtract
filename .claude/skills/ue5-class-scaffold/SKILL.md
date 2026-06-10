---
name: ue5-class-scaffold
description: Generate UE5 C++ class files. Use when the user's request requires creating new .h/.cpp files — whether they explicitly say "new class" or describe a feature, system, or behavior that implies new C++ source files need to exist. This includes wanting a new actor, component, widget, controller, ability, subsystem, data asset, struct, or any gameplay feature that doesn't have existing source files yet.
---

# UE5 Class Scaffold

Generate production-ready UE5 C++ class files that are correct from the first compile. Infer the class type and base class from context. If ambiguous, ask.

Follow the project's CLAUDE.md hard rules. Match the module API macro from existing headers or the module's `*.Build.cs`.

## Class Type Reference

| Prefix | Base Class | Use For |
|--------|-----------|---------|
| `A` | `AActor` | World-placed things |
| `A` | `ACharacter` / `APawn` | Player or AI entities |
| `A` | `APlayerController` | Player input and possession |
| `A` | `AAIController` | AI decision-making |
| `A` | `AGameModeBase` | Game rules and flow |
| `U` | `UActorComponent` | Logic-only components |
| `U` | `USceneComponent` | Components with transforms |
| `U` | `UUserWidget` | UI widgets |
| `U` | `UGameInstanceSubsystem` | Singletons / managers |
| `F` | (struct) | Data containers |
| `E` | (enum class) | Finite categories |

## UCLASS Specifier Combos

- Actor/Character: `UCLASS(Blueprintable)`
- Component: `UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))`
- Subsystem: `UCLASS()`
- Widget: `UCLASS(Blueprintable)` or `UCLASS(Abstract, Blueprintable)` for bases
- A bare `UCLASS()` is almost never correct for gameplay classes.

## Class-Specific Patterns

- **AI Controllers:** Add `AIModule` to Build.cs. `TObjectPtr<UBehaviorTree>` + `TObjectPtr<UBlackboardData>` as EditAnywhere. Null-check Blackboard before use.
- **Characters (Enhanced Input):** Add `EnhancedInput` module. Null-check every `UInputAction*` before `BindAction()`.
- **Components:** `bWantsInitializeComponent = true` if runtime init needed. Override `InitializeComponent()` not constructor.
- **Widgets:** Use `BindWidgetOptional` + null-check. Override `NativeConstruct()` not constructor. Event-driven updates, not NativeTick.
- **Replicated:** `bReplicates = true` in constructor. Override `GetLifetimeReplicatedProps` — always call `Super::`. Include `Net/UnrealNetwork.h`.

## Gotchas

- Every UObject member pointer MUST have `UPROPERTY()` — silent GC crash without it
- Declare `EndPlay` override if the class uses timers, delegates, or cached references
- `PrimaryActorTick.bCanEverTick` — set explicitly, default to `false`
- Declare log category: `DECLARE_LOG_CATEGORY_EXTERN` in header, `DEFINE_LOG_CATEGORY` in source
- Forward declare instead of including headers where possible
- `.generated.h` must be the LAST include in the header
- `Category = "Section"` on every UPROPERTY
