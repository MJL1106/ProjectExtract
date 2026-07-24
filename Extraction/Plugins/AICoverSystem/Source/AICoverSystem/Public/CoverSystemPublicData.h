//// Copyright, (c) Sami Kangasmaa 2022

/*
*
* Contains modified subtantial algorithms or/and parts of code from original work of Deams51 licensed under MIT License:

	Copyright (c) 2016 Deams51

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

* THE THIRD PARTY SNIPPETS USED IN CODE ARE MARKED WITH COMMENTED SECTIONS.

*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "CoverSystemVersion.h"
#include "Engine/HitResult.h"
#include "CoverSystemPublicData.generated.h"

/**
* Build parameters for the cover generation
*/
USTRUCT(BlueprintType)
struct AICOVERSYSTEM_API FCoverBuildParams
{
	GENERATED_BODY()

public:

	// -------- Code from Deams51 work [Section begin] ------------ //

	// Length of nav mesh sub segments. Smaller value is more accurate but consumes more CPU
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float SegmentLength = 20.0f;

	// Length of trace when trying to find blocking geometry in direction of nav segment
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float TraceLength = 100.0f;

	// Raytrace substep distance when finding geometry that may provide cover
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float TraceStepDistance = 25.0f;

	/** 
	* Size of the sphere sweep used to determine if cover can be used to shoot to certain directions
	* Use 0 to check it with raycasts which is faster but may not be as accurate as sweep
	*/
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float SweepTraceRadius = 5.0f;

	// Minimum space between generated covers
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float MinSpaceBetweenValidPoints = 50.0f;

	/**
	* The maximum width of the agents.
	* Generation ensures that agent with this width is fully covered behind an object if standing at cover
	*/
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float AgentMaxWidth = 80.0f;

	/** The maximum height of the agents when crouching */
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float AgentMaxHeightCrouch = 80.0f;

	/** The maximum height of the agents when standing */
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float AgentMaxHeightStanding = 180.0f;

	/** The distance between in cover position and leaning out of the cover on the sides */
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float OffsetWhenLeaningSides = 65.0f;

	/** The distance in front of a shooting position that must be free */
	UPROPERTY(Category = Cover, EditAnywhere, BlueprintReadWrite)
	float OffsetFrontAim = 100.0f;

	// -------- Code from Deams51 work [Section end] ------------ //
};

/**
 * Represents data of single cover
 */
USTRUCT(BlueprintType)
struct AICOVERSYSTEM_API FCoverData
{
	GENERATED_BODY()

public:

	// -------- Code from Deams51 work [Section begin] ------------ //

	/**  Location of the cover */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	FVector Location;

	/** Rotator from X of direction to wall */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	FRotator Rotation;

	/** Direction to wall (Perpendicular to cover) */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	FVector DirectionToWall;

	/** Is it a Left cover (can lean on left) */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	bool bLeftCoverStanding;

	/** Is it a Right cover (can lean on Right) */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	bool bRightCoverStanding;

	/** Is it a Left cover (can lean on left) */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	bool bLeftCoverCrouched;

	/** Is it a Right cover (can lean on Right) */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	bool bRightCoverCrouched;

	/** Is it a front cover, so crouch gives cover and while standing there's line of sight for shooting */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	bool bFrontCoverCrouched;

	/** Is it a cover requiring crouch */
	UPROPERTY(Category = Cover, VisibleAnywhere, BlueprintReadOnly)
	bool bCrouchedCover;

public:

	FVector GetDirectionToWall() const { return DirectionToWall; }
	void SetDirectionToWall(const FVector& Direction)
	{
		DirectionToWall = Direction;
		Rotation = DirectionToWall.ToOrientationRotator();
	}

	// -------- Code from Deams51 work [Section end] ------------ //

	FCoverData()
	{
		Location = FVector::ZeroVector;
		Rotation = FRotator::ZeroRotator;
		DirectionToWall = FVector::ZeroVector;
		bLeftCoverStanding = false;
		bRightCoverStanding = false;
		bLeftCoverCrouched = false;
		bRightCoverCrouched = false;
		bFrontCoverCrouched = false;
		bCrouchedCover = false;
	}
};

/**
 * Handle to access certain partition in cover system
 * Each partition has own cover system proxy
 */
USTRUCT()
struct AICOVERSYSTEM_API FCoverPartitionHash
{
	GENERATED_BODY()

private:

	UPROPERTY()
	int16 X;

	UPROPERTY()
	int16 Y;

	UPROPERTY()
	int16 Z;

public:

	FCoverPartitionHash()
	{
		X = 0;
		Y = 0;
		Z = 0;
	}

	FCoverPartitionHash(int16 InX, int16 InY, int16 InZ)
	{
		X = InX;
		Y = InY;
		Z = InZ;
	}

	FCoverPartitionHash(const FVector& Location, double Size)
	{
		X = (int16)FMath::RoundToInt(Location.X / Size);
		Y = (int16)FMath::RoundToInt(Location.Y / Size);
		Z = (int16)FMath::RoundToInt(Location.Z / Size);
	}

	bool operator==(const FCoverPartitionHash& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}

	bool operator!=(const FCoverPartitionHash& Other) const
	{
		return X != Other.X || Y != Other.Y || Z != Other.Z;
	}

	uint32 GetHash() const 
	{
		return ((uint32)((X & 0x0000FFFF) | (Y & 0xFFFF0000))) ^ Z;
	}

	friend uint32 GetTypeHash(const FCoverPartitionHash& Handle)
	{
		return Handle.GetHash();
	}

	template<typename TContainerType>
	static void AppendPartitionsWithinBounds(const FVector& Origin, const FVector& Extent, double Size, TContainerType& OutPartitions)
	{
		const FVector MinBound = FVector(Origin.X - Extent.Y, Origin.Y - Extent.Y, Origin.Z - Extent.Z);
		const FVector MaxBound = FVector(Origin.X + Extent.X, Origin.Y + Extent.Y, Origin.Z + Extent.Z);

		int32 MinX, MaxX, MinY, MaxY, MinZ, MaxZ;
		MinX = FMath::RoundToInt(MinBound.X / Size);
		MaxX = FMath::RoundToInt(MaxBound.X / Size);
		MinY = FMath::RoundToInt(MinBound.Y / Size);
		MaxY = FMath::RoundToInt(MaxBound.Y / Size);
		MinZ = FMath::RoundToInt(MinBound.Z / Size);
		MaxZ = FMath::RoundToInt(MaxBound.Z / Size);
		check(MinX <= MaxX);
		check(MinY <= MaxY);
		check(MinZ <= MaxZ);

		const int32 CellNum = (MaxX - MinX) * (MaxY - MinY) * (MaxZ - MinZ);
		OutPartitions.Reserve(CellNum);

		for (int32 iX = MinX; iX <= MaxX; ++iX)
		{
			for (int32 iY = MinY; iY <= MaxY; ++iY)
			{
				for (int32 iZ = MinZ; iZ <= MaxZ; ++iZ)
				{
					OutPartitions.Add(FCoverPartitionHash(iX, iY, iZ));
				}
			}
		}
	}

	FBox GetPartitionBound(double Size) const
	{
		const FVector Origin = GetPartitionOrigin(Size);
		const FVector Extent = FVector(Size * 0.5, Size * 0.5, Size * 0.5);
		return FBox::BuildAABB(Origin, Extent);
	}

	FVector GetPartitionOrigin(double Size) const
	{
		return FVector(X * Size, Y * Size, Z * Size);
	}
};

// Unique handle to access certain cover in the memory
USTRUCT(BlueprintType)
struct AICOVERSYSTEM_API FCoverHandle
{
	GENERATED_BODY()

public:

	FCoverHandle();

	FCoverHandle(uint64_t InValue, const FCoverPartitionHash& InPartition);

	uint64_t Get() const { return handle; }

	const FCoverPartitionHash& GetPartition() const { return partition; }

	bool IsValid() const;

	bool operator==(const FCoverHandle& Other) const
	{
		return handle == Other.handle && partition == Other.partition;
	}

	bool operator!=(const FCoverHandle& Other) const
	{
		return handle != Other.handle || partition != Other.partition;
	}

	friend uint32 GetTypeHash(const FCoverHandle& Handle)
	{
		const uint32 BaseHash = ::GetTypeHash(Handle.handle);
		return HashCombineFast(BaseHash, Handle.partition.GetHash());
	}

private:

	uint64_t handle;
	FCoverPartitionHash partition;
};

/**
 * Represents a single cover.
 */
USTRUCT(BlueprintType)
struct AICOVERSYSTEM_API FCover
{
	GENERATED_BODY()

public:

	FCover()
	{
		Handle = FCoverHandle();
		Data = FCoverData();
	}

	FCover(const FCoverHandle& InHandle, const FCoverData& InData)
	{
		Handle = InHandle;
		Data = InData;
	}

	// Handle to the cover.
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Cover")
	FCoverHandle Handle;

	// Data of the cover
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Cover")
	FCoverData Data;

public:

	inline bool Equals(const FCover& Other) const 
	{
		return Handle == Other.Handle;
	}

	bool operator==(const FCover& Other) const
	{
		return Equals(Other);
	}

	bool operator!=(const FCover& Other) const
	{
		return !(Equals(Other));
	}

	inline void Invalidate()
	{
		Handle = FCoverHandle();
	}

	inline bool IsValid() const
	{
		return Handle.IsValid();
	}
};