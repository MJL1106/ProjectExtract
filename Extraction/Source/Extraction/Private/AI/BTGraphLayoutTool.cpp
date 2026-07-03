#include "AI/BTGraphLayoutTool.h"

#if WITH_EDITOR

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FLayoutMove
	{
		FString NodeName;
		int32 CurX = MAX_int32;   // MAX_int32 = no current-position match required
		int32 CurY = MAX_int32;
		int32 NewX = 0;
		int32 NewY = 0;
		bool bApplied = false;
	};

	// "NodeName@curX,curY->newX,newY" or "NodeName->newX,newY"
	bool ParseMove(const FString& Token, FLayoutMove& Out)
	{
		FString Left, Right;
		if (!Token.Split(TEXT("->"), &Left, &Right)) return false;

		FString CurPart;
		if (Left.Split(TEXT("@"), &Out.NodeName, &CurPart))
		{
			FString CX, CY;
			if (!CurPart.Split(TEXT(","), &CX, &CY)) return false;
			Out.CurX = FCString::Atoi(*CX);
			Out.CurY = FCString::Atoi(*CY);
		}
		else
		{
			Out.NodeName = Left;
		}
		Out.NodeName.TrimStartAndEndInline();

		FString NX, NY;
		if (!Right.Split(TEXT(","), &NX, &NY)) return false;
		Out.NewX = FCString::Atoi(*NX);
		Out.NewY = FCString::Atoi(*NY);
		return true;
	}

	// The graph nodes are UBehaviorTreeGraphNode (BehaviorTreeEditor module) — read NodeInstance via
	// reflection to avoid an editor-module dependency; the instance itself is a UBTNode (AIModule).
	const UBTNode* GetNodeInstance(const UEdGraphNode* GraphNode)
	{
		const FObjectProperty* InstProp = FindFProperty<FObjectProperty>(GraphNode->GetClass(), TEXT("NodeInstance"));
		return InstProp ? Cast<UBTNode>(InstProp->GetObjectPropertyValue_InContainer(GraphNode)) : nullptr;
	}
}

int32 UBTGraphLayoutTool::ApplyBTGraphLayout(const FString& TreeAssetPath, const FString& LayoutSpec)
{
	UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *TreeAssetPath);
	if (!Tree || !Tree->BTGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyBTGraphLayout: no BT/graph at '%s'"), *TreeAssetPath);
		return 0;
	}

	TArray<FString> Tokens;
	LayoutSpec.ParseIntoArray(Tokens, TEXT(";"), true);

	TArray<FLayoutMove> Moves;
	Moves.Reserve(Tokens.Num());
	for (const FString& Token : Tokens)
	{
		FLayoutMove Move;
		if (ParseMove(Token, Move)) Moves.Add(Move);
		else UE_LOG(LogTemp, Warning, TEXT("ApplyBTGraphLayout: bad token '%s'"), *Token);
	}

	int32 TotalMoved = 0;
	for (UEdGraphNode* GraphNode : Tree->BTGraph->Nodes)
	{
		if (!GraphNode) continue;
		const UBTNode* Inst = GetNodeInstance(GraphNode);
		if (!Inst) continue;

		for (FLayoutMove& Move : Moves)
		{
			if (Move.bApplied || Inst->NodeName != Move.NodeName) continue;
			if (Move.CurX != MAX_int32 && (GraphNode->NodePosX != Move.CurX || GraphNode->NodePosY != Move.CurY)) continue;

			GraphNode->Modify();
			GraphNode->NodePosX = Move.NewX;
			GraphNode->NodePosY = Move.NewY;
			Move.bApplied = true;
			++TotalMoved;
			UE_LOG(LogTemp, Log, TEXT("ApplyBTGraphLayout: '%s' -> (%d, %d)"), *Move.NodeName, Move.NewX, Move.NewY);
			break;
		}
	}

	for (const FLayoutMove& Move : Moves)
		if (!Move.bApplied)
			UE_LOG(LogTemp, Warning, TEXT("ApplyBTGraphLayout: no match for '%s' (cur %d,%d)"), *Move.NodeName, Move.CurX, Move.CurY);

	if (TotalMoved > 0) Tree->MarkPackageDirty();
	return TotalMoved;
}

#endif // WITH_EDITOR
