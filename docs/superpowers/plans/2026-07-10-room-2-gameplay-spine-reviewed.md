# Room 2 Gameplay Spine Implementation Plan (Reviewed)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the reusable Room 2 gameplay spine: a Stealth-gated entrance, local aggression punishment volumes, a placeable extraction NPC that starts a finite adaptive Director wave, a wave-gated lift interaction, and a Level Complete/restart screen.

**Architecture:** Keep the Enemy Director responsible only for adaptive spawning, temporary profiles, and finite-wave lifecycle. Placeable World actors own Room 2 sequencing and communicate through delegates and a small interaction interface. GameMode/PlayerController own authoritative completion and local UI; marketplace elevator assets remain untouched.

**Tech Stack:** Unreal Engine 5.7, UE C++, Enhanced Input, UMG, WorldSubsystem, replicated Actors/RPCs, VibeUE for Blueprint/UMG/level assets, NeoStack for PIE verification.

## Global Constraints

- Preserve every unrelated dirty working-tree change, especially DemoMap and enemy Blueprint work.
- No hardcoded `/Game/...` references in new C++; assign asset classes in Blueprint defaults.
- All AI, wave, gate, interaction, and completion state changes are server-authoritative.
- Clear every new timer in `EndPlay` or `Deinitialize`.
- No per-frame Actor Tick for stealth pressure; use a timer at `0.25` seconds.
- Use `TWeakObjectPtr` for tracked actors that may die or be destroyed.
- Reserve arrays when the required capacity is known.
- Health-stim inventory/injection and extraction-NPC escort behaviour are out of scope.
- Do not modify the marketplace elevator Blueprints; use a separate interaction-gate actor.
- `BP_Double_Door7` is the only door instance gated in DemoMap.
- Existing `X -> 1/2/3` mode selection, takedowns, loot, and objective systems remain intact.

---

### Task 1: Pure encounter state types and automation tests

**Files:**
- Create: `Extraction/Source/Extraction/Public/Enemy/Director/DirectorWaveTypes.h`
- Create: `Extraction/Source/Extraction/Public/World/StealthDisciplineTypes.h`
- Create: `Extraction/Source/Extraction/Private/Tests/Room2EncounterTypes.spec.cpp`
- Modify: `Extraction/Source/Extraction/Extraction.Build.cs`

**Interfaces:**
- Produces: `FDirectorWaveRequest`, `FDirectorWaveProgress`, `FStealthDisciplineSettings`, `FStealthPressureAccumulator`, `EWaveCompletionAction`.
- Consumes: `EMissionPhase`, `UDirectorConfigData`.

- [ ] **Step 1: Register the tests include path**

Add `"Extraction/Private/Tests"` to `PrivateIncludePaths`. Do not add a new module.

- [ ] **Step 2: Add the finite-wave types**

Define the reflected request and pure progress tracker:

```cpp
USTRUCT(BlueprintType)
struct EXTRACTION_API FDirectorWaveRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	FName WaveId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="1"))
	int32 TargetSquads = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	EMissionPhase MissionPhase = EMissionPhase::Objective;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	TObjectPtr<UDirectorConfigData> ConfigOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="5.0"))
	float BlockedWarningSeconds = 30.f;
};

UENUM(BlueprintType)
enum class EWaveCompletionAction : uint8
{
	UnlockExit,
	CompleteLevel,
	BroadcastOnly,
};

struct EXTRACTION_API FDirectorWaveProgress
{
	int32 TargetSquads = 0;
	int32 SpawnedSquads = 0;
	int32 RemainingMembers = 0;

	void Begin(int32 InTargetSquads);
	bool RecordSuccessfulSquad(int32 SpawnedMemberCount);
	void RecordMemberEnded();
	bool IsActive() const { return TargetSquads > 0; }
	bool CanSpawnMore() const;
	bool IsComplete() const;
};

struct EXTRACTION_API FDirectorProfileSelector
{
	static const UDirectorConfigData* SelectConfig(bool bWaveActive, const UDirectorConfigData* Wave,
		bool bPunishmentActive, const UDirectorConfigData* Punishment, const UDirectorConfigData* Base);
	static EMissionPhase SelectPhase(bool bWaveActive, EMissionPhase Wave,
		bool bPunishmentActive, EMissionPhase Punishment, EMissionPhase Base);
};
```

`RecordSuccessfulSquad` returns false and changes nothing for `SpawnedMemberCount <= 0`. `RecordMemberEnded` clamps at zero. Completion requires target reached and remaining members zero.

- [ ] **Step 3: Add pressure settings and transitions**

```cpp
UENUM(BlueprintType)
enum class EStealthPressureTransition : uint8
{
	None,
	Warned,
	Escalated,
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FStealthDisciplineSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float SprintGraceSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float SprintPressurePerSecond = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float PressurePerShot = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float DecayPerSecond = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float WarningThreshold = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float EscalationThreshold = 70.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0")) float SprintSpeedThreshold = 550.f;
};

struct EXTRACTION_API FStealthPressureAccumulator
{
	float Pressure = 0.f;
	float ContinuousSprintSeconds = 0.f;
	bool bWarned = false;
	bool bEscalated = false;

	EStealthPressureTransition Advance(float DeltaSeconds, bool bSprinting, int32 NormalShots,
		const FStealthDisciplineSettings& Settings);
	void Reset();
};
```

`Advance` applies sprint pressure only after the grace time, ignores exempt shots by accepting only `NormalShots`, decays when neither sprinting nor firing, clamps pressure to `[0, EscalationThreshold]`, and broadcasts each transition once.

- [ ] **Step 4: Write failing automation tests**

Cover these exact cases in `Room2EncounterTypes.spec.cpp` using `IMPLEMENT_SIMPLE_AUTOMATION_TEST` with `EditorContext | ProductFilter`:

```cpp
TestFalse(TEXT("zero-member squad is not counted"), Progress.RecordSuccessfulSquad(0));
TestTrue(TEXT("target reached but living members blocks completion"), !Progress.IsComplete());
Progress.RecordMemberEnded();
TestTrue(TEXT("completion requires target squads and zero members"), Progress.IsComplete());

TestEqual(TEXT("brief sprint remains pressure-free"), Acc.Pressure, 0.f);
TestEqual(TEXT("exempt shot contributes no pressure"), Acc.Advance(0.25f, false, 0, Settings), EStealthPressureTransition::None);
TestEqual(TEXT("warning fires once"), Transition, EStealthPressureTransition::Warned);
TestEqual(TEXT("escalation fires once"), Transition, EStealthPressureTransition::Escalated);

TestEqual(TEXT("wave config has highest priority"),
	FDirectorProfileSelector::SelectConfig(true, WaveConfig, true, PunishmentConfig, BaseConfig), WaveConfig);
TestEqual(TEXT("punishment restores after wave"),
	FDirectorProfileSelector::SelectConfig(false, WaveConfig, true, PunishmentConfig, BaseConfig), PunishmentConfig);
```

- [ ] **Step 5: Run the tests red, implement the pure methods, then run green**

Run after the editor-closed build:

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject' `
  -ExecCmds='Automation RunTests Extraction.Room2.EncounterTypes;Quit' -unattended -nop4
```

Expected before implementation: test compile/failure. Expected after implementation: all `Extraction.Room2.EncounterTypes` tests pass.

- [ ] **Step 6: Review and commit Task 1**

Run `git diff --check`, dispatch `ue5-reviewer`, fix every CRITICAL/WARNING, reconcile the roadmap only if its status changes, then commit the four Task 1 files.

---

### Task 2: Director contextual profiles and finite adaptive wave

**Files:**
- Modify: `Extraction/Source/Extraction/Public/Enemy/EnemyDirectorSubsystem.h`
- Modify: `Extraction/Source/Extraction/Private/Enemy/EnemyDirectorSubsystem.cpp`
- Modify: `Extraction/Source/Extraction/Public/Enemy/Director/DirectorConfigData.h`
- Modify: `Extraction/Source/Extraction/Private/Enemy/Director/DirectorConfigData.cpp`
- Test: `Extraction/Source/Extraction/Private/Tests/Room2EncounterTypes.spec.cpp`

**Interfaces:**
- Consumes: `FDirectorWaveRequest`, `FDirectorWaveProgress`.
- Produces: `StartWave`, `CancelWave`, `ActivatePunishmentProfile`, `DeactivatePunishmentProfile`, wave delegates, and effective config/phase selection.

- [ ] **Step 1: Extend the failing selector tests**

Finish the `FDirectorProfileSelector` coverage: wave profile beats punishment, punishment beats base, removing wave restores punishment, and removing punishment restores base. Run the focused automation group and confirm these assertions fail until the selector methods are implemented.

- [ ] **Step 2: Add Director public APIs and delegates**

Declare:

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectorWaveStarted, FName, WaveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDirectorWaveProgress, FName, WaveId, int32, SpawnedSquads, int32, RemainingMembers);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectorWaveCompleted, FName, WaveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDirectorWaveBlocked, FName, WaveId, FText, Reason);

UFUNCTION(BlueprintCallable, Category="Enemy|Director|Wave")
bool StartWave(const FDirectorWaveRequest& Request);

UFUNCTION(BlueprintCallable, Category="Enemy|Director|Wave")
void CancelWave(FName WaveId);

bool ActivatePunishmentProfile(AActor* Source, UDirectorConfigData* Profile, EMissionPhase Phase);
void DeactivatePunishmentProfile(AActor* Source);
```

Expose Blueprint-assignable wave delegates and pure getters for active wave, squad progress, and remaining members.

- [ ] **Step 3: Separate base and effective profiles**

Keep the existing `Config` as the base config. Add weak punishment source/config, punishment phase, wave request, and this selection rule:

```cpp
const UDirectorConfigData* UEnemyDirectorSubsystem::GetEffectiveConfig() const
{
	return FDirectorProfileSelector::SelectConfig(
		WaveProgress.IsActive(), ActiveWaveRequest.ConfigOverride,
		PunishmentSource.IsValid(), PunishmentConfig, Config);
}
```

Add the corresponding effective phase helper. Replace direct config reads in tension, sawtooth, phase-config lookup, and spawn-distance selection with effective config reads. Do not change `SetDirectorConfig`'s base-config ownership rule.

- [ ] **Step 4: Make spawning report actual members**

Change `TrySpawn` to return success and optionally return spawned members. Change `SpawnSquadAtZone` to fill a reserved output array. A squad is successful only when at least one `AEnemyCharacter` was spawned.

- [ ] **Step 5: Integrate wave lifecycle into the one-second Director tick**

When wave active:

- Use wave phase/config.
- Stop spawn attempts after `TargetSquads` successful squads.
- Add successfully spawned members to `TSet<TWeakObjectPtr<AEnemyCharacter>> WaveMembers`.
- On `HandleEnemyKilled`, remove the exact dead wave member once.
- Prune invalid members during `DirectorTick`.
- Complete once `FDirectorWaveProgress::IsComplete()`.
- Increment blocked time only when the wave still needs squads and a valid spawn fails; broadcast the blocked message once at `BlockedWarningSeconds`.

`StartWave` must call `TripAlarm`, reset sawtooth/build timing, and reject a second active wave. `CompleteWave` clears wave state only after broadcasting its final progress and completion events.

- [ ] **Step 6: Verify Director behaviour in a focused PIE harness**

Use the existing Director spawn zones and a temporary wave request with `TargetSquads=2`. Verify logs show two successful squads, no third squad, and completion only after tracked members die. Verify unrelated placed enemies do not change `RemainingMembers`.

- [ ] **Step 7: Review and commit Task 2**

Run the consolidated reviewer with the task goal and all five changed files. Fix all findings, rerun automation tests, then commit.

---

### Task 3: External door gate and companion-mode objective actor

**Files:**
- Modify: `Extraction/Source/Extraction/Public/World/DoorBase.h`
- Modify: `Extraction/Source/Extraction/Private/World/DoorBase.cpp`
- Modify: `Extraction/Source/Extraction/Public/World/BreachableDoor.h`
- Modify: `Extraction/Source/Extraction/Private/World/BreachableDoor.cpp`
- Modify: `Extraction/Source/Extraction/Public/World/ScriptedDoor.h`
- Modify: `Extraction/Source/Extraction/Private/World/ScriptedDoor.cpp`
- Create: `Extraction/Source/Extraction/Public/World/CompanionModeDoorGate.h`
- Create: `Extraction/Source/Extraction/Private/World/CompanionModeDoorGate.cpp`

**Interfaces:**
- Consumes: `UCompanionCommandComponent::OnCompanionModeChanged`, `UObjectiveSubsystem`.
- Produces: `ADoorBase::SetExternalGateLocked`, replicated external-gate state, and a placeable `ACompanionModeDoorGate`.

- [ ] **Step 1: Add the replicated external gate**

Add to `ADoorBase`:

```cpp
UFUNCTION(BlueprintCallable, Category="Door|Gate")
void SetExternalGateLocked(bool bLocked);

UFUNCTION(BlueprintPure, Category="Door|Gate")
bool IsExternalGateLocked() const { return bExternalGateLocked; }

UPROPERTY(Replicated, Transient)
bool bExternalGateLocked = false;
```

Register it with `DOREPLIFETIME`; call `Super::GetLifetimeReplicatedProps`. `SetExternalGateLocked` changes state only on authority.

- [ ] **Step 2: Enforce the gate on every native opening path**

Make `ABreachableDoor::CanBreach_Implementation` and `AScriptedDoor::CanBreach_Implementation` require `!IsExternalGateLocked()`. Add the same early return in both `Breach_Implementation` functions and in `ABreachableDoor::TryUnlock` so a direct call cannot bypass the gate.

- [ ] **Step 3: Implement the mode-gate actor**

Create a non-ticking replicated actor with root, `UBoxComponent`, target `ADoorBase`, required `ECompanionMode`, objective id/text, and replicated `bUnlocked`.

On server BeginPlay, lock the target. On local-player overlap, add the objective. Resolve the overlapping player's `UCompanionCommandComponent`, bind mode changes once, and unlock permanently when the required mode arrives. On unlock: clear the door gate, remove the objective, disable the box, and unbind. Replicate `bUnlocked`; its OnRep removes the local client's objective and disables the local box. Clean bindings in `EndPlay`.

- [ ] **Step 4: Inspect `BP_Double_Door7` before asset edits**

With PIE stopped, read the instance class and `BP_Double_Door` graphs. Confirm whether any direct player input/overlap path opens the vendor door without native `CanBreach`. If it does, add one Blueprint branch calling `IsExternalGateLocked`; do not alter its timeline or transforms.

- [ ] **Step 5: PIE-test the exact door gate**

Set the target to `BP_Double_Door7`. Verify Normal/Combat cannot open it through player, companion, or enemy paths; selecting Stealth unlocks it once; later mode changes do not relock; no other double-door instance is affected.

- [ ] **Step 6: Review and commit Task 3**

Run reviewer, fix every warning, build/test, and commit only the Task 3 source plus any required `BP_Double_Door` asset change.

---

### Task 4: Shot-context relay and Stealth Discipline Volume

**Files:**
- Modify: `Extraction/Source/Extraction/Public/Companion/CompanionCharacter.h`
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Modify: `Extraction/Source/Extraction/Public/Components/WeaponComponent.h`
- Modify: `Extraction/Source/Extraction/Private/Components/WeaponComponent.cpp`
- Create: `Extraction/Source/Extraction/Public/World/StealthDisciplineVolume.h`
- Create: `Extraction/Source/Extraction/Private/World/StealthDisciplineVolume.cpp`
- Test: `Extraction/Source/Extraction/Private/Tests/Room2EncounterTypes.spec.cpp`

**Interfaces:**
- Consumes: `FStealthPressureAccumulator`, Director punishment APIs, player velocity, weapon-fire callback.
- Produces: per-shot `FOnPlayerWeaponShot(bool bStealthExempt)` and placeable pressure volume.

- [ ] **Step 1: Snapshot the shoot-takedown exemption before firing**

Add `ACompanionCharacter::IsShootTakedownArmed() const`, returning armed and active method Shoot. In authority-side `AExtractionPlayer::FireStart`, snapshot this state before broadcasting `OnPlayerFiredWeapon`, because the synchronous takedown listener disarms before the actual weapon shot. Pass that trusted snapshot into `UWeaponComponent::StartFire(bool bAuthorityTakedownSnapshot)`.

For a remote client, do not trust a client exemption bit: `Server_StartFire` resolves the server companion and snapshots `IsShootTakedownArmed()` before starting the weapon. Store the result inside WeaponComponent as `bNextShotStealthExempt`. The first actual shot consumes it; continued automatic fire is not exempt.

- [ ] **Step 2: Relay each actual shot from WeaponComponent**

Declare:

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerWeaponShot, bool, bStealthExempt);
UPROPERTY(BlueprintAssignable, Category="Weapon|Events")
FOnPlayerWeaponShot OnPlayerWeaponShot;
```

In the authoritative `OnWeaponFiredCallback`, copy and clear `bNextShotStealthExempt`, broadcast it, then preserve the existing multicast effects flow. `StopFire`, weapon replacement, and `EndPlay` also clear the bit so it cannot leak into a later trigger pull.

- [ ] **Step 3: Implement the pressure volume**

Create a replicated, non-ticking actor with a box component, `FStealthDisciplineSettings`, warning text, punishment config, punishment phase, and a `0.25f` timer.

On authoritative player entry:

- Cache the player weakly.
- Bind the player's WeaponComponent shot relay.
- Reset the accumulator.
- Start the sampling timer.

Each timer sample treats horizontal speed at or above `SprintSpeedThreshold` as sprinting. Feed the accumulated count of non-exempt shots into `Advance`. On Warned, broadcast the existing HUD notification channel once. On Escalated, call `ActivatePunishmentProfile(this, Config, Phase)` and `TripAlarm`.

On exit, unbind, clear timer, call `DeactivatePunishmentProfile(this)`, reset state, and leave spawned enemies untouched. Clear the timer and delegate in `EndPlay`.

- [ ] **Step 4: Test the exact pressure behaviours**

Automation: brief sprint, grace crossing, decay, one warning, one escalation, exempt shot, repeated normal shots.

PIE: a companion shoot takedown does not move pressure; holding automatic fire does; leaving the box prevents subsequent Director punishment spawns.

- [ ] **Step 5: Review and commit Task 4**

Review replication, timer cleanup, and shot ordering specifically. Fix all findings, rerun tests, and commit.

---

### Task 5: Reusable interaction and extraction NPC wave trigger

**Files:**
- Create: `Extraction/Source/Extraction/Public/World/WorldInteractable.h`
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Create: `Extraction/Source/Extraction/Public/World/ExtractionTargetActor.h`
- Create: `Extraction/Source/Extraction/Private/World/ExtractionTargetActor.cpp`

**Interfaces:**
- Consumes: Director wave APIs/delegates and ObjectiveSubsystem.
- Produces: `IWorldInteractable` and `AExtractionTargetActor`.

- [ ] **Step 1: Add the interaction interface**

Define Blueprint-native events:

```cpp
UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction")
bool CanWorldInteract(AActor* Interactor) const;
UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction")
void WorldInteract(AActor* Interactor);
UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction")
FText GetWorldInteractionPrompt(AActor* Interactor) const;
```

- [ ] **Step 2: Route new interactions through authority**

In `TryWorldInteract`, check `IWorldInteractable` before loot/keycard logic. On authority, execute directly. On clients, call `ServerWorldInteract(AActor* Target)`. Server implementation validates interface, `CanWorldInteract`, and squared distance against `InteractTraceRange + 50.f` before executing. Do not refactor existing loot/keycard paths in this task.

- [ ] **Step 3: Implement the placeable extraction target**

Create a replicated, non-damageable, non-ticking actor with root scene component, skeletal mesh, and visibility-blocking interaction box. Expose initial/defence objective ids and labels, `FDirectorWaveRequest`, `EWaveCompletionAction`, and an optional `ALevelCompletionLiftGate` target.

Interaction is accepted once on authority. It removes the reach objective, adds Defend the Position, and binds to completion/blocked events before calling `StartWave`. If `StartWave` returns false, it unbinds, restores the reach objective and interaction state, and emits a precise failure notification. Replicated activation/completion state drives equivalent objective changes on clients.

On matching `WaveId` completion, remove the defence objective and perform exactly one action: unlock exit, complete level, or broadcast only. Clean Director bindings in `EndPlay`.

- [ ] **Step 4: Add editor validation**

In editor builds, warn when squad count is below one, Config is missing, Unlock Exit lacks a target, or mesh/collision references are absent. Do not hardcode the purchased NPC mesh or animation.

- [ ] **Step 5: Test interaction and one-shot behaviour**

PIE assertions: client/server range validation, second interaction ignored, one wave starts, only matching wave completion is consumed, and blocked spawn feedback leaves the objective active.

- [ ] **Step 6: Review and commit Task 5**

Review the new RPC/interface security and delegate cleanup. Fix all findings, test, and commit.

---

### Task 6: Lift gate, authoritative completion, and restart UI

**Files:**
- Create: `Extraction/Source/Extraction/Public/World/LevelCompletionLiftGate.h`
- Create: `Extraction/Source/Extraction/Private/World/LevelCompletionLiftGate.cpp`
- Modify: `Extraction/Source/Extraction/Public/Game/ExtractionGameMode.h`
- Modify: `Extraction/Source/Extraction/Private/Game/ExtractionGameMode.cpp`
- Modify: `Extraction/Source/Extraction/Public/Game/ExtractionPlayerController.h`
- Modify: `Extraction/Source/Extraction/Private/Game/ExtractionPlayerController.cpp`
- Create: `Extraction/Source/Extraction/Public/UI/LevelCompleteWidget.h`
- Create: `Extraction/Source/Extraction/Private/UI/LevelCompleteWidget.cpp`
- Create in engine: `Extraction/Content/Core/UI/WBP_LevelComplete.uasset`
- Modify in engine: `Extraction/Content/Core/Blueprints/Game/BP_ExtractionPlayerController.uasset`

**Interfaces:**
- Consumes: `IWorldInteractable`, extraction target Unlock Exit action.
- Produces: replicated lift unlock, `AExtractionGameMode::CompleteLevel`, client completion UI, server restart.

- [ ] **Step 1: Implement the separate lift interaction gate**

Create a replicated actor with interaction box, referenced marketplace lift actor used only for marker location, locked/unlocked prompt text, objective id/label, and replicated `bUnlocked`.

Before unlock, interaction emits **Eliminate remaining enemies** through the existing notification channel. `UnlockExit()` sets the replicated flag once and registers **Use the lift**. Its OnRep performs the same objective swap on clients. After unlock, interaction calls `AExtractionGameMode::CompleteLevel` on authority.

- [ ] **Step 2: Add authoritative completion to GameMode**

Add idempotent `CompleteLevel()` and `RestartCurrentLevel()`. Completion sets the paused state and iterates all player controllers, calling their client completion RPC. Restart unpauses and opens `UGameplayStatics::GetCurrentLevelName(this, true)` from game code.

- [ ] **Step 3: Add PlayerController client UI and restart RPC**

Add designer-assigned `TSubclassOf<ULevelCompleteWidget>`, stored widget instance, `ClientShowLevelComplete`, and `ServerRequestRestartLevel`.

The client RPC creates once, adds at popup Z-order, sets UI-only input focused on the widget, and shows the cursor. The server restart RPC delegates to the current `AExtractionGameMode`.

- [ ] **Step 4: Implement the native widget base**

`ULevelCompleteWidget` uses optional `BindWidget` TitleText and required RestartButton. Bind RestartButton once in `NativeConstruct`, unbind in `NativeDestruct`, and call the owning `AExtractionPlayerController::RequestRestartLevel()`.

- [ ] **Step 5: Build the UMG asset through VibeUE**

Load `umg-widgets` and `blueprint-graphs`. Create a full-screen dim overlay, centered **LEVEL COMPLETE** title, and **RESTART LEVEL** button. Use native `NativeConstruct` binding; do not use `WidgetService.bind_event`. Compile with zero warnings and assign the widget class on `/Game/Core/Blueprints/Game/BP_ExtractionPlayerController`.

- [ ] **Step 6: Verify pause and restart**

In PIE, complete from the lift, verify world gameplay freezes while button input remains available, click Restart, and confirm DemoMap reloads with wave/door/lift state reset.

- [ ] **Step 7: Review and commit Task 6**

Run `ue5-reviewer` plus `ue5-ui-specialist`; fix all warnings, compile WBP again, and commit source plus the two UI/controller assets.

---

### Task 7: Full build, DemoMap wiring, and end-to-end UAT

**Files/assets:**
- Modify in engine: `Extraction/Content/UWC_Modular_Skyscraper/Maps/DemoMap.umap`
- Create in engine: `/Game/Core/Blueprints/World/BP_ExtractionTarget`
- Create in engine: `/Game/Core/Enemies/Director/DA_Director_Room2Punishment`
- Reuse: `/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Elevator_Lift`
- Update: `agent_docs/project_roadmap.md`

**Interfaces:**
- Consumes every prior task.
- Produces a Play-ready Room 2 flow.

- [ ] **Step 1: Clean review before build**

Run one consolidated `ue5-reviewer` over all changed source with the approved spec and plan paths. Fix and re-review until no CRITICAL/WARNING remains. Do not build before this gate is clean.

- [ ] **Step 2: Ask before closing, then full-build and reboot**

Use `engine-guard.ps1 can-close`, ask the user fresh, close only the Extraction editor by `.uproject` command line, and build `ExtractionEditor Win64 Development`. Require `Result: Succeeded`; reboot through `boot-engine`; verify VibeUE and NeoStack respond.

- [ ] **Step 3: Inspect the live Room 2 assets before editing**

With PIE stopped, locate `BP_Double_Door7`, the existing posed NPC/set-dressing actor, the Room 2 hallway/rooms, the lift/button/gates, Director zones, scope volumes, and objective markers. Capture actor/component transforms before changes.

- [ ] **Step 4: Create/configure reusable content assets**

Create `BP_ExtractionTarget` as a child of `AExtractionTargetActor`; leave mesh/animation unassigned if the purchased asset is not present. Create `DA_Director_Room2Punishment` with a shorter cadence, higher alive cap, and harder compositions than the base Infiltration config, but within the existing AI budget.

- [ ] **Step 5: Wire DemoMap without changing marketplace assets**

Place and configure:

- One companion-mode gate whose target is exactly `BP_Double_Door7`.
- Stealth discipline volume(s) covering the intended Room 2 stealth traversal; exclude the extraction defence room.
- Extraction target at the current posed-NPC location, with the agreed squad count and Unlock Exit action.
- Lift interaction gate at the elevator interaction point, referencing the existing lift actor.
- Director spawn zones/scope volume that cover the defence room and remain outside immediate player sight.

Save DemoMap once, then reload and verify every pre-captured unrelated actor transform is unchanged.

- [ ] **Step 6: Run one-line UAT scenarios**

- Door gate: Normal/Combat blocked; Stealth permanently unlocks only `BP_Double_Door7`.
- Discipline grace: short sprint, one shot, and detection produce no warning/escalation.
- Shoot takedown: commit shot is exempt.
- Discipline punishment: sustained sprint plus repeated shots warns once, then spawns harder squads.
- Volume exit: no new punishment spawn; existing enemies persist.
- Extraction NPC: one interaction starts exactly the configured squad count.
- Wave tracking: placed enemies do not block; every wave member must die.
- Lift locked: before completion, reports remaining enemies.
- Lift unlocked: after completion, objective changes and interaction shows Level Complete.
- Restart: current level reloads and all one-shot state resets.
- Multiplayer: server controls state; client observes identical door/wave/lift completion.

- [ ] **Step 7: Reconcile roadmap and final handoff**

Mark the Room 2 gameplay-spine items complete or in progress based on verified UAT; leave health stims and escort NPC deferred. Run `git diff --check`, report changed files/assets, test evidence, and outstanding deferred work. Do not push or merge.

---

## Required execution order

1. Task 1 pure state/tests.
2. Task 2 Director wave/profile foundation.
3. Task 3 door gate and Task 4 stealth volume after Task 2; these can be developed in parallel with exclusive file ownership.
4. Task 5 extraction interaction after Task 2.
5. Task 6 completion/lift after Task 5 interfaces stabilize.
6. Task 7 review, build, in-engine wiring, and UAT.

## Team composition

- Lead/current strong model: architecture, `AExtractionPlayerController`, `AExtractionGameMode`, integration, final judgement.
- `ue5-cpp-implementer` on `gpt-5.6-terra`: Director/types/tests ownership.
- `ue5-cpp-implementer` on `gpt-5.6-terra`: World actors, door gate, interaction ownership.
- `ue5-inengine-agent` on `gpt-5.6-terra`: UMG/assets/DemoMap wiring after build.
- `ue5-reviewer` inheriting the current model: consolidated safety, replication, performance, and edge-case review after each task.


