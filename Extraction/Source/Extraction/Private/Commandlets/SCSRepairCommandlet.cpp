// USCSRepairCommandlet implementation.

#include "Commandlets/SCSRepairCommandlet.h"

#include "Misc/Parse.h"

#if WITH_EDITOR
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY(LogSCSRepair);

/** Every log line carries this so the pass is greppable in a cook/commandlet log. */
static const TCHAR* const SCSRepairLogPrefix = TEXT("[SCSRepair]");

#if WITH_EDITOR

namespace SCSRepair
{
	/** Asset registry scan root — a search path, not an asset reference. */
	static const TCHAR* const ContentRoot = TEXT("/Game");

	/** Blueprints loaded between garbage collection passes, so a big project does not blow up memory. */
	static constexpr int32 GarbageCollectInterval = 64;

	/** What a single Blueprint needed fixing. */
	struct FRepairReport
	{
		/** Variable names of the nodes whose redundant parent name fields were cleared. */
		TArray<FName> ClearedNodeNames;

		/** True when AllNodes was out of parent-first order and got rewritten. */
		bool bReorderedAllNodes = false;

		bool IsEmpty() const { return ClearedNodeNames.Num() == 0 && !bReorderedAllNodes; }
	};

	/** Depth-first walk of a node and its descendants, emitting every node before its children. */
	static void CollectNodes(USCS_Node* Node, TSet<USCS_Node*>& Visited, TArray<USCS_Node*>& OutParentFirst)
	{
		if (!IsValid(Node)) return;

		bool bAlreadyVisited = false;
		Visited.Add(Node, &bAlreadyVisited);
		if (bAlreadyVisited) return;

		OutParentFirst.Add(Node);
		for (USCS_Node* Child : Node->GetChildNodes())
		{
			CollectNodes(Child, Visited, OutParentFirst);
		}
	}

	/** Builds the parent-first node order for a whole SCS. */
	static void MapSCS(USimpleConstructionScript* SCS, TArray<USCS_Node*>& OutParentFirst)
	{
		const TArray<USCS_Node*>& AllNodes = SCS->GetAllNodes();
		OutParentFirst.Reserve(AllNodes.Num());

		TSet<USCS_Node*> Visited;
		Visited.Reserve(AllNodes.Num());

		for (USCS_Node* RootNode : SCS->GetRootNodes())
		{
			CollectNodes(RootNode, Visited, OutParentFirst);
		}

		CollectNodes(SCS->GetDefaultSceneRootNode(), Visited, OutParentFirst);

		// Anything the root set does not reach still has to keep a slot in AllNodes.
		for (USCS_Node* Node : AllNodes)
		{
			CollectNodes(Node, Visited, OutParentFirst);
		}
	}

	/**
	 * The actual repair: a node whose owning parent lives in this same SCS must express that only via
	 * ChildNodes, never via ParentComponentOrVariableName. Walking parent -> child edges rather than
	 * node -> first-parent mirrors the engine ensure exactly, so a node listed under two parents is
	 * caught whichever of the two its name points at.
	 */
	static void ClearRedundantParentNames(const TArray<USCS_Node*>& ParentFirstOrder, const bool bDryRun, FRepairReport& Report)
	{
		for (const USCS_Node* Parent : ParentFirstOrder)
		{
			const FName ParentVariableName = Parent->GetVariableName();

			for (USCS_Node* Child : Parent->GetChildNodes())
			{
				if (!IsValid(Child)) continue;
				if (Child->ParentComponentOrVariableName.IsNone()) continue;
				if (Child->ParentComponentOrVariableName != ParentVariableName) continue;

				Report.ClearedNodeNames.Add(Child->GetVariableName());
				if (bDryRun) continue;

				Child->Modify();
				Child->ParentComponentOrVariableName = NAME_None;
				Child->ParentComponentOwnerClassName = NAME_None;
				Child->bIsParentComponentNative = false;
			}
		}
	}

	/**
	 * AllNodes is a private UPROPERTY and FSCSAllNodesHelper::Add/Remove are private to USCS_Node, so
	 * the reorder goes through the reflected property. Returns null if the layout is not what we expect.
	 */
	static TArray<TObjectPtr<USCS_Node>>* FindMutableAllNodes(USimpleConstructionScript* SCS)
	{
		static const FName AllNodesPropertyName(TEXT("AllNodes"));

		FArrayProperty* AllNodesProperty = FindFProperty<FArrayProperty>(USimpleConstructionScript::StaticClass(), AllNodesPropertyName);
		if (!AllNodesProperty) return nullptr;

		// Exact class, not FObjectPropertyBase: a weak-pointer array would clear both the class and the
		// size check and then get reinterpreted as strong pointers.
		const FObjectProperty* InnerProperty = CastField<FObjectProperty>(AllNodesProperty->Inner);
		if (!InnerProperty || InnerProperty->PropertyClass != USCS_Node::StaticClass()) return nullptr;
		if (InnerProperty->GetElementSize() != sizeof(TObjectPtr<USCS_Node>)) return nullptr;

		return AllNodesProperty->ContainerPtrToValuePtr<TArray<TObjectPtr<USCS_Node>>>(SCS);
	}

	/** Rewrites AllNodes so no child ever precedes its parent. Never adds or drops entries. */
	static void NormaliseAllNodesOrder(USimpleConstructionScript* SCS, const TArray<USCS_Node*>& ParentFirstOrder, const bool bDryRun, FRepairReport& Report)
	{
		TArray<TObjectPtr<USCS_Node>>* AllNodes = FindMutableAllNodes(SCS);
		if (!AllNodes)
		{
			UE_LOG(LogSCSRepair, Warning, TEXT("%s AllNodes is not reachable through reflection - skipping the ordering pass."), SCSRepairLogPrefix);
			return;
		}

		TSet<USCS_Node*> Existing;
		Existing.Reserve(AllNodes->Num());
		for (const TObjectPtr<USCS_Node>& Node : *AllNodes)
		{
			Existing.Add(Node.Get());
		}

		// The tree walk also reaches nodes AllNodes deliberately does not hold - a DefaultSceneRootNode
		// superseded by a real scene root stays non-null but is dropped from the array - so filter down
		// to what is actually in there rather than demanding the two lists match.
		TArray<USCS_Node*> NewOrder;
		NewOrder.Reserve(AllNodes->Num());
		for (USCS_Node* Node : ParentFirstOrder)
		{
			if (Existing.Contains(Node))
			{
				NewOrder.Add(Node);
			}
		}

		// Anything else (a null slot, a duplicate entry) is not a reorder problem - leave it be.
		if (NewOrder.Num() != AllNodes->Num())
		{
			UE_LOG(LogSCSRepair, Warning, TEXT("%s '%s' has AllNodes entries the node tree does not account for - leaving the order alone."), SCSRepairLogPrefix, *SCS->GetPathName());
			return;
		}

		bool bOrderDiffers = false;
		for (int32 Index = 0; !bOrderDiffers && Index < NewOrder.Num(); ++Index)
		{
			bOrderDiffers = (*AllNodes)[Index] != NewOrder[Index];
		}
		if (!bOrderDiffers) return;

		Report.bReorderedAllNodes = true;
		if (bDryRun) return;

		SCS->Modify();
		AllNodes->Reset(NewOrder.Num());
		for (USCS_Node* Node : NewOrder)
		{
			AllNodes->Add(Node);
		}
	}

	/**
	 * Inspects one Blueprint and, unless this is a dry run, applies the repair in memory.
	 * The ordering pass is opt-in: child-before-parent in AllNodes is common and harmless on its own,
	 * so rewriting every Blueprint that has it is pure churn.
	 */
	static FRepairReport RepairBlueprint(UBlueprint* Blueprint, const bool bDryRun, const bool bFixOrder)
	{
		FRepairReport Report;

		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		if (!IsValid(SCS)) return Report;

		TArray<USCS_Node*> ParentFirstOrder;
		MapSCS(SCS, ParentFirstOrder);

		ClearRedundantParentNames(ParentFirstOrder, bDryRun, Report);

		if (bFixOrder)
		{
			NormaliseAllNodesOrder(SCS, ParentFirstOrder, bDryRun, Report);
		}
		return Report;
	}

	/** Every /Game Blueprint the asset registry knows about, subclasses included. */
	static void GatherBlueprints(TArray<FAssetData>& OutAssets)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.SearchAllAssets(/*bSynchronousSearch =*/true);

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.PackagePaths.Add(FName(ContentRoot));
		Filter.bRecursivePaths = true;

		AssetRegistry.GetAssets(Filter, OutAssets);
	}

	static void LogReport(const FName PackageName, const FRepairReport& Report)
	{
		if (Report.ClearedNodeNames.Num() > 0)
		{
			TArray<FString> NodeNames;
			NodeNames.Reserve(Report.ClearedNodeNames.Num());
			for (const FName NodeName : Report.ClearedNodeNames)
			{
				NodeNames.Add(NodeName.ToString());
			}

			UE_LOG(LogSCSRepair, Display, TEXT("%s %s: cleared same-SCS parent name on %d node(s): %s"),
				SCSRepairLogPrefix, *PackageName.ToString(), Report.ClearedNodeNames.Num(), *FString::Join(NodeNames, TEXT(", ")));
		}

		if (Report.bReorderedAllNodes)
		{
			UE_LOG(LogSCSRepair, Display, TEXT("%s %s: rewrote AllNodes parent-first."), SCSRepairLogPrefix, *PackageName.ToString());
		}
	}

	/** Marks the package dirty and writes it back to disk. */
	static bool SaveRepairedBlueprint(UBlueprint* Blueprint)
	{
		UPackage* Package = Blueprint->GetOutermost();
		if (!IsValid(Package)) return false;

		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
		{
			UE_LOG(LogSCSRepair, Error, TEXT("%s No filename for package '%s' - not saved."), SCSRepairLogPrefix, *Package->GetName());
			return false;
		}

		if (IFileManager::Get().IsReadOnly(*Filename))
		{
			UE_LOG(LogSCSRepair, Error, TEXT("%s '%s' is read-only - check it out and re-run."), SCSRepairLogPrefix, *Filename);
			return false;
		}

		Blueprint->Modify();
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		SaveArgs.bSlowTask = false;

		// The default is GError, which raises a structured exception for anything routed to it regardless
		// of verbosity - a save diagnostic would abort the process instead of returning false.
		SaveArgs.Error = GWarn;

		if (UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs)) return true;

		UE_LOG(LogSCSRepair, Error, TEXT("%s Failed to save '%s'."), SCSRepairLogPrefix, *Filename);
		return false;
	}
}

#endif // WITH_EDITOR

USCSRepairCommandlet::USCSRepairCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;

	// Loading the corrupt Blueprints trips the very ensure this pass exists to silence, so the engine's
	// "any error logged means exit 1" rule would report failure on a perfectly good repair run.
	UseCommandletResultAsExitCode = true;
}

int32 USCSRepairCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bDryRun = FParse::Param(*Params, TEXT("DryRun"));
	const bool bFixOrder = FParse::Param(*Params, TEXT("FixOrder"));

	TArray<FAssetData> BlueprintAssets;
	SCSRepair::GatherBlueprints(BlueprintAssets);

	UE_LOG(LogSCSRepair, Display, TEXT("%s Scanning %d Blueprint asset(s) under %s (DryRun=%s, FixOrder=%s)."),
		SCSRepairLogPrefix, BlueprintAssets.Num(), SCSRepair::ContentRoot,
		bDryRun ? TEXT("true") : TEXT("false"), bFixOrder ? TEXT("true") : TEXT("false"));

	int32 ScannedCount = 0;
	int32 RepairedCount = 0;
	int32 SavedCount = 0;
	int32 FailedCount = 0;
	int32 LoadedSinceCollect = 0;

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		if (++LoadedSinceCollect >= SCSRepair::GarbageCollectInterval)
		{
			LoadedSinceCollect = 0;
			CollectGarbage(RF_NoFlags);
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!IsValid(Blueprint)) continue;

		++ScannedCount;

		const SCSRepair::FRepairReport Report = SCSRepair::RepairBlueprint(Blueprint, bDryRun, bFixOrder);
		if (Report.IsEmpty()) continue;

		++RepairedCount;
		SCSRepair::LogReport(AssetData.PackageName, Report);

		if (bDryRun) continue;

		if (SCSRepair::SaveRepairedBlueprint(Blueprint))
		{
			++SavedCount;
		}
		else
		{
			++FailedCount;
		}
	}

	UE_LOG(LogSCSRepair, Display, TEXT("%s Done. Scanned %d, repaired %d, saved %d, failed %d.%s"),
		SCSRepairLogPrefix, ScannedCount, RepairedCount, SavedCount, FailedCount,
		bDryRun ? TEXT(" (dry run - nothing was written)") : TEXT(""));

	return FailedCount > 0 ? 1 : 0;
#else
	UE_LOG(LogSCSRepair, Error, TEXT("%s This commandlet is editor-only."), SCSRepairLogPrefix);
	return 1;
#endif // WITH_EDITOR
}
