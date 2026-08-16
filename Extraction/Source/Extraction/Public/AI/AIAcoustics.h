// Shared acoustic-occlusion helpers for AI hearing.
// Single source of truth for "can this listener hear a noise through the level geometry?"
// Walls/floors/locked doors block sound entirely; openable doors let a muffled fraction
// through (the AI can open them and investigate); open doors don't attenuate.

#pragma once

#include "CoreMinimal.h"

class AActor;
class ADoorBase;
class UWorld;
struct FHitResult;

namespace AIAcoustics
{
	enum class EDoorClass : uint8
	{
		Open,     // doorway acoustically clear
		Openable, // closed, but the AI can open it — muffled pass-through
		Blocking, // locked (or momentarily mid-state) — treated as a wall
	};

	/** How a door on (or near) the sound path treats a noise. */
	EXTRACTION_API EDoorClass ClassifyDoor(ADoorBase* Door);

	/** Open = 1, Openable = ThroughDoorMult, Blocking = 0. */
	EXTRACTION_API float DoorMultiplier(EDoorClass DoorClass, float ThroughDoorMult);

	/** ECC_Visibility line trace Start→End that steps past up to MaxPawnSkips pawn blockers
	 *  (bodies don't stop sound). Returns false when the path is clear of solid blockers;
	 *  true with OutBlock = the first solid hit otherwise. */
	EXTRACTION_API bool TraceSolidBlocker(const UWorld* World, const FVector& Start, const FVector& End,
		TConstArrayView<const AActor*> IgnoreActors, int32 InMaxPawnSkips, FHitResult& OutBlock);

	/** Audibility (0..1) of a noise at StimLoc for a listener whose ears are at ListenerEye.
	 *  1 = clear line or open door; ThroughDoorMult = closed-but-openable door on the line, or
	 *  reachable as a portal when the direct line is walled; NavDetourMult = no door, but a
	 *  same-storey nav route within the detour budget connects the spaces (open-ended walls);
	 *  0 = acoustically sealed (solid rooms, the floor slab between storeys, locked keycard doors). */
	EXTRACTION_API float ComputeMultiplier(UWorld* World, const FVector& ListenerEye, const FVector& StimLoc,
		const AActor* Listener, const AActor* Instigator, float ThroughDoorMult);

	/** Nearest doors tested as portals when the direct line is blocked. */
	inline constexpr int32 PortalCandidateCount = 3;

	/** Stacked pawns a single trace may step past before degrading to "blocked". */
	inline constexpr int32 MaxPawnSkips = 4;

	// --- Doorless-route fallback (open-ended walls, corridor corners) ---
	// When the direct line is walled and no door connects the spaces, a same-storey nav route
	// can still carry the sound. The Z gate is what keeps a multistory building quiet: the
	// slab always walls the direct line, and a stair route to another floor crosses the gate.

	/** Max |Z| between noise and listener feet for the nav-route fallback to run at all —
	 *  below one storey, so a floor slab always keeps blocking. */
	inline constexpr float SameStoreyZThreshold = 260.f;

	/** Route length budget: path must be <= direct * ratio + slack, else the detour is big
	 *  enough that the spaces don't read as acoustically connected. */
	inline constexpr float NavDetourRatioMax = 2.5f;
	inline constexpr float NavDetourSlack = 300.f;

	/** Audibility through a doorless open route. Deliberately above the closed-door multiplier
	 *  (~0.6) — an open gap is nearly clear air, and a sprint heard around a wall end must still
	 *  clear its investigate threshold. */
	inline constexpr float NavDetourMult = 0.85f;
}
