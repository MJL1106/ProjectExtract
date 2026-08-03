//// Copyright, (c) Sami Kangasmaa 2024

#pragma once

#include "CoreMinimal.h"
#include "CoverSystemPublicData.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "StaticCoverArchive.generated.h"

struct FCoverArData
{
public:

	FVector L;
	FVector DTW;
	bool bLCS;
	bool bRCS;
	bool bLCC;
	bool bRCC;
	bool bFCC;

	friend FArchive& operator<<(FArchive& Ar, FCoverArData& ArData)
	{
		Ar << ArData.L;
		Ar << ArData.DTW;
		Ar << ArData.bLCS;
		Ar << ArData.bRCS;
		Ar << ArData.bLCC;
		Ar << ArData.bRCC;
		Ar << ArData.bFCC;
		return Ar;
	}

	FCoverArData()
	{
		L = FVector::ZeroVector;
		DTW = FVector::OneVector;
		bLCS = false;
		bRCS = false;
		bLCC = false;
		bRCC = false;
		bFCC = false;
	}

	FCoverArData(const FCoverData& Data)
	{
		L = Data.Location;
		DTW = Data.DirectionToWall;
		bLCS = Data.bLeftCoverStanding;
		bRCS = Data.bRightCoverStanding;
		bLCC = Data.bLeftCoverCrouched;
		bRCC = Data.bRightCoverCrouched;
		bFCC = Data.bFrontCoverCrouched;
	}

	inline FCoverData GetCoverData() const
	{
		FCoverData Data;
		Data.Location = L;
		Data.SetDirectionToWall(DTW);
		Data.bLeftCoverStanding = bLCS;
		Data.bRightCoverStanding = bRCS;
		Data.bLeftCoverCrouched = bLCC;
		Data.bRightCoverCrouched = bRCC;
		Data.bFrontCoverCrouched = bFCC;
		Data.bCrouchedCover = bLCC || bRCC || bFCC;
		return Data;
	}
};

/**
 * Archive to store static covers
 */
struct FCoverArchive
{
protected:

	TArray<FCoverArData> Data;

public:

	FCoverArchive() {}

	friend FArchive& operator<<(FArchive& Ar, FCoverArchive& ArData)
	{
		Ar << ArData.Data;
		return Ar;
	}

	void Save(struct FCoverMemory* Memory);
	void Load(struct FCoverMemory* Memory);

	void Reset();

	int32 GetDataNum() const { return Data.Num(); }
};

/**
 * Archive that is saved as bytes and deserialized into inner FCoverArchive on load
 */
USTRUCT()
struct AICOVERSYSTEM_API FCoverSerializedArchive
{
	GENERATED_BODY()

private:

	// Inner archive structure that is created from ByteData or used to save into ByteData
	FCoverArchive Archive;

	// Bulk data that is serialized to disk
	UPROPERTY()
	TArray<uint8> ByteData;

public:

	void WriteBytes();

	void ReadBytes();

	/**
	 * Saves covers in memory to this archive
	 */
	void Save(struct FCoverMemory* Memory)
	{
		Archive.Save(Memory);
		WriteBytes();
	}

	/**
	 * Loads this archive into given memory
	 */
	void Load(struct FCoverMemory* Memory, bool bDeserializeFromBytes = true)
	{
		if (bDeserializeFromBytes)
		{
			ReadBytes();
		}
		Archive.Load(Memory);
	}

	/**
	 * Resets the archive
	 */
	void Reset()
	{
		ByteData.Empty();
		Archive.Reset();
	}

	bool HasSaveData() const
	{
		return Archive.GetDataNum() > 0;
	}

	bool HasLoadData() const
	{
		return ByteData.Num() > 0 || Archive.GetDataNum() > 0;
	}

	int32 GetSerializedByteNum() const
	{
		return ByteData.Num();
	}

	int32 GetCoverDataNum() const
	{
		return Archive.GetDataNum();
	}
};