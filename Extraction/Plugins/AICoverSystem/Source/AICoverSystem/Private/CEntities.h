//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "handle_map_base.h"

/**
* Wrap handle_map for Unreal usage and define Entity Id data type for cover system
**/

using FCEntityId = griffin2::Id_T;
#define InvalidCEntityId griffin2::Id_T{}

inline uint32 GetTypeHash(const FCEntityId& EntityId)
{
	return GetTypeHash((uint64)EntityId.value);
}

static_assert(std::is_standard_layout_v<FCEntityId> == true);
static_assert(std::is_trivially_copyable_v <FCEntityId> == true);

template<typename TDataType>
class TCEntities : public griffin2::handle_map<TDataType>
{
public:

	explicit TCEntities(int32 ReserveNum)
		: griffin2::handle_map<TDataType>(0, (size_t)ReserveNum) {}

	TCEntities()
		: griffin2::handle_map<TDataType>(0, 0) {}

public: // Unreal friendly functions

	inline FCEntityId Insert(TDataType&& Element) { return this->insert(Element); }
	inline FCEntityId Insert(const TDataType& Element) { return this->insert(Element); }
	inline bool Remove(FCEntityId EntityId) { const size_t rNum = this->erase(EntityId); return rNum > 0; }

	inline void Reset() { this->reset(); }
	inline bool IsValidId(FCEntityId EntityId) const { return this->isValid(EntityId); }
	inline int32 Num() const { return (int32)this->size(); }
};
