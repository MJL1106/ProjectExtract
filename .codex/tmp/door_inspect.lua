local paths = {
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Door01",
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Door01_Closed",
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Door02",
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Door03",
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Door04",
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Door_Glass",
  "/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Double_Door"
}
for _, p in ipairs(paths) do
  print("=== " .. p)
  local bp = open_asset(p)
  if bp then
    bp:info()
    local comps = bp:list("components")
    if comps then
      for _, c in ipairs(comps) do print("COMP " .. tostring(c)) end
    end
  else print("OPEN_FAILED") end
end
