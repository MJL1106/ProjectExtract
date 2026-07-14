# Abandoned Office Floor Design

## Goal

Dress the target DemoMap office floor with IndustryPropsPack3 assets so it reads as recently abandoned while preserving clear FPS combat, enemy patrol, and companion navigation space.

## Layout

- Keep a wide central combat lane through the floor.
- Build two sparse workstation clusters beside the window-side flanking route.
- Build a copier/archive cluster against the solid interior wall.
- Use one displaced negotiation table and chairs as the rear combat anchor.
- Keep doors, stairs, window approaches, and the loop around structural columns unobstructed.
- Space cover-sized obstacles roughly 8–12 metres apart.
- Concentrate small abandoned-office detail on or immediately beside larger furniture.

## Collision and AI Cover

- Every placed mesh type must have at least one simple box collision primitive.
- Cover-sized actors must block the WorldStatic trace channel used by the dynamic AICS CoverSystem.
- Rebuild navigation after placement and confirm representative paths across the central, window, and wall-side lanes.
- Run PIE so the CoverSystem generates from the placed collision, then verify cover generation around the new furniture.

## Scope

- Modify only IndustryPropsPack3 mesh collision and new actors placed on the target floor.
- Put all new actors in one clearly named level folder with a consistent prefix.
- Preserve existing DemoMap actors and unrelated dirty work.
- Use sparse, authored placement rather than procedural scatter.

## Verification

- Capture multiple editor and in-game views after placement.
- Confirm no floating, intersecting, or inaccessible props.
- Confirm each used mesh has box collision.
- Confirm navigation remains connected and AICS produces usable cover around cover-sized props.
