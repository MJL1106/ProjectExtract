// UKeybindHintLibrary — resolves a UInputAction to the key the local player actually has bound, as
// display text for a HUD prompt. The point is that no prompt string in the game hardcodes a letter:
// a hint is authored as a token, and the key is substituted at display time from the live mapping.
//
// Asset-agnostic by construction: it never names an input action or a mapping context, it only
// answers "what is bound to THIS action right now". Callers hold the UInputAction reference (a
// designer-assigned UPROPERTY), so nothing here needs a /Game/ path.
//
// Timing note, and it is load-bearing: AddMappingContext does NOT rebuild the mapping table
// immediately — IEnhancedInputSubsystemInterface::RequestRebuildControlMappings only rebuilds inline
// when FModifyContextOptions::bForceImmediately is set, and nothing in this project sets it. Since
// QueryKeysMappedToAction reads the BUILT table, a query issued during world BeginPlay (before the
// first Enhanced Input tick) legitimately returns nothing. A caller that wants a correct first frame
// must re-resolve on UEnhancedInputLocalPlayerSubsystem::ControlMappingsRebuiltDelegate rather than
// trust its BeginPlay-time answer.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KeybindHintLibrary.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;

UCLASS()
class EXTRACTION_API UKeybindHintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Display text for the key currently bound to Action for the local player. Never empty — an
	 *  unassigned action, an unbound action, or no local player all return GetUnboundKeyText(), so a
	 *  caller substituting this into a prompt can never render a hint with a hole in it. */
	UFUNCTION(BlueprintPure, Category = "UI|Keybind Hint", meta = (WorldContext = "WorldContextObject"))
	static FText GetActionKeyText(const UObject* WorldContextObject, const UInputAction* Action);

	/** THE arbitration rule, in one place, for when several keys are mapped to one action.
	 *
	 *  Rule: the first keyboard-or-mouse key in the order Enhanced Input reports them, which is
	 *  highest-priority applied mapping context first, then authoring order within that context.
	 *  IA_CompanionBreach is bound to both B and I, so "first one wins" is not hypothetical — it is
	 *  what stops the hint flapping between two equally valid answers between runs.
	 *
	 *  Gamepad and touch keys are only used when the action has NO keyboard or mouse binding at all:
	 *  a pad-only binding is still better information for the player than "unbound". */
	static FText PickKeyText(const TArray<FKey>& Keys);

	/** What a hint reads when there is no key to name. Bracketed so it is legible as a wiring fault
	 *  rather than as copy, and non-empty so it can never collapse a prompt to dangling punctuation. */
	static FText GetUnboundKeyText();

	/** The local player's Enhanced Input subsystem, or null when there is no local player yet.
	 *  Exposed so a caller that wants to follow rebinds can subscribe to
	 *  ControlMappingsRebuiltDelegate without re-deriving the lookup. */
	static UEnhancedInputLocalPlayerSubsystem* FindLocalPlayerInputSubsystem(const UObject* WorldContextObject);
};
