// FTutorialControlRow key resolution.

#include "UI/TutorialBriefingData.h"

#include "InputAction.h"

#include "UI/KeybindHintLibrary.h"

FText FTutorialControlRow::ResolveKeyText(const UObject* WorldContextObject) const
{
	if (IsValid(Action))
	{
		const FText Resolved = UKeybindHintLibrary::GetActionKeyText(WorldContextObject, Action);
		// Actions mapped inside a context that is only active situationally (e.g. takedown confirms
		// in IMC_CompanionTakedownPrompt) resolve to "[unbound]" at briefing time. When the designer
		// has also filled KeyOverride for exactly this case, fall back to it rather than showing the
		// player a placeholder. Action still wins whenever it resolves to a real key.
		if (!Resolved.EqualTo(UKeybindHintLibrary::GetUnboundKeyText())) return Resolved;
		if (!KeyOverride.IsEmptyOrWhitespace()) return KeyOverride;

		// No override to fall back to: return the "[unbound]" sentinel rather than empty text. It is
		// non-empty by design so a wiring fault reads as a visible fault instead of collapsing the key
		// block and quietly rendering a label with no key at all.
		return Resolved;
	}

	if (!KeyOverride.IsEmptyOrWhitespace()) return KeyOverride;

	// Reached only by heading rows, which carry neither an action nor an override.
	return FText::GetEmpty();
}
