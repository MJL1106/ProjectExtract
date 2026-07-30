// Automation coverage for the keybind-hint fallbacks and the multi-binding arbitration rule, plus the
// objective-label substitution that sits on top of them. IA_CompanionBreach is bound to both B and I,
// so "which key wins" is a real answer the HUD depends on rather than a hypothetical.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/KeybindHintLibrary.h"
#include "World/ObjectiveStep.h"

namespace KeybindHintTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::ProductFilter;

	/** Compare against the engine's own display string rather than a hardcoded letter — these tests are
	 *  about WHICH key the rule picks, not about how the engine spells it. */
	FString Display(const FKey& Key) { return Key.GetDisplayName(/*bLongDisplayName=*/false).ToString(); }

	FString Unbound() { return UKeybindHintLibrary::GetUnboundKeyText().ToString(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeybindHintFallbackTest,
	"Extraction.UI.KeybindHint.FallbackNeverEmpty", KeybindHintTests::TestFlags)

bool FKeybindHintFallbackTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("the unbound fallback is not empty"), KeybindHintTests::Unbound().IsEmpty());

	TestEqual(TEXT("an action with no bindings falls back"),
		UKeybindHintLibrary::PickKeyText(TArray<FKey>()).ToString(), KeybindHintTests::Unbound());

	// A key list made entirely of unset slots is the same answer as no list at all.
	TestEqual(TEXT("invalid keys alone fall back"),
		UKeybindHintLibrary::PickKeyText({ FKey(), FKey() }).ToString(), KeybindHintTests::Unbound());

	// The null-action guard runs before any world lookup, so this path is safe with no context at all —
	// which is exactly the state a HUD widget can be in before the level is up.
	TestEqual(TEXT("a null action falls back"),
		UKeybindHintLibrary::GetActionKeyText(nullptr, nullptr).ToString(), KeybindHintTests::Unbound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeybindHintArbitrationTest,
	"Extraction.UI.KeybindHint.FirstKeyboardOrMouseBindingWins", KeybindHintTests::TestFlags)

bool FKeybindHintArbitrationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a single binding is reported"),
		UKeybindHintLibrary::PickKeyText({ EKeys::B }).ToString(), KeybindHintTests::Display(EKeys::B));

	// The IA_CompanionBreach case: two equally valid keyboard bindings, and the answer must be stable.
	TestEqual(TEXT("the first of two keyboard bindings wins"),
		UKeybindHintLibrary::PickKeyText({ EKeys::B, EKeys::I }).ToString(), KeybindHintTests::Display(EKeys::B));
	TestEqual(TEXT("order is the rule, not the alphabet"),
		UKeybindHintLibrary::PickKeyText({ EKeys::I, EKeys::B }).ToString(), KeybindHintTests::Display(EKeys::I));

	// Mouse counts as keyboard-or-mouse — the ping is on middle mouse.
	TestEqual(TEXT("a mouse binding is reported"),
		UKeybindHintLibrary::PickKeyText({ EKeys::MiddleMouseButton }).ToString(),
		KeybindHintTests::Display(EKeys::MiddleMouseButton));

	// A pad binding listed first must not beat a keyboard binding listed later: the HUD is a
	// mouse-and-keyboard prompt.
	TestEqual(TEXT("keyboard beats an earlier gamepad binding"),
		UKeybindHintLibrary::PickKeyText({ EKeys::Gamepad_FaceButton_Bottom, EKeys::B }).ToString(),
		KeybindHintTests::Display(EKeys::B));

	// An unset slot is skipped rather than ending the search.
	TestEqual(TEXT("an invalid key is skipped"),
		UKeybindHintLibrary::PickKeyText({ FKey(), EKeys::B }).ToString(), KeybindHintTests::Display(EKeys::B));

	// Pad-only is still better information than "unbound".
	const FString PadOnly = UKeybindHintLibrary::PickKeyText({ EKeys::Gamepad_FaceButton_Bottom }).ToString();
	TestFalse(TEXT("a pad-only binding still names a key"), PadOnly.IsEmpty());
	TestFalse(TEXT("a pad-only binding is not treated as unbound"), PadOnly == KeybindHintTests::Unbound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeybindHintObjectiveLabelTest,
	"Extraction.UI.KeybindHint.ObjectiveLabelSubstitution", KeybindHintTests::TestFlags)

bool FKeybindHintObjectiveLabelTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AObjectiveStep* Step = IsValid(World) ? World->SpawnActor<AObjectiveStep>() : nullptr;
	TestNotNull(TEXT("test step spawned"), Step);
	if (!Step) return false;

	// A test world has no local player, so every resolution here lands on the fallback — which is the
	// case worth asserting: the substitution must still happen.
	UInputAction* Action = NewObject<UInputAction>();
	TestEqual(TEXT("a valid action with no local player falls back"),
		UKeybindHintLibrary::GetActionKeyText(World, Action).ToString(), KeybindHintTests::Unbound());

	// No hint wired and no token authored: the label must come back byte-identical, braces included.
	// This is what keeps FText::Format away from labels that were never meant for it.
	const FText Untouched = FText::FromString(TEXT("Recover {0} of 3 crates"));
	Step->TestSetLabel(Untouched);
	TestEqual(TEXT("a hintless label is not reformatted"),
		Step->TestBuildDisplayLabel().ToString(), Untouched.ToString());

	// A token with an action wired: the fallback substitutes, so no brace ever reaches the HUD.
	Step->TestSetLabel(FText::FromString(TEXT("Breach the roof door ({PromptKey} to mark it)")));
	Step->TestSetPromptAction(Action);
	const FString Resolved = Step->TestBuildDisplayLabel().ToString();
	TestTrue(TEXT("the unresolved key becomes the fallback"), Resolved.Contains(KeybindHintTests::Unbound()));
	TestFalse(TEXT("no raw token survives to the HUD"), Resolved.Contains(TEXT("{PromptKey}")));

	// The same must hold for a token authored with NOTHING assigned — the mis-wire the audit warns
	// about must still not render a literal brace.
	AObjectiveStep* Unwired = World->SpawnActor<AObjectiveStep>();
	TestNotNull(TEXT("second test step spawned"), Unwired);
	if (!Unwired) return false;

	Unwired->TestSetLabel(FText::FromString(TEXT("Breach the door ({PromptKey}, then {SecondaryPromptKey})")));
	const FString UnwiredText = Unwired->TestBuildDisplayLabel().ToString();
	TestFalse(TEXT("an unwired primary token does not leak"), UnwiredText.Contains(TEXT("{PromptKey}")));
	TestFalse(TEXT("an unwired secondary token does not leak"), UnwiredText.Contains(TEXT("{SecondaryPromptKey}")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
