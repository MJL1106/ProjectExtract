//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "CoverSystemVersion.h"
#include "CEntities.h"

#include "CoverSystemPublicData.h"
#include "Math/GenericOctree.h"
#include "Math/BoxSphereBounds.h"
#include "Math/MathFwd.h"
#include "NavigationSystem.h"
#include "Navmesh/RecastNavMesh.h"

/**
 * Entity that is stored into entity container per cover
 */
struct FCoverEntity
{
	// Generated data of this cover.
	FCoverData Data = FCoverData();

	// Id of this entity in the entity container
	FCEntityId EntityId = InvalidCEntityId;

	FCoverEntity() {}
};

struct FCoverOctreeElement
{
	FBoxSphereBounds Bounds = FBoxSphereBounds();

	/** Entity id of the cover */
	FCEntityId EntityId = InvalidCEntityId;

	FCoverOctreeElement(const FCoverEntity& InEntity)
		: EntityId(InEntity.EntityId)
	{
		Bounds = FBoxSphereBounds(&(InEntity.Data.Location), 1);
	}

	FCoverOctreeElement() {}
};

static_assert(std::is_trivially_copyable_v <FCoverOctreeElement> == true);

struct FCoverOctreeSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef FDefaultAllocator ElementAllocator;

	FORCEINLINE static bool AreElementsEqual(const FCoverOctreeElement& A, const FCoverOctreeElement& B)
	{
		return A.EntityId == B.EntityId;
	}

	static void SetElementId(const FCoverOctreeElement& Element, FOctreeElementId2 Id) {}

	FORCEINLINE static const FBoxSphereBounds& GetBoundingBox(const FCoverOctreeElement& Element)
	{
		return Element.Bounds;
	}

	FCoverOctreeSemantics() {}
};

using FCoverOctree = TOctree2<FCoverOctreeElement, FCoverOctreeSemantics>;

/**
 * Memory structure of cover entities and octree
 */
struct FCoverMemory
{
private:

	// Explicitly prevent usage of default constructor
	FCoverMemory() {}

	// Delete assignments. Copying this isn't allowed
	FCoverMemory& operator=(const FCoverMemory&) = delete;   // assignment constructor 
	FCoverMemory& operator=(FCoverMemory&&) = delete;   // move assignment

	friend struct FCoverArchive;

	FVector OctreeOrigin = FVector::ZeroVector;
	double OctreeExtent = 1.0;
	TCEntities<FCoverEntity>* Entities = nullptr;
	FCoverOctree* Octree = nullptr;

	void Initialize();
	void Release();

public:

	FCoverMemory(double InOctreeExtent, FVector InOctreeOrigin);
	~FCoverMemory();

	void Reset();

	TCEntities<FCoverEntity>* GetEntities() const 
	{ 
		check(Entities); 
		return Entities; 
	}

	FCoverOctree* GetOctree() const 
	{ 
		check(Octree); 
		return Octree;
	}

	void BuildOctree();
};


namespace CoverMemoryUtils
{
	inline FCEntityId ConvertHandle(const FCoverHandle& Handle)
	{
		FCEntityId Id = InvalidCEntityId;
		Id.value = Handle.Get();
		return Id;
	}

	inline FCoverHandle ConvertEntityId(FCEntityId Id, const FCoverPartitionHash& Partition)
	{
		return FCoverHandle(Id.value, Partition);
	}
}