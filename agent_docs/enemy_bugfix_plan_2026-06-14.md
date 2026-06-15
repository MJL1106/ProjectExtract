# Enemy AI Bug-Fix Round — Plan & Decisions (2026-06-14, branch `Enemies`)

Autonomous overnight fix of 8 reported enemy AI bugs. Root-caused by a 6-agent parallel investigation
(5 cluster investigators + completeness critic). This doc records the root causes, the design decisions
made on the investigators' open questions (user asleep — director-proxy calls), and the editor/DA/BT
follow-ups that C++ alone cannot deliver.

## Root causes (verified, file:line)

- **Bug 6 (no damage) — NOT a C++-resolution bug.** `AEnemyCharacter::TakeDamage` + `UHealthComponent::TakeDamage`
  are correct and were NOT touched by the suspect commit d9419c5a. Cause is UPSTREAM: player likely fires a kit
  weapon whose BP `Fire_HitScan` owns damage (never calls `AWeaponBase::PerformHitscan`), OR `CurrentAmmo`
  defaults to 0 and `InitializeAmmo()` is never called in `BeginPlay` (CanFire()==false), OR a collision-channel
  issue. Decisive PIE tell: the `LogExtraction Log "X hit Y for Z damage"` line (WeaponBase.cpp:480) fires on every
  C++ player hit — its absence means our path isn't running. **Deliverable: diagnostics + safe ammo-seed in BeginPlay
  + editor handoff. Do NOT edit TakeDamage/HealthComponent.**
- **Bug 2 (Combat→patrol fall-through).** Combat subtree returns Failure when no LOS+range (CombatFire bails
  Failed at BTTask_EnemyCombatFire.cpp:53,93-98); base Selector then runs the lowest-priority `BTTask_EnemyPatrol`
  (now does return-to-post, added d9419c5a) while `AwarenessState`==Combat. Amplified by `LostContactGrace` 4→8s
  (d9419c5a). Sticky because `BTService_EnemyCombat` stops ticking on subtree fail → BB_HasLineOfSight/TargetInRange freeze false.
- **Bug 3 (slow perception).** `ComputeSightFillRate` uses linear `DistFactor=clamp(1-Dist/SightRadius,0.15,1)`
  (Awareness.cpp:565) — only ~40/s at Dist≈0. PLUS the stationary-aiming player hits `StillFillFactor=0.5`
  (the literal "blatantly in front" case) → ~12-20/s. Also enemy sight config omits `AutoSuccessRangeFromLastSeenLocation`
  (companion sets 500) → bSighted flickers at close range.
- **Bug 1 (suppressed crouch-in-place).** CombatFire Acquire suppression gate (lines 112-124): suppressed + no cover →
  crouch + Acquire↔Pause loop forever, never fails, so the Selector never re-seeks cover.
- **Bug 5 (accuracy never settles).** Settle clock resets per engagement entry (`SetAimTarget(Target)` at CombatFire:58
  after MoveToCover::StopAdvanceFire cleared it at line 334) and on LOS-flicker task churn; with SpreadSettleTime 2.0s and
  short bursts it stays in the 5-7° band. Also WeaponBase applies spread independently to Yaw AND Pitch (≈2× cone).
- **Bug 7a (not inside cover box).** Regression: d9419c5a changed arrival to `GetBehindCoverPosition(alpha, standoff≈59cm)`
  = capsuleRadius(34)+CoverStandoffPadding(25), pushing the body ~29cm OUTSIDE the 30cm-deep `CoverBoundsBox`. Companion
  still arrives on-line (BTTask_MoveToCover.cpp:149).
- **Bug 7b/c/d (too scared / cover doesn't protect / fallback feel).** Morale drains stack: sustained-suppression −10/s
  + flanked −10/s (every tick, no cooldown) ÷ resistance 1.0 → Broken (≤30) in 3.5-7s. Recovery +2/s is gated behind a
  5s no-loss grace that **resets every tick under suppression → recovery is structurally impossible mid-fight.** Morale
  is blind to BB_HasCover. No Broken-exit hysteresis (flickers at the 30 boundary). Fallback flees far (search ×2, dist-weight 2.0).
- **Bug 8 (never crouches / no stand cover).** `AEnemyCharacter` never sets `bCanCrouch=true`, so every `Char->Crouch()`
  in the cover/fallback tasks is a silent no-op. "No standing cover" is the same bug from the other side: crouch never
  engaged, so every slot looked like a stand.
- **Bug 4 (bark debug).** Bark path has zero instrumentation. Single chokepoint `UBarkSubsystem::RequestBark` (3 silent
  drops) + upstream `UEnemySquad::TryClaimSquadBark` (4th gate for squad barks).

## Design decisions (calls made on open questions)

1. **Bugs 1+2 = one coherent control-flow rewrite of `BTTask_EnemyCombatFire`, BT-ordering-agnostic.** While
   `AwarenessState`==Combat the task NEVER bubbles Failure to the Selector. Branches: has-LOS+range → peek-fire (existing);
   suppressed+has-cover → crouch-hold (existing, correct); **suppressed+no-cover → query nearest cover slot, move-and-fire
   toward it (InProgress), crouch-hold only if no slot found within search radius (re-seek cooldown ~2.5s to avoid thrash);
   no-LOS/out-of-range+Combat → pursue BB_LastKnownLocation (InProgress).** Self-sufficient so it does not depend on the
   unread BT_EnemyCombat_Grunt node order.
2. **Bug 3 = two terms.** (a) `DistFactor`=1.0 out to new `FullFillRange` (DA, default 1500), then ramp to 0.15 floor
   toward LoseSightRadius. (b) Raise `StillFillFactor` default 0.5→0.85 (a clearly-visible stationary starer should still
   convert fast). (c) Set enemy `SightConfig->AutoSuccessRangeFromLastSeenLocation = 500` (mirror companion). Target:
   stationary centred ~1000cm ≈ 34/s (~3s), walking ≈ 40/s (~2.5s).
3. **Bug 5.** (a) C++ settle persistence: re-aiming the SAME target within `ReAimSettleGrace` (default 1.5s) preserves
   `AimStartWorldTime`. (b) AI-branch single uniform cone instead of independent Yaw+Pitch (AI `else` only — never touch
   player spread). DA spread retune (SpreadStartDeg/SettleTime) = editor follow-up.
4. **Bug 6.** Diagnostics (KitFire entry CanFire/ammo/WeaponData log, throttled; player-branch miss / no-HealthComponent log)
   + add guarded `InitializeAmmo()` in `BeginPlay` only when `HasAuthority && WeaponData valid && CurrentAmmo==0` (fixes the
   ammo-seed fork without masking; harmless if kit drives ammo). Editor handoff for the kit-weapon-wiring / collision fork.
   **RESOLVED 2026-06-15 (in-engine investigation):** the real cause was character meshes IGNORING `ECC_Visibility` (the weapon
   trace channel) — every shot passed through. Fixed with `GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block)`
   in the 3 character ctors (AEnemyCharacter / ACompanionCharacter / AExtractionPlayer). Built green; verified live in PIE:
   player fired 5 shots → enemy 83→0, hit-region bone resolves (spine_04). LOS unaffected (LOS traces ignore self+target).
   Diagnostics + ammo-seed retained (harmless, useful). Follow-up: player→companion friendly fire now possible (no FF filter on
   player weapon) — add a team filter if unwanted.
5. **Bug 7a.** Clamp standoff to box half-depth: `Standoff = min(capsuleRadius + CoverStandoffPadding, boxHalfDepthX)`;
   set `CoverStandoffPadding` default 0. Body sits flush behind the wall. "Literally inside a 30cm box" is impossible for a
   34cm capsule → widen `BP_CoverSlot.CoverBoundsBox` = editor follow-up.
6. **Bug 7b/c/d = one morale rework in `UEnemyMoraleComponent`.** (7c) when BB_HasCover, multiply suppression+flank losses
   by `InCoverMoraleProtection` (0.25). (7d) recovery grace resets only on DISCRETE losses (ally/officer death, low-HP),
   NOT on continuous per-tick suppression/flank drains, so a turtled enemy can recover; +in-cover recovery bonus (×2).
   (7d) Broken-exit hysteresis: leave Broken only when morale ≥ BrokenThreshold + `BrokenExitMargin` (15). (7b) throttle
   flank loss (apply at most every `FlankLossInterval` ~2s) and scale suppression loss by suppression01. Fallback flee
   distance reduced in `BTTask_EnemyFallback` (CoverSearchRadiusMultiplier 2.0→1.3) — WP-2b.
7. **Bug 8.** `GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;` in the enemy ctor. Visual crouch POSE
   needs an enemy AnimBP crouch state = editor follow-up (capsule resizes regardless).
8. **Bug 4.** Instrument `RequestBark` (new `enemy.DrawBarkDebug` cvar: log+on-screen+head-string for FIRED/each drop reason)
   + `TryClaimSquadBark` (4th gate). Head-tag morale+suppression+threshold overlay folded into WP-1 (Awareness). Skip the
   optional per-call-site value logging (Part B) to keep the footprint clean.

## Editor / DA / BT follow-ups (cannot be done in C++ tonight — handoff)

- **Bug 6 (decisive):** confirm in PIE which weapon `BP_FPCharacter` PrimarySlot equips and whether it routes to the C++
  path; if a pure kit BP owns damage, re-slot a project-side `AWeaponBase` child OR route the kit `Fire_HitScan` into our path.
  Also verify the enemy capsule/mesh blocks `ECC_Visibility`.
- **Bug 8 visual:** add a crouch state/pose to the enemy AnimBP (else the body stays standing while the capsule shrinks).
- **Bug 7a literal:** widen `BP_CoverSlot.CoverBoundsBox` X extent (>34cm) if the body must be fully inside the box.
- **Bugs 1/2 verify:** confirm `BT_EnemyCombat_Grunt` / `BT_EnemyBase` Combat decorator (abort Both) behaviour (the C++ fix is
  designed to not depend on it, but verify).
- **Bugs 3/5/7 tuning:** the `.h` defaults may be overridden by the DA assets — confirm/adjust DA_Enemy_* values
  (FullFillRange, StillFillFactor, SpreadStartDeg/SettleTime, MoraleEventResistance) to taste.

## File ownership (disjoint — safe to implement in parallel)

- WP-1: EnemyAwarenessComponent.{h,cpp}, EnemyAIController.{h,cpp}, EnemyArchetypeData.h
- WP-2a: BTTask_EnemyCombatFire.{h,cpp}, EnemyCharacter.{h,cpp}, WeaponBase.{h,cpp}, BTService_EnemyCombat.{h,cpp}
- WP-2b: BTTask_EnemyMoveToCover.{h,cpp}, BTTask_EnemyFallback.{h,cpp}, AICoverSlot.{h,cpp}
- WP-3: EnemyMoraleComponent.{h,cpp}
- WP-4: BarkSubsystem.{h,cpp}, Debug/EnemyDebug.{h,cpp}, Squad/EnemySquad.cpp
