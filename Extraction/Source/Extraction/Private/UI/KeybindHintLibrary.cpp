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

	/** GetDisplayName(false) has no short form for mouse buttons at all — it returns the same long
	 *  string ("Middle Mouse Button") regardless of the bLongDisplayName argument, because the engine
	 *  never registered one. A keycap is a fixed ~48x24 box, not a sentence, so the keys that can
	 *  realistically end up on one get a hand-authored short form here, applied after the engine
	 *  lookup. Keyed on the FKey, never the localised string returned above — matching the string
	 *  would silently stop working the day this game ships in a second language. */
	const TMap<FKey, FText>& KeycapShortNames()
	{
		static const TMap<FKey, FText> ShortNames = {
			{ EKeys::LeftMouseButton,   NSLOCTEXT("KeybindHint", "LMB", "LMB") },
			{ EKeys::RightMouseButton,  NSLOCTEXT("KeybindHint", "RMB", "RMB") },
			{ EKeys::MiddleMouseButton, NSLOCTEXT("KeybindHint", "MMB", "MMB") },
			{ EKeys::ThumbMouseButton,  NSLOCTEXT("KeybindHint", "MB4", "MB4") },
			{ EKeys::ThumbMouseButton2, NSLOCTEXT("KeybindHint", "MB5", "MB5") },
			{ EKeys::MouseScrollUp,     NSLOCTEXT("KeybindHint", "WheelUp", "WHEEL UP") },
			{ EKeys::MouseScrollDown,   NSLOCTEXT("KeybindHint", "WheelDn", "WHEEL DN") },
		};
		return ShortNames;
	}

	/** Short display name where a key has one ("A" rather than "Gamepad Face Button Bottom"), falling
	 *  back to the long name where it does not, then to the keycap override table above for the keys
	 *  the engine never gave a short name at all.
	 *
	 *  A key with no registered details at all still has an FName, and showing that beats showing a
	 *  blank: the whole contract of this file is that a hint is never rendered with a hole in it. */
	FText KeyToDisplayText(const FKey& Key)
	{
		const FText Display = Key.GetDisplayName(/*bLongDisplayName=*/false);
		if (!Display.IsEmptyOrWhitespace())
		{
			if (const FText* ShortName = KeycapShortNames().Find(Key)) return *ShortName;
			return Display;
		}

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
