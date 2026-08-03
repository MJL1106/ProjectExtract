//// Copyright, (c) Sami Kangasmaa 2024


#include "StaticCoverArchive.h"
#include "CoverMemory.h"

void FCoverArchive::Save(FCoverMemory* Memory)
{
	check(Memory != nullptr);

	TCEntities<FCoverEntity>* Entities = Memory->GetEntities();
	check(Entities);

	Data.Empty();
	Data.Reserve(Entities->Num());
	for (const FCoverEntity& Entity : (*Entities))
	{
		FCoverArData ArData(Entity.Data);
		Data.Add(ArData);
	}
}

void FCoverArchive::Load(FCoverMemory* Memory)
{
	check(Memory != nullptr);

	Memory->Reset();

	TCEntities<FCoverEntity>* Entities = Memory->GetEntities();
	check(Entities);

	for (const FCoverArData& ArData : Data)
	{
		FCoverEntity Entity;
		Entity.Data = ArData.GetCoverData();

		const FCEntityId Id = Entities->Insert(MoveTemp(Entity));
		(*Entities)[Id].EntityId = Id;
	}

	Memory->BuildOctree();
}

void FCoverArchive::Reset()
{
	Data.Empty();
}

void FCoverSerializedArchive::WriteBytes()
{
	ByteData.Empty();
	FMemoryWriter MemoryWriter(ByteData, true);
	MemoryWriter << Archive;
}

void FCoverSerializedArchive::ReadBytes()
{
	Archive.Reset();
	FMemoryReader MemoryReader(ByteData, true);
	if (MemoryReader.TotalSize() > 0)
	{
		MemoryReader << Archive;
	}
}
