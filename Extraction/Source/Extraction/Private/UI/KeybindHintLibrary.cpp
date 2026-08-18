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
	/** How informative an answer is. The mapping table is rebuilt ASYNCHRONOUSLY after every
	 *  AddMappingContext/RemoveMappingContext — and the companion mode-select and takedown-prompt
	 *  contexts come and go constantly during play — so a query landing mid-rebuild reads a partial
	 *  table: fewer keys than are really bound, or none at all. That is indistinguishable at the call
	 *  site from a genuinely unbound action, and it is why hints were degrading from "3" to a gamepad
	 *  fallback (or to [unbound]) a second into the level and never recovering: a caller that polls a
	 *  bounded number of times and then stops latches whichever answer it happened to see last. */
	enum class EHintQuality : uint8 { Unbound = 0, GamepadOnly = 1, KeyboardOrMouse = 2 };

	/** Ranks exactly the way PickKeyText chooses, so the cache can never prefer a text that
	 *  PickKeyText would not itself have returned for that key set. */
	EHintQuality RankKeys(const TArray<FKey>& Keys)
	{
		EHintQuality Best = EHintQuality::Unbound;
		for (const FKey& Key : Keys)
		{
			if (!Key.IsValid()) continue;
			if (!Key.IsGamepadKey() && !Key.IsTouch()) return EHintQuality::KeyboardOrMouse;
			Best = EHintQuality::GamepadOnly;
		}
		return Best;
	}

	/** Best answer seen so far for each action. Keyed weakly so a GC'd action drops out on its own.
	 *  Values are level- and session-stable (a key binding does not change on level load), and a real
	 *  rebind still lands because it reports at the same quality and so replaces the entry. */
	TMap<TWeakObjectPtr<const UInputAction>, TPair<EHintQuality, FText>>& BestHintSoFar()
	{
		static TMap<TWeakObjectPtr<const UInputAction>, TPair<EHintQuality, FText>> Cache;
		return Cache;
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

	TPair<EHintQuality, FText>& Best = BestHintSoFar().FindOrAdd(Action,
		TPair<EHintQuality, FText>(EHintQuality::Unbound, GetUnboundKeyText()));

	const UEnhancedInputLocalPlayerSubsystem* Input = FindLocalPlayerInputSubsystem(WorldContextObject);
	if (!IsValid(Input)) return Best.Value;

	// The one 5.7 query for "which keys reach this action right now". It reads the rebuilt mapping
	// table, so it reflects every applied context (and any remap profile) with no knowledge here of
	// which contexts those are — see the header on why an early caller must re-ask after a rebuild.
	const TArray<FKey> Keys = Input->QueryKeysMappedToAction(Action);
	const EHintQuality Quality = RankKeys(Keys);

	// Never trade down. A mid-rebuild read is strictly less informative than a settled one, so the
	// only safe response is to keep the better answer we already proved. Equal quality still writes
	// through, so a genuine rebind is picked up.
	if (Quality < Best.Key) return Best.Value;

	Best.Key = Quality;
	Best.Value = PickKeyText(Keys);
	return Best.Value;
}
