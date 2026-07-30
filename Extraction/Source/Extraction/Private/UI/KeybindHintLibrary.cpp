// UKeybindHintLibrary implementation.

#include "UI/KeybindHintLibrary.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Single-player: player 0 is the only local player there will ever be. */
	constexpr int32 HintLocalPlayerIndex = 0;

	/** Short display name where a key has one ("A" rather than "Gamepad Face Button Bottom"), falling
	 *  back to the long name where it does not — mouse buttons register no short name, so middle mouse
	 *  still reads as "Middle Mouse Button".
	 *
	 *  A key with no registered details at all still has an FName, and showing that beats showing a
	 *  blank: the whole contract of this file is that a hint is never rendered with a hole in it. */
	FText KeyToDisplayText(const FKey& Key)
	{
		const FText Display = Key.GetDisplayName(/*bLongDisplayName=*/false);
		if (!Display.IsEmptyOrWhitespace()) return Display;

		const FName KeyName = Key.GetFName();
		if (KeyName.IsNone()) return UKeybindHintLibrary::GetUnboundKeyText();
		return FText::FromName(KeyName);
	}
}

FText UKeybindHintLibrary::GetUnboundKeyText()
{
	return NSLOCTEXT("KeybindHint", "UnboundKey", "[unbound]");
}

FText UKeybindHintLibrary::PickKeyText(const TArray<FKey>& Keys)
{
	// See the header for the rule this implements. Keyboard/mouse wins outright; the first non-KBM key
	// is only held as a fallback so a pad-only binding still names something.
	const FKey* NonKeyboardFallback = nullptr;
	for (const FKey& Key : Keys)
	{
		if (!Key.IsValid()) continue;
		if (!Key.IsGamepadKey() && !Key.IsTouch()) return KeyToDisplayText(Key);
		if (!NonKeyboardFallback) NonKeyboardFallback = &Key;
	}

	if (NonKeyboardFallback) return KeyToDisplayText(*NonKeyboardFallback);
	return GetUnboundKeyText();
}

UEnhancedInputLocalPlayerSubsystem* UKeybindHintLibrary::FindLocalPlayerInputSubsystem(const UObject* WorldContextObject)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, HintLocalPlayerIndex);
	if (!IsValid(PC)) return nullptr;

	return ULocalPlayer::GetSubsystemFromController<UEnhancedInputLocalPlayerSubsystem>(PC);
}

FText UKeybindHintLibrary::GetActionKeyText(const UObject* WorldContextObject, const UInputAction* Action)
{
	if (!IsValid(Action)) return GetUnboundKeyText();

	const UEnhancedInputLocalPlayerSubsystem* Input = FindLocalPlayerInputSubsystem(WorldContextObject);
	if (!IsValid(Input)) return GetUnboundKeyText();

	// The one 5.7 query for "which keys reach this action right now". It reads the rebuilt mapping
	// table, so it reflects every applied context (and any remap profile) with no knowledge here of
	// which contexts those are — see the header on why an early caller must re-ask after a rebuild.
	return PickKeyText(Input->QueryKeysMappedToAction(Action));
}
