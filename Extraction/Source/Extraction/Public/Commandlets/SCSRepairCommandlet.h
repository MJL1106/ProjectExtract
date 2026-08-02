// USCSRepairCommandlet — one-off editor maintenance tool that repairs corrupt Simple Construction
// Script data in /Game Blueprints.
//
// The defect: programmatically authored Blueprints ended up with USCS_Nodes that are BOTH a child in
// another same-SCS node's ChildNodes array AND carry ParentComponentOrVariableName set to that same
// parent's variable name. That name field is only meant to be set when the parent is a NATIVE
// component or a node inherited from a super Blueprint's SCS. Combined with an AllNodes array that
// lists the child before its parent, USimpleConstructionScript::FixupSceneNodeHierarchy() trips an
// ensure on load, which logs at Error verbosity and makes the Cook commandlet exit non-zero.
//
// The repair clears the redundant name fields, which is on its own enough to silence the ensure.
// Rewriting AllNodes parent-first is opt-in behind -FixOrder: child-before-parent ordering is common
// and harmless while the name field is None, so touching every Blueprint that has it is pure churn.
// Run with -DryRun to preview. Editor-only; there is nothing to do in a cooked build.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "SCSRepairCommandlet.generated.h"

/** Log category for the SCS repair pass. */
DECLARE_LOG_CATEGORY_EXTERN(LogSCSRepair, Log, All);

UCLASS()
class EXTRACTION_API USCSRepairCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USCSRepairCommandlet();

	//~ Begin UCommandlet interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet interface
};
