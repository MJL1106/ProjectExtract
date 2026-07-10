# Abandoned Office Floor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Populate the target DemoMap floor with a sparse abandoned-office combat layout whose props reliably generate AICS cover.

**Architecture:** Prepare collision on the small set of mesh assets actually used, then place named StaticMeshActors in four authored clusters. Validate visual composition, navigation connectivity, WorldStatic blocking, and dynamic AICS cover generation before saving.

**Tech Stack:** Unreal Engine 5.7, NeoStack LevelDesign/Screenshot/NavMesh/Playtest APIs, IndustryPropsPack3 StaticMesh assets, AICoverSystem.

## Global Constraints

- Work inline in the current session; do not use subagents.
- Preserve every existing actor and unrelated dirty change in DemoMap.
- Every placed mesh type must include simple box collision.
- Cover-sized actors must block the CoverSystem's ECC_WorldStatic trace.
- Keep doors, stairs, window approaches, and patrol routes clear.
- Keep clutter sparse enough for FPS combat.

---

### Task 1: Prepare the used prop meshes

**Files:**
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Table1.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Table2.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/NegotiationTable.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/FilingCabinets2.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/CopierMFP.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/WaterCooler.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Chair1.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Chair2.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/PaperBox.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Trashcan.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/PC.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Monitor1.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Laptop.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Keyboard.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Phone.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/Paper.uasset`
- Modify: `Extraction/Content/IndustryPropsPack3/Meshes/PaperFolder.uasset`

**Interfaces:**
- Consumes: StaticMesh bounds and existing collision reported by NeoStack.
- Produces: Saved StaticMesh assets with at least one box collision primitive.

- [ ] Inspect collision for each selected mesh and record whether a box already exists.
- [ ] Add a bounds-fitted box primitive to every selected mesh lacking one.
- [ ] Rebuild and save each modified StaticMesh.
- [ ] Re-read collision and confirm `boxes >= 1` for every selected mesh.

### Task 2: Place the abandoned-office clusters

**Files:**
- Modify: `Extraction/Content/UWC_Modular_Skyscraper/Maps/DemoMap.umap`

**Interfaces:**
- Consumes: Collision-ready meshes from Task 1 and the target floor bounds.
- Produces: New StaticMeshActors under `AbandonedOffice_Industry3` with `IP3_AO_` labels.

- [ ] Create the dedicated level folder.
- [ ] Place two sparse workstation clusters along the window-side flank.
- [ ] Place a copier, filing cabinets, paper boxes, and water cooler against the solid wall.
- [ ] Place the negotiation table and displaced chairs as the rear combat anchor.
- [ ] Add restrained tabletop and floor detail without creating movement hazards.
- [ ] Read back every new actor transform, mesh, folder, and collision response.

### Task 3: Refine the combat layout visually

**Files:**
- Modify: `Extraction/Content/UWC_Modular_Skyscraper/Maps/DemoMap.umap`

**Interfaces:**
- Consumes: Initial actor placement from Task 2.
- Produces: A visually coherent layout with three clear travel lanes and 8–12 metre cover spacing.

- [ ] Capture the floor from the original sightline and three complementary angles.
- [ ] Move or rotate props that float, intersect, form accidental barricades, or produce repetitive alignment.
- [ ] Capture the same views again and confirm the abandoned-office read remains sparse.

### Task 4: Verify navigation and AICS cover

**Files:**
- Modify: `Extraction/Content/UWC_Modular_Skyscraper/Maps/DemoMap.umap` only if verification exposes a placement problem

**Interfaces:**
- Consumes: Final prop placement and collision-ready meshes.
- Produces: Connected navigation and runtime cover generated around the new cover-sized props.

- [ ] Rebuild navigation.
- [ ] Test representative paths through the central, window-side, wall-side, and cross-floor routes.
- [ ] Start PIE and wait for dynamic AICS generation on BeginPlay.
- [ ] Verify cover data or debug output around the negotiation table, desks, copier, and filing cabinets.
- [ ] Capture in-game views of the populated floor.
- [ ] Stop PIE on every success or failure path.

### Task 5: Save and audit

**Files:**
- Modify: `Extraction/Content/UWC_Modular_Skyscraper/Maps/DemoMap.umap`
- Modify: selected `Extraction/Content/IndustryPropsPack3/Meshes/*.uasset`

**Interfaces:**
- Consumes: Verified layout from Task 4.
- Produces: Persisted map and mesh collision changes without touching unrelated assets.

- [ ] Save the selected StaticMesh assets and DemoMap.
- [ ] Re-list all `IP3_AO_` actors and confirm the intended count and folder.
- [ ] Re-read every used mesh collision and confirm at least one box.
- [ ] Confirm PIE is stopped and report the screenshots, navigation result, and AICS result.
