// Automation coverage for UDamageMitigationSettings pure helpers and UHealthComponent gated damage.
// No engine world required — UObject allocation via NewObject<> on the transient package.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DamageMitigationSettings.h"
#include "HealthComponent.h"

// ---- RampChance01 ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRampChance01AtZeroTest, "Extraction.DamageMitigation.RampChance01.AtZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRampChance01AtZeroTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->AcquireHitChance = 0.15f;
	S->SettledHitChance = 0.9f;
	S->RampTime = 1.5f;

	const float Result = UDamageMitigationSettings::RampChance01(*S, 0.f);
	TestNearlyEqual(TEXT("t=0 returns AcquireHitChance"), Result, 0.15f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRampChance01AtSettledTest, "Extraction.DamageMitigation.RampChance01.AtSettled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRampChance01AtSettledTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->AcquireHitChance = 0.15f;
	S->SettledHitChance = 0.9f;
	S->RampTime = 1.5f;

	const float AtRamp = UDamageMitigationSettings::RampChance01(*S, 1.5f);
	TestNearlyEqual(TEXT("t=RampTime returns SettledHitChance"), AtRamp, 0.9f, 0.001f);

	const float BeyondRamp = UDamageMitigationSettings::RampChance01(*S, 10.f);
	TestNearlyEqual(TEXT("t>>RampTime clamps to SettledHitChance"), BeyondRamp, 0.9f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRampChance01MonotonicTest, "Extraction.DamageMitigation.RampChance01.Monotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRampChance01MonotonicTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->AcquireHitChance = 0.1f;
	S->SettledHitChance = 0.95f;
	S->RampTime = 2.f;

	float Prev = UDamageMitigationSettings::RampChance01(*S, 0.f);
	constexpr int32 Steps = 20;
	for (int32 i = 1; i <= Steps; ++i)
	{
		const float T = S->RampTime * static_cast<float>(i) / static_cast<float>(Steps);
		const float Cur = UDamageMitigationSettings::RampChance01(*S, T);
		TestTrue(FString::Printf(TEXT("monotonic at step %d (%.4f >= %.4f)"), i, Cur, Prev), Cur >= Prev);
		Prev = Cur;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRampChance01ClampsNegativeTest, "Extraction.DamageMitigation.RampChance01.ClampsNegative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRampChance01ClampsNegativeTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->AcquireHitChance = 0.2f;
	S->SettledHitChance = 0.8f;
	S->RampTime = 1.f;

	const float Result = UDamageMitigationSettings::RampChance01(*S, -5.f);
	TestNearlyEqual(TEXT("negative time clamps to AcquireHitChance"), Result, 0.2f, 0.001f);
	return true;
}

// ---- TryConsumeGatedDamage ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGatedDamageBasicTest, "Extraction.DamageMitigation.GatedDamage.BasicCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGatedDamageBasicTest::RunTest(const FString& Parameters)
{
	UHealthComponent* HC = NewObject<UHealthComponent>();

	TestTrue(TEXT("first call always succeeds"), HC->TryConsumeGatedDamage(1.0f, 0.5f));
	TestFalse(TEXT("second call within window is blocked"), HC->TryConsumeGatedDamage(1.2f, 0.5f));
	TestTrue(TEXT("call after window passes succeeds"), HC->TryConsumeGatedDamage(1.6f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGatedDamageMultiAttackerTest, "Extraction.DamageMitigation.GatedDamage.AttackerAgnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGatedDamageMultiAttackerTest::RunTest(const FString& Parameters)
{
	UHealthComponent* HC = NewObject<UHealthComponent>();

	TestTrue(TEXT("attacker A first hit succeeds"), HC->TryConsumeGatedDamage(0.f, 0.5f));
	// Simulating a different attacker hitting the same victim within the window.
	TestFalse(TEXT("attacker B within window is still blocked (per-victim, not per-attacker)"), HC->TryConsumeGatedDamage(0.3f, 0.5f));
	return true;
}

// ---- EffectiveResetGap ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEffectiveGapSlowWeaponTest, "Extraction.DamageMitigation.EffectiveGap.SlowWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEffectiveGapSlowWeaponTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->RampResetGapSeconds = 0.6f;
	S->ShotGapResetMultiplier = 2.5f;

	// Slow weapon: 1 RPS = 1 second between shots. Effective gap = max(0.6, 2.5 * 1.0) = 2.5
	const float SlowGap = UDamageMitigationSettings::EffectiveResetGap(*S, 1.f);
	TestNearlyEqual(TEXT("slow weapon gap = multiplier * seconds-per-shot"), SlowGap, 2.5f, 0.001f);

	// The gap must exceed the weapon's shot interval so the ramp doesn't reset every shot.
	const float ShotInterval = 1.f / 1.f; // 1 RPS = 1s interval
	TestTrue(TEXT("slow weapon gap exceeds its shot interval"), SlowGap > ShotInterval);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEffectiveGapFastWeaponTest, "Extraction.DamageMitigation.EffectiveGap.FastWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEffectiveGapFastWeaponTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->RampResetGapSeconds = 0.6f;
	S->ShotGapResetMultiplier = 2.5f;

	// Fast weapon: 10 RPS = 0.1s between shots. Effective gap = max(0.6, 2.5 * 0.1) = 0.6
	const float FastGap = UDamageMitigationSettings::EffectiveResetGap(*S, 10.f);
	TestNearlyEqual(TEXT("fast weapon gap = floor (RampResetGapSeconds)"), FastGap, 0.6f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEffectiveGapSniperTest, "Extraction.DamageMitigation.EffectiveGap.SniperNeverResets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEffectiveGapSniperTest::RunTest(const FString& Parameters)
{
	UDamageMitigationSettings* S = NewObject<UDamageMitigationSettings>();
	S->RampResetGapSeconds = 0.6f;
	S->ShotGapResetMultiplier = 2.5f;

	// Sniper: 0.5 RPS = 2s between shots. Effective gap = max(0.6, 2.5 * 2.0) = 5.0
	const float SniperGap = UDamageMitigationSettings::EffectiveResetGap(*S, 0.5f);
	const float SniperShotInterval = 1.f / 0.5f; // 2s
	TestTrue(TEXT("sniper gap exceeds its shot interval"), SniperGap > SniperShotInterval);
	TestNearlyEqual(TEXT("sniper gap value"), SniperGap, 5.f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
