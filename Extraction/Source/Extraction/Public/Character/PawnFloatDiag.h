// Pawn float diagnostics — CVar-gated (debug.PawnFloat 1) anomaly logger for the
// "character floats in the air near doors" bug. Watches EVERY ACharacter in the world
// (player, companion, all enemy classes) and logs walking-on-air (with the exact base
// component underfoot), upward launches, and mesh-vs-capsule offsets so capsule physics
// and animation drift can be told apart from a single log line.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "PawnFloatDiag.generated.h"

UCLASS()
class UPawnFloatDiagSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool IsTickable() const override;
};
